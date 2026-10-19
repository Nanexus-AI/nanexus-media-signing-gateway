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
    if(argc<3 || (std::string(argv[1])!="--check-config" && std::string(argv[1])!="--config") || (argc!=3 && argc!=5)) {
        std::cerr<<"usage: nanexus-gateway --check-config FILE | --config FILE [--duration SECONDS]\n"; return 2;
    }
    try {
        std::ifstream in(argv[2]); if(!in) throw std::runtime_error("cannot open configuration");
        auto c=nanexus::load_config(in);
        if(std::string(argv[1])=="--check-config") { if(argc!=3) return 2; std::cout<<"configuration=valid media_observed=0\n"; return 0; }
        unsigned seconds=0;
        if(argc==5) {
            std::string n=argv[4];
            if(std::string(argv[3])!="--duration" || n.empty() || n.size()>3 || n.find_first_not_of("0123456789")!=std::string::npos) throw std::runtime_error("invalid duration (1..600 seconds)");
            seconds=unsigned(std::stoul(n)); if(!seconds || seconds>600) throw std::runtime_error("invalid duration (1..600 seconds)");
        }
#ifdef NANEXUS_NATIVE
        std::signal(SIGINT,request_stop); std::signal(SIGTERM,request_stop);
        try { return nanexus::run_gateway(c,seconds,stop_requested); }
        catch(...) { std::cerr<<"state=failed reason=runtime_initialization_failed\n"; return 1; }
#else
        (void)c; (void)seconds; std::cerr<<"state=failed error=runtime_adapters_unavailable (build with NANEXUS_NATIVE=ON)\n"; return 2;
#endif
    } catch(const std::exception& e) { std::cerr<<"configuration=invalid reason="<<e.what()<<'\n'; return 1; }
}
