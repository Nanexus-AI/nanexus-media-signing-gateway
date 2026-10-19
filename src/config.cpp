// SPDX-License-Identifier: Apache-2.0
#include "nanexus_gateway/config.hpp"
#include <map>
#include <regex>
#include <set>
#include <stdexcept>
namespace nanexus {
namespace {
std::string trim(const std::string& s) {
    auto a=s.find_first_not_of(" \t\r");
    return a==std::string::npos ? "" : s.substr(a,s.find_last_not_of(" \t\r")-a+1);
}
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
}
Config load_config(std::istream& input) {
    std::map<std::string,std::string> values;
    const std::set<std::string> keys={"camera.url", "camera.auth_file", "output.url", "signing.key_file", "signing.certificate_file", "limits.startup_timeout_ms", "limits.gap_timeout_ms", "limits.drain_timeout_ms", "limits.max_signing_frames", "limits.max_nal_bytes", "limits.max_au_bytes", "limits.max_nals"};
    std::string line; std::size_t total=0;
    // Read before growing: even one unterminated hostile line has a fixed bound.
    auto parse_line=[&]() {
        auto s=trim(line); line.clear();
        if(s.empty() || s[0]=='#') return;
        auto pos=s.find('='); require(pos!=std::string::npos,"expected key=value");
        auto key=trim(s.substr(0,pos)), value=trim(s.substr(pos+1));
        require(!key.empty() && keys.count(key)!=0,"unknown configuration key");
        require(!value.empty(),"empty configuration value");
        require(values.emplace(key,value).second,"duplicate configuration key");
    };
    char ch;
    while (input.get(ch)) {
        require(++total<=16384,"configuration exceeds 16 KiB");
        if (ch!='\n') { require(line.size()<2048,"configuration line exceeds 2048 bytes"); line+=ch; continue; }
        parse_line();
    }
    require(!input.bad(),"configuration read failed");
    parse_line();
    Config c;
    auto required=[&](const char* key) { require(values.count(key)!=0,"missing required configuration field"); return values.at(key); };
    c.camera_url=required("camera.url"); c.output_url=required("output.url");
    c.key_file=required("signing.key_file"); c.certificate_file=required("signing.certificate_file");
    c.camera_auth_file=values["camera.auth_file"];
    // Deliberately narrow syntax: DNS/IPv4, explicit port, app/stream path.
    const std::regex url(R"(^rtsp://[A-Za-z0-9][A-Za-z0-9.-]*:[0-9]{1,5}/[A-Za-z0-9_./-]+$)");
    for(const auto* u:{&c.camera_url,&c.output_url}) {
        require(std::regex_match(*u,url),"invalid RTSP endpoint; credentials, query and IPv6 unsupported");
        auto colon=u->find(':',7), slash=u->find('/',colon);
        auto port=std::stoul(u->substr(colon+1,slash-colon-1));
        require(port>0 && port<=65535,"RTSP port outside 1..65535");
    }
    auto path=c.output_url.find('/',7);
    auto separator=c.output_url.find('/',path+1);
    require(separator!=std::string::npos && separator>path+1 && separator+1<c.output_url.size(),"output requires app/stream path");
    auto number=[&](const char* key,std::size_t fallback,std::size_t low,std::size_t high) {
        if(!values.count(key)) return fallback;
        const auto& v=values.at(key); require(v.size()<=10 && v.find_first_not_of("0123456789")==std::string::npos,"invalid unsigned operational limit");
        auto n=std::stoull(v); require(n>=low && n<=high,"operational limit outside supported range"); return std::size_t(n);
    };
    c.startup_timeout_ms=number("limits.startup_timeout_ms",10000,1,60000);
    c.gap_timeout_ms=number("limits.gap_timeout_ms",2000,1,60000);
    c.drain_timeout_ms=number("limits.drain_timeout_ms",1000,1,10000);
    c.max_signing_frames=number("limits.max_signing_frames",15,1,300);
    c.max_nal_bytes=number("limits.max_nal_bytes",1048576,1,8388608);
    c.max_au_bytes=number("limits.max_au_bytes",8388608,1,8388608);
    c.max_nals=number("limits.max_nals",4096,1,4096);
    require(c.max_nal_bytes<=c.max_au_bytes,"NAL limit exceeds AU limit");
    for(const auto* p:{&c.key_file,&c.certificate_file,&c.camera_auth_file})
        require(p->find_first_of("\t\r\n") == std::string::npos && p->find(char(0)) == std::string::npos,"invalid file reference");
    return c;
}
}
