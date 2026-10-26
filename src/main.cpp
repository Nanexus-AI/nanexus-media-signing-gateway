// SPDX-License-Identifier: Apache-2.0
#include "nanexus_gateway/config.hpp"
#ifdef NANEXUS_NATIVE
#include "nanexus_gateway/runtime.hpp"
#endif
#include <csignal>
#include <atomic>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
static_assert(ATOMIC_BOOL_LOCK_FREE==2,"signal stop flag must be lock-free");
std::atomic<bool> stop_requested{false};
void request_stop(int) { stop_requested.store(true,std::memory_order_relaxed); }
}
int main(int argc,char** argv) {
    if(argc<3 || (std::string(argv[1])!="--check-config" && std::string(argv[1])!="--config") || argc%2==0) {
        std::cerr<<"usage: nanexus-gateway --check-config FILE [--channel camN] | --config FILE [--channel camN] [--duration SECONDS]\n"; return 2;
    }
    std::string channel;
    try {
        unsigned seconds=0;
        for(int i=3;i<argc;i+=2) {
            std::string option=argv[i],value=argv[i+1];
            if(option=="--channel" && channel.empty()) {
                if(value!="cam1" && value!="cam2" && value!="cam3" && value!="cam4") throw std::runtime_error("invalid channel selector");
                channel=value;
            } else if(option=="--duration" && !seconds && std::string(argv[1])=="--config") {
                if(value.empty() || value.size()>3 || value.find_first_not_of("0123456789")!=std::string::npos) throw std::runtime_error("invalid duration (1..600 seconds)");
                seconds=unsigned(std::stoul(value)); if(!seconds || seconds>600) throw std::runtime_error("invalid duration (1..600 seconds)");
            } else throw std::runtime_error("unknown or duplicate command option");
        }
        std::ifstream in(argv[2]); if(!in) throw std::runtime_error("cannot open configuration");
        if(std::string(argv[1])=="--check-config") {
            auto configs=nanexus::load_configs(in,channel);
            if(!channel.empty()) std::cout<<"channel="<<channel<<' ';
            std::cout<<"configuration=valid media_observed=0 channels="<<configs.size()<<'\n'; return 0;
        }
        auto c=nanexus::load_config(in,channel);
#ifdef NANEXUS_NATIVE
        std::signal(SIGINT,request_stop); std::signal(SIGTERM,request_stop);
        try { return nanexus::run_gateway(c,seconds,stop_requested); }
        catch(...) { std::cerr<<"channel="<<(c.channel_id.empty()?"single":c.channel_id)<<" state=failed reason=runtime_initialization_failed\n"; return 1; }
#else
        (void)c; (void)seconds; std::cerr<<"state=failed error=runtime_adapters_unavailable (build with NANEXUS_NATIVE=ON)\n"; return 2;
#endif
    } catch(const std::exception& e) {
        if(!channel.empty()) std::cerr<<"channel="<<channel<<' ';
        std::cerr<<"configuration=invalid reason="<<e.what()<<'\n'; return 1;
    }
}
