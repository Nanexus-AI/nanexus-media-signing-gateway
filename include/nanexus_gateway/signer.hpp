// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "media.hpp"
#include "onvif_media_signing_common.h"
#include <memory>
namespace nanexus {
class Signer {
public:
    explicit Signer(const Config&);
    ~Signer();
    Signer(const Signer&)=delete;
    Signer& operator=(const Signer&)=delete;
    AccessUnit sign(const AccessUnit&,bool final=false);
private:
    Config config_;
    std::unique_ptr<onvif_media_signing_t, decltype(&onvif_media_signing_free)> context_;
    bool ended_=false, failed_=false;
};
}
