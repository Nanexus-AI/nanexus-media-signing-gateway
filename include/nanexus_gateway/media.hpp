// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "config.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace nanexus {
// NAL payloads exclude Annex-B framing. Ownership stays within one serialized epoch.
struct AccessUnit {
    std::vector<std::string> nals;
    uint64_t pts_ms=0;
    bool random_access=false;
};
struct RtpView {
    const uint8_t* payload=nullptr;
    size_t size=0;
    uint32_t stamp=0, ssrc=0;
    uint16_t sequence=0;
    uint8_t pt=0;
    bool marker=false;
};
RtpView parse_rtp(const uint8_t*, size_t);
// Checks before upstream decoder allocates; NAL callbacks are collected afterwards.
class AuAdmission {
public:
    explicit AuAdmission(const Config& c):config_(c) {}
    bool before(const RtpView&); // false while discarding initial partial group
    void nal(const char*,size_t);
    bool complete(AccessUnit&); // only after a marker packet
private:
    Config config_;
    AccessUnit current_;
    bool boundary_=false, group_=false, fu_=false, started_=false, marker_=false;
    uint8_t fu_identity_=0;
    size_t bytes_=0, fu_bytes_=0, expected_=0, decoded_bytes_=0;
    uint32_t stamp_=0, last_stamp_=0;
    uint64_t ticks_=0;
};
}
