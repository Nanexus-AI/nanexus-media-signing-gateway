// SPDX-License-Identifier: Apache-2.0
#include "nanexus_gateway/config.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace nanexus;
namespace {
void check(bool b) { if(!b) throw std::runtime_error("four-channel assertion failed"); }
const std::string shared="[gateway]\nsigning.key_file=secrets/key\nsigning.certificate_file=secrets/chain\nlimits.max_signing_frames=15\n";
std::string channel(unsigned n) {
    auto id=std::to_string(n);
    return "[channel.cam"+id+"]\ncamera.url=rtsp://camera"+id+".invalid:554/video\noutput.url=rtsp://127.0.0.1:"+std::to_string(18553+n)+"/signed/camera"+id+"\n";
}
auto parse(const std::string& text,const std::string& selected="") -> std::vector<Config> {
    std::istringstream in(text); return load_configs(in,selected);
}
void rejects(const std::string& text,const std::string& selected="") {
    bool failed=false; try { parse(text,selected); } catch(const std::exception&) { failed=true; } check(failed);
}
}
int main() {
    try {
        auto text=shared;
        for(unsigned i=1;i<=4;++i) text+=channel(i);
        auto configs=parse(text); check(configs.size()==4);
        for(unsigned i=0;i<4;++i) {
            auto selected=parse(text,"cam"+std::to_string(i+1)); check(selected.size()==1);
            check(selected[0].channel_id==configs[i].channel_id);
            check(selected[0].key_file==configs[0].key_file && selected[0].certificate_file==configs[0].certificate_file);
        }
        // Invalid unrelated channel fields must not poison a healthy worker.
        for(const auto& fault:{"camera.url=duplicate\n","unknown=bad\n","output.url=\n","not-a-field\n"}) {
            auto broken=shared+channel(1)+channel(2)+fault;
            rejects(broken); rejects(broken,"cam2"); check(parse(broken,"cam1").size()==1);
        }
        auto badurl=channel(2); badurl.replace(badurl.find("rtsp://camera2.invalid:554/video"),30,"not-a-url");
        rejects(shared+channel(1)+badurl); check(parse(shared+channel(1)+badurl,"cam1").size()==1);
        rejects(text,"cam5"); rejects(shared+channel(1),"cam2");
        rejects(shared); rejects(channel(1)); rejects(text+channel(1));
        rejects("camera.url=x\n"+text); rejects(shared+"camera.url=x\n"+channel(1));
        rejects(shared+"limits.max_nals=0\n"+channel(1),"cam1");
        auto conflict=channel(2); conflict.replace(conflict.find(":18555/"),8,":18554/");
        rejects(shared+channel(1)+conflict); rejects(shared+channel(1)+conflict,"cam1");
        rejects(shared+channel(1)+"[channel.cam5]\n");
        rejects(text+std::string(17000,'x')); rejects(text+std::string(2049,'x')+"\n");
        rejects(text+std::string(1,'\0'));
        auto one=parse(shared+channel(3)); check(one.size()==1 && one[0].channel_id=="cam3");
        std::istringstream full(text); bool missing_selector=false;
        try { load_config(full); } catch(...) { missing_selector=true; } check(missing_selector);
        std::cout<<"PASS four-channel shared identity, selected validation and port isolation\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
