// SPDX-License-Identifier: Apache-2.0
#include "nanexus_gateway/config.hpp"
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <vector>
using namespace nanexus;
void check(bool b) { if(!b) throw std::runtime_error("test assertion failed"); }
const std::string base="camera.url=rtsp://camera.example.invalid:554/stream\noutput.url=rtsp://127.0.0.1:8554/signed/camera\nsigning.key_file=secrets/key\nsigning.certificate_file=secrets/chain\n";
Config parse(std::string s) { std::istringstream in(s); return load_config(in); }
void rejects(std::string s) { bool failed=false; try { parse(s); } catch(const std::exception&) { failed=true; } check(failed); }
int main() {
 try {
    auto c=parse(base); check(c.max_au_bytes==8388608 && c.max_signing_frames==15);
    check(parse(base.substr(0,base.size()-1)).key_file=="secrets/key");
    rejects(base+"camera.url camera.auth_file=value\n");
    rejects(base+"limits.max_nals=\n");
    rejects(""); rejects(base+"unknown=value\n"); rejects(base+"camera.url=x\n");
    rejects(base+"limits.max_nal_bytes=8388609\n"); rejects(base+"limits.max_nals=-1\n");
    rejects(base+"limits.max_au_bytes=10\n"); rejects(base+"limits.gap_timeout_ms=0\n");
    rejects(base+std::string(17000,'a')); rejects(base+"limits.max_nals=99999999999999999999\n");
    auto secret=base; secret.replace(secret.find("camera.example.invalid"),21,"user:SECRET@camera.invalid"); rejects(secret);
    auto port=base; port.replace(port.find(":554/"),5,":65536/"); rejects(port);
    auto output=base; output.replace(output.find("/signed/camera"),14,"/camera"); rejects(output);
    std::cout<<"PASS configuration contracts\n";
 } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
