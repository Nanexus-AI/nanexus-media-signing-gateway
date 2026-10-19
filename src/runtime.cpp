// SPDX-License-Identifier: Apache-2.0
#include "nanexus_gateway/runtime.hpp"
#include "nanexus_gateway/media.hpp"
#include "nanexus_gateway/signer.hpp"
#include "nanexus_gateway/publisher.hpp"
#include "Rtsp/RtspPlayer.h"
#include "Common/config.h"
#include <openssl/crypto.h>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>
#include <stdexcept>
namespace nanexus {
namespace {
using Clock=std::chrono::steady_clock;
class Input final:public mediakit::PlayerImp<mediakit::RtspPlayer,mediakit::PlayerBase> {
    using Base=mediakit::PlayerImp<mediakit::RtspPlayer,mediakit::PlayerBase>;
public:
    explicit Input(const toolkit::EventPoller::Ptr& p):Base(p) {}
    int video_pt=-1;
    std::function<bool(const char*,size_t)> raw;
    std::function<void(mediakit::RtpPacket::Ptr)> packet;
protected:
    bool onCheckSDP(const std::string& s) override {
        std::istringstream lines(s); std::string line; unsigned videos=0;
        while(std::getline(lines,line)) if(line.compare(0,8,"m=video ")==0) ++videos;
        if(videos!=1) return false;
        mediakit::SdpParser parser(s); auto v=parser.getTrack(mediakit::TrackVideo);
        if(!v || v->_codec!="H264" || v->_samplerate!=90000) return false;
        video_pt=v->_pt; return true;
    }
    void onRtpPacket(const char* p,size_t n) override { if(raw(p,n)) mediakit::RtspPlayer::onRtpPacket(p,n); }
    void onRecvRTP(mediakit::RtpPacket::Ptr p,const mediakit::SdpTrack::Ptr& t) override {
        if(t->_type==mediakit::TrackVideo && p->getHeader()->pt==video_pt) packet(std::move(p));
    }
};
struct AuthBuffer {
    std::string contents;
    ~AuthBuffer() { if(!contents.empty()) OPENSSL_cleanse(&contents[0],contents.size()); }
};
class QuietLog final:public toolkit::LogChannel {
public:
    QuietLog():LogChannel("gateway") {}
    void write(const toolkit::Logger&,const toolkit::LogContextPtr&) override {}
};
struct Runtime:std::enable_shared_from_this<Runtime> {
    Config config;
    toolkit::EventPoller::Ptr poller;
    std::shared_ptr<Input> input;
    mediakit::H264RtpDecoder decoder;
    AuAdmission admission;
    std::unique_ptr<Signer> signer;
    std::unique_ptr<Publisher> publisher;
    AccessUnit pending;
    bool have_pending=false,wire=false,sorted=false,finishing=false,admitted=false;
    uint16_t sequence=0, sorted_sequence=0;
    uint32_t ssrc=0;
    uint64_t aus=0,nals=0;
    Clock::time_point begin=Clock::now(),last=begin,first=begin;
    unsigned duration;
    const std::atomic<bool>& stop;
    std::promise<int> result;
    toolkit::EventPoller::DelayTask::Ptr timer,cleanup;
    Runtime(const Config& c,unsigned d,const std::atomic<bool>& s):config(c),admission(c),duration(d),stop(s) {}
    void finish(bool deliberate,const char* reason) {
        if(finishing) return;
        finishing=true; timer.reset();
        bool finalized=false;
        if(input) input->teardown();
        if(deliberate && have_pending) {
            try { publisher->publish(signer->sign(pending,true)); ++aus; finalized=true; }
            catch(...) { reason="finalization_failed"; }
        }
        auto weak=std::weak_ptr<Runtime>(shared_from_this());
        // Keep publication alive for a bounded opportunity to send trailers. No receipt claim.
        cleanup=poller->doDelayTask(finalized?config.drain_timeout_ms:1,[weak,finalized,reason]() {
            if(auto self=weak.lock()) {
                self->input.reset(); self->publisher.reset(); self->signer.reset();
                std::cout<<"state="<<(finalized?"stopped":"failed")<<" reason="<<reason
                         <<" published_aus="<<self->aus<<" admitted_original_nals="<<self->nals
                         <<" eos_emitted="<<(finalized?1:0)<<" recording_completeness=unknown\n";
                self->result.set_value(finalized?0:1);
            }
            return uint64_t(0);
        });
    }
    bool raw(const char* data,size_t n) {
        if(finishing) return false;
        try {
            if(n<4 || uint8_t(data[0])!='$' || size_t((uint8_t(data[2])<<8)|uint8_t(data[3]))!=n-4) throw std::runtime_error("interleaved framing");
            if(uint8_t(data[1])&1) return true; // RTCP is handled by upstream.
            auto p=parse_rtp(reinterpret_cast<const uint8_t*>(data+4),n-4);
            if(p.pt!=input->video_pt) return true; // Unselected tracks are not signed.
            if(wire && (p.sequence!=uint16_t(sequence+1) || p.ssrc!=ssrc)) throw std::runtime_error("RTP continuity");
            wire=true; sequence=p.sequence; ssrc=p.ssrc; last=Clock::now(); return true;
        } catch(...) { finish(false,"input_continuity_failed"); return false; }
    }
    void receive(const mediakit::RtpPacket::Ptr& packet) {
        if(finishing) return;
        try {
            auto v=parse_rtp(reinterpret_cast<const uint8_t*>(packet->data()+4),packet->size()-4);
            if(v.ssrc!=ssrc || v.pt!=input->video_pt || (sorted && v.sequence!=uint16_t(sorted_sequence+1))) throw std::runtime_error("sorted RTP continuity");
            sorted=true; sorted_sequence=v.sequence;
            if(!admission.before(v)) return;
            decoder.inputRtp(packet);
            AccessUnit au;
            if(!admission.complete(au)) return;
            if(!admitted) { publisher.reset(new Publisher(config,poller,au)); admitted=true; first=Clock::now(); }
            if(have_pending) { publisher->publish(signer->sign(pending)); ++aus; }
            nals+=au.nals.size(); pending=std::move(au); have_pending=true;
        } catch(...) { finish(false,"media_signing_or_publication_failed"); }
    }
    void start() {
        try {
            signer.reset(new Signer(config));
            input=std::make_shared<Input>(poller);
            // Upstream diagnostics are suppressed because they can contain credentials/URLs.
            (*input)[mediakit::Client::kRtpType]=mediakit::Rtsp::RTP_TCP;
            (*input)[mediakit::Client::kTimeoutMS]=config.startup_timeout_ms;
            (*input)[mediakit::Client::kMediaTimeoutMS]=config.gap_timeout_ms;
            if(!config.camera_auth_file.empty()) {
                std::ifstream in(config.camera_auth_file,std::ios::binary);
                AuthBuffer secret; auto& contents=secret.contents; char c;
                while(in.get(c)) { if(contents.size()>=2048) throw std::runtime_error("auth bound"); contents+=c; }
                auto nl=contents.find('\n');
                if(!in.eof() || nl==std::string::npos || nl==0 || contents.back()!='\n' || contents.find('\n',nl+1)!=contents.size()-1 || nl+1==contents.size()-1 || contents.find('\0')!=std::string::npos || contents.find('\r')!=std::string::npos) throw std::runtime_error("invalid camera authentication file");
                (*input)[mediakit::Client::kRtspUser]=contents.substr(0,nl);
                (*input)[mediakit::Client::kRtspPwd]=contents.substr(nl+1,contents.size()-nl-2);
            }
            auto weak=std::weak_ptr<Runtime>(shared_from_this());
            input->raw=[weak](const char* p,size_t n) { auto s=weak.lock(); return s && s->raw(p,n); };
            input->packet=[weak](mediakit::RtpPacket::Ptr p) { if(auto s=weak.lock()) s->receive(p); };
            input->setOnPlayResult([weak](const toolkit::SockException& e) { if(e) if(auto s=weak.lock()) s->finish(false,"camera_start_failed"); });
            input->setOnShutdown([weak](const toolkit::SockException&) { if(auto s=weak.lock()) s->finish(false,"camera_source_lost"); });
            decoder.addDelegate([weak](const mediakit::Frame::Ptr& f) {
                if(auto s=weak.lock()) { s->admission.nal(f->data()+f->prefixSize(),f->size()-f->prefixSize()); }
                return true;
            });
            begin=last=Clock::now();
            timer=poller->doDelayTask(50,[weak]() {
                auto s=weak.lock(); if(!s || s->finishing) return uint64_t(0);
                auto now=Clock::now();
                if(s->wire && now-s->last>=std::chrono::milliseconds(s->config.gap_timeout_ms)) s->finish(false,"media_gap");
                else if(s->stop.load(std::memory_order_relaxed) || (s->admitted && s->duration && now-s->first>=std::chrono::seconds(s->duration))) s->finish(true,"deliberate_stop");
                else if(!s->admitted && now-s->begin>=std::chrono::milliseconds(s->config.startup_timeout_ms)) s->finish(false,"admission_startup_timeout");
                return s->finishing?uint64_t(0):uint64_t(50);
            });
            input->play(config.camera_url);
        } catch(...) { finish(false,"initialization_failed"); }
    }
};
}
int run_gateway(const Config& c,unsigned seconds,const std::atomic<bool>& stop) {
    toolkit::Logger::Instance().add(std::make_shared<QuietLog>());
    toolkit::EventPollerPool::setPoolSize(1);
    auto runtime=std::make_shared<Runtime>(c,seconds,stop);
    runtime->poller=toolkit::EventPollerPool::Instance().getFirstPoller();
    auto future=runtime->result.get_future();
    runtime->poller->async([runtime]() { runtime->start(); });
    return future.get();
}
}
