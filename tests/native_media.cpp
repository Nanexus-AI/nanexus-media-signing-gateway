// SPDX-License-Identifier: Apache-2.0
#include "nanexus_gateway/media.hpp"
#include "nanexus_gateway/signer.hpp"
#include "nanexus_gateway/publisher.hpp"
#include <future>
#include "ext-codec/H264Rtp.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace nanexus;
using namespace mediakit;
void check(bool b,const char* s) { if(!b) throw std::runtime_error(s); }
template<class F> void rejects(F f) { bool threw=false; try { f(); } catch(const std::exception&) { threw=true; } check(threw,"expected rejection"); }
std::vector<Frame::Ptr> frames(const AccessUnit& au) {
    std::vector<Frame::Ptr> out;
    for(const auto& n:au.nals) { auto f=FrameImp::create<H264Frame>(); f->_buffer.assign("\0\0\0\1",4); f->_buffer.append(n.data(),n.size()); f->_prefix_size=4; f->_pts=f->_dts=au.pts_ms; out.push_back(f); }
    return out;
}
struct Reconstruct:toolkit::RingDelegate<RtpPacket::Ptr> {
    H264RtpDecoder decoder;
    std::vector<std::string> nals;
    size_t markers=0,packets=0;
    uint16_t sequence=0;
    uint64_t pts=0;
    explicit Reconstruct() {
        decoder.addDelegate([this](const Frame::Ptr& f) { nals.emplace_back(f->data()+f->prefixSize(),f->size()-f->prefixSize()); return true; });
    }
    void onWrite(RtpPacket::Ptr p,bool) override {
        auto v=parse_rtp(reinterpret_cast<const uint8_t*>(p->data()+4),p->size()-4);
        check(p->size()<=1204,"MTU exceeded");
        if(packets) check(v.sequence==uint16_t(sequence+1),"packet sequence mismatch");
        sequence=v.sequence; ++packets;
        check(v.stamp==uint32_t(pts*90),"packet timestamp mismatch");
        decoder.inputRtp(p);
        if(v.marker) ++markers;
    }
};
struct Encoder {
    H264RtpEncoder encoder;
    std::shared_ptr<Reconstruct> rx=std::make_shared<Reconstruct>();
    Encoder() { auto ring=std::make_shared<RtpRing::RingType>(); ring->setDelegate(rx); encoder.setRtpRing(ring); encoder.setRtpInfo(1234,1200,90000,96); }
    void exact(const AccessUnit& au) {
        rx->pts=au.pts_ms; rx->nals.clear(); auto marks=rx->markers;
        check(encoder.inputSignedAccessUnit(frames(au),au.random_access),"encoder rejected valid AU");
        check(rx->nals==au.nals && rx->markers==marks+1,"packetization mutation/marker mismatch");
    }
};
void units() {
    Encoder e;
    AccessUnit a; a.pts_ms=1000;
    a.nals={std::string("\x09\xf0",2),std::string("\x06",1)+std::string(3000,'x'),std::string("\x65\xb8\x80",3)};
    a.random_access=true; e.exact(a);
    check(!e.encoder.inputSignedAccessUnit({},false),"empty AU accepted");
    auto bad=frames(a); auto f=FrameImp::create<H264Frame>(); f->_buffer="\x61\xb8"; f->_pts=999; bad.push_back(f);
    check(!e.encoder.inputSignedAccessUnit(bad,false),"mixed PTS accepted");
    Encoder mixed; auto sps=FrameImp::create<H264Frame>(); sps->_buffer="\x67\x42\x00\x1e"; mixed.encoder.inputFrame(sps);
    check(!mixed.encoder.inputSignedAccessUnit(frames(a),true),"cached stock input accepted");
    std::string empty(12,'\0'); rejects([&] { parse_rtp(reinterpret_cast<const uint8_t*>(empty.data()),empty.size()); });
    Config c; AuAdmission gate(c); RtpView p; p.marker=true; gate.before(p);
    uint8_t stap[]={24,0,9,0x65}; p.payload=stap; p.size=sizeof(stap); rejects([&] { gate.before(p); });
    AuAdmission fu(c); p=RtpView(); p.marker=true; fu.before(p);
    uint8_t fragment[]={28,0x85,0xb8}; p.marker=false; p.payload=fragment; p.size=3; check(fu.before(p),"FU start rejected");
    uint8_t changed[]={28,0x41,0}; p.payload=changed; p.marker=true; rejects([&] { fu.before(p); });
    Config small; small.max_nal_bytes=2; AuAdmission bounded(small); p=RtpView(); p.marker=true; bounded.before(p); p.marker=false; p.payload=fragment; p.size=3; bounded.before(p); rejects([&] { bounded.before(p); });
    AuAdmission unsupported(c); p=RtpView(); p.marker=true; unsupported.before(p);
    uint8_t b_slice[]={0x61,0xa0}; p.payload=b_slice; p.size=2; unsupported.before(p);
    unsupported.nal(reinterpret_cast<const char*>(b_slice),2);
    AccessUnit rejected; rejects([&] { unsupported.complete(rejected); });
    AuAdmission incomplete(c); p=RtpView(); p.marker=true; incomplete.before(p);
    p.payload=fragment; p.size=3; rejects([&] { incomplete.before(p); });
    std::cout<<"PASS native packetization and admission guards\n";
}
std::vector<std::string> split(const std::string& data) {
    std::vector<std::pair<size_t,size_t>> starts;
    for(size_t i=0;i+3<=data.size();) {
        size_t prefix=0;
        if(i+4<=data.size() && data.compare(i,4,std::string("\0\0\0\1",4))==0) prefix=4;
        else if(data.compare(i,3,std::string("\0\0\1",3))==0) prefix=3;
        if(prefix) { starts.emplace_back(i,prefix); i+=prefix; } else ++i;
    }
    check(!starts.empty() && starts[0].first==0,"invalid Annex-B fixture");
    std::vector<std::string> out;
    for(size_t i=0;i<starts.size();++i) {
        auto end=i+1<starts.size()?starts[i+1].first:data.size(); auto begin=starts[i].first+starts[i].second;
        check(end>begin,"empty fixture NAL"); out.push_back(data.substr(begin,end-begin));
    }
    return out;
}
struct AdmitPackets:toolkit::RingDelegate<RtpPacket::Ptr> {
    AuAdmission gate;
    H264RtpDecoder decoder;
    AccessUnit result;
    bool ready=false;
    explicit AdmitPackets(const Config& c):gate(c) {
        RtpView skipped; skipped.marker=true; gate.before(skipped);
        decoder.addDelegate([this](const Frame::Ptr& f) { gate.nal(f->data()+f->prefixSize(),f->size()-f->prefixSize()); return true; });
    }
    void onWrite(RtpPacket::Ptr packet,bool) override {
        auto v=parse_rtp(reinterpret_cast<const uint8_t*>(packet->data()+4),packet->size()-4);
        if(gate.before(v)) { decoder.inputRtp(packet); if(gate.complete(result)) ready=true; }
    }
};
void publisher_component(Config config,const AccessUnit& signed_au) {
    config.output_url="rtsp://127.0.0.1:0/component/signed"; // Ephemeral test listener only.
    toolkit::EventPollerPool::setPoolSize(1);
    auto poller=toolkit::EventPollerPool::Instance().getFirstPoller();
    auto promise=std::make_shared<std::promise<void>>(); auto done=promise->get_future();
    poller->async([config,signed_au,poller,promise]() {
        try {
            Publisher publisher(config,poller,signed_au);
            publisher.publish(signed_au); // Production precedes any reader.
            auto source=std::dynamic_pointer_cast<RtspMediaSource>(MediaSource::find(MediaInfo(config.output_url)));
            check(bool(source),"owned RTSP source not registered");
            auto rx=std::make_shared<Reconstruct>(); rx->pts=signed_au.pts_ms;
            auto reader=source->getRing()->attach(poller,true);
            reader->setReadCB([rx](const RtspMediaSource::RingDataType& list) {
                list->for_each([rx](const RtpPacket::Ptr& packet) { rx->onWrite(packet,false); });
            });
            check(rx->nals==signed_au.nals && rx->markers==1,"publisher cache changed signed AU");
            promise->set_value();
        } catch(...) { promise->set_exception(std::current_exception()); }
    });
    check(done.wait_for(std::chrono::seconds(5))==std::future_status::ready,"publisher test deadline"); done.get();
}
void fixture(char** args) {
    Config c; c.key_file=args[3]; c.certificate_file=args[4];
    Signer signer(c); Encoder transport;
    auto admission=std::make_shared<AdmitPackets>(c);
    H264RtpEncoder input_encoder; input_encoder.setRtpInfo(1234,1200,90000,96);
    auto input_ring=std::make_shared<RtpRing::RingType>(); input_ring->setDelegate(admission); input_encoder.setRtpRing(input_ring);
    std::ifstream input(args[1],std::ios::binary),manifest(args[2]);
    std::ofstream signed_file(std::string(args[5])+"/signed.h264",std::ios::binary),received(std::string(args[5])+"/reconstructed.h264",std::ios::binary);
    check(bool(input)&&bool(manifest)&&bool(signed_file)&&bool(received),"test files unavailable");
    size_t offset,size,end=0,total_nals=0,added=0,aus=0; uint64_t pts,dts,duration;
    // A pending final AU makes the EOS test match runtime publication semantics.
    AccessUnit pending; bool have=false;
    auto process=[&](const AccessUnit& original,bool final) {
        auto out=signer.sign(original,final); size_t j=0;
        for(const auto& n:out.nals) {
            if(j<original.nals.size() && n==original.nals[j]) ++j;
            else { check((uint8_t(n[0])&31)==6,"non-SEI inserted"); ++added; }
            signed_file.write("\0\0\0\1",4); signed_file.write(n.data(),n.size());
        }
        check(j==original.nals.size(),"original NAL mutation/order"); total_nals+=j;
        transport.exact(out);
        if(aus==0) publisher_component(c,out);
        for(const auto& n:transport.rx->nals) { received.write("\0\0\0\1",4); received.write(n.data(),n.size()); }
        ++aus;
    };
    while(manifest>>offset>>size>>pts>>dts>>duration) {
        check(offset==end && size>0 && size<=c.max_au_bytes && aus<1000,"fixture bound");
        std::string data(size,'\0'); input.read(&data[0],size); check(bool(input),"fixture truncated"); end+=size;
        AccessUnit au; au.nals=split(data); au.pts_ms=pts;
        for(const auto& n:au.nals) au.random_access|=(uint8_t(n[0])&31)==5;
        admission->ready=false;
        auto wire_frames=frames(au);
        for(auto& frame:wire_frames) { auto owned=std::dynamic_pointer_cast<H264Frame>(frame); owned->_pts=owned->_dts=au.pts_ms+47720800; }
        check(input_encoder.inputSignedAccessUnit(wire_frames,au.random_access),"input packetization failed");
        check(admission->ready && admission->result.nals==au.nals && admission->result.pts_ms==au.pts_ms,"AU admission changed/dropped fixture or wrap timing");
        if(have) process(pending,false);
        pending=std::move(au); have=true;
    }
    check(have && manifest.eof(),"fixture manifest invalid"); process(pending,true);
    rejects([&] { signer.sign(pending,true); });
    Config bad=c; bad.key_file="missing-test-key"; rejects([&] { Signer missing(bad); });
    Config bounded=c; bounded.max_au_bytes=1; Signer bound(bounded); rejects([&] { bound.sign(pending); }); rejects([&] { bound.sign(pending); });
    check(added>0 && bool(signed_file) && bool(received),"signing/output missing");
    std::cout<<"PASS official signer original_nals="<<total_nals<<" added_seis="<<added<<" aus="<<aus<<" EOS=once\n";
}
int main(int argc,char** argv) {
    try { units(); if(argc==6) fixture(argv); else check(argc==1,"usage: test [ANNEXB MANIFEST KEY CHAIN OUTPUT_DIR]"); }
    catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n'; return 1; }
}
