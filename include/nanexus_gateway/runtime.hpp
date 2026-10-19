// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "config.hpp"
#include <atomic>
namespace nanexus {
// Blocks main thread; media callbacks and cleanup remain serialized on one poller.
int run_gateway(const Config&,unsigned duration_seconds,const std::atomic<bool>& stop);
}
