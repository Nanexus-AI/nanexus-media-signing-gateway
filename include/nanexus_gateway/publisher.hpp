// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "media.hpp"
#include "ext-codec/H264Rtp.h"
#include "Network/TcpServer.h"
#include "Rtsp/RtspMediaSource.h"
namespace nanexus {
// Producer owns source independently of RTSP readers. All methods use owner poller.
class Publisher {
public:
    Publisher(const Config&,const toolkit::EventPoller::Ptr&,const AccessUnit& first);
    void publish(const AccessUnit&);
private:
    Config config_;
    toolkit::TcpServer::Ptr server_;
    std::shared_ptr<mediakit::MediaSourceEvent> owner_;
    mediakit::RtspMediaSource::Ptr source_;
    mediakit::H264RtpEncoder encoder_;
};
}
