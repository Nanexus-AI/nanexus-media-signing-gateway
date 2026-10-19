// SPDX-License-Identifier: Apache-2.0
#include "nanexus_gateway/publisher.hpp"
#include "Rtsp/RtspSession.h"
#include "Common/config.h"
#include <stdexcept>
namespace nanexus {
namespace {
struct Owner final:mediakit::MediaSourceEvent {
    toolkit::EventPoller::Ptr poller;
    explicit Owner(toolkit::EventPoller::Ptr p):poller(std::move(p)) {}
    toolkit::EventPoller::Ptr getOwnerPoller(mediakit::MediaSource&) override { return poller; }
    int totalReaderCount(mediakit::MediaSource& s) override { return s.readerCount(); }
    void onReaderChanged(mediakit::MediaSource&,int) override {} // No reader-driven close/timer.
};
}
Publisher::Publisher(const Config& c,const toolkit::EventPoller::Ptr& p,const AccessUnit& first):config_(c) {
    using namespace mediakit;
    toolkit::mINI::Instance()[Rtsp::kLowLatency]=1;
    auto colon=c.output_url.find(':',7), slash=c.output_url.find('/',colon);
    auto host=c.output_url.substr(7,colon-7);
    auto port=std::stoul(c.output_url.substr(colon+1,slash-colon-1));
    MediaInfo tuple(c.output_url);
    if(tuple.app.empty() || tuple.stream.empty()) throw std::runtime_error("output needs app/stream path");
    std::string sps,pps;
    for(const auto& n:first.nals) { if((uint8_t(n[0])&31)==7) sps=n; if((uint8_t(n[0])&31)==8) pps=n; }
    if(sps.empty() || pps.empty()) throw std::runtime_error("publication requires in-band SPS/PPS");
    owner_=std::make_shared<Owner>(p);
    source_=std::make_shared<RtspMediaSource>(tuple,512);
    source_->setListener(owner_);
    Track::Ptr metadata=std::make_shared<H264Track>(sps,pps,0,0); // SDP only; signed media never enters a Track.
    source_->setSdp(TitleSdp().getSdp()+metadata->getSdp(96)->getSdp()+"a=control:trackID=0\r\n");
    auto ring=std::make_shared<RtpRing::RingType>(); ring->setDelegate(source_);
    encoder_.setRtpInfo(0,1200,90000,96,0,0); encoder_.setRtpRing(ring);
    server_=std::make_shared<toolkit::TcpServer>(p);
    server_->start<RtspSession>(uint16_t(port),host,4);
}
void Publisher::publish(const AccessUnit& au) {
    std::vector<mediakit::Frame::Ptr> frames;
    size_t bytes=0;
    if(au.nals.empty() || au.nals.size()>config_.max_nals) throw std::runtime_error("output AU count bound");
    for(const auto& n:au.nals) {
        if(n.empty() || n.size()>config_.max_nal_bytes || n.size()+4>config_.max_au_bytes-bytes) throw std::runtime_error("output AU byte bound");
        bytes+=n.size()+4;
    }
    frames.reserve(au.nals.size());
    for(const auto& n:au.nals) {
        auto f=mediakit::FrameImp::create<mediakit::H264Frame>();
        f->_buffer.assign("\0\0\0\1",4); f->_buffer.append(n.data(),n.size());
        f->_prefix_size=4; f->_pts=f->_dts=au.pts_ms;
        frames.emplace_back(std::move(f));
    }
    if(!encoder_.inputSignedAccessUnit(frames,au.random_access)) throw std::runtime_error("signed AU encoder rejected output");
}
}
