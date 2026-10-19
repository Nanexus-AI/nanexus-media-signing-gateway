// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
#include <istream>
#include <string>
namespace nanexus {
struct Config {
    std::string camera_url, camera_auth_file, output_url, key_file, certificate_file;
    unsigned startup_timeout_ms = 10000, gap_timeout_ms = 2000, drain_timeout_ms = 1000;
    unsigned max_signing_frames = 15;
    std::size_t max_nal_bytes = 1048576, max_au_bytes = 8388608, max_nals = 4096;
};
// Values are never included in exceptions. Bounded input, strict keys, no interpolation.
Config load_config(std::istream& input);
}
