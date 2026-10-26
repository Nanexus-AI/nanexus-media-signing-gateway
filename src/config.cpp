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
namespace {
using Values = std::map<std::string,std::string>;
bool shared_key(const std::string& k) {
    return k=="signing.key_file" || k=="signing.certificate_file" ||
           k=="limits.startup_timeout_ms" || k=="limits.gap_timeout_ms" ||
           k=="limits.drain_timeout_ms" || k=="limits.max_signing_frames" ||
           k=="limits.max_nal_bytes" || k=="limits.max_au_bytes" || k=="limits.max_nals";
}
bool channel_key(const std::string& k) {
    return k=="camera.url" || k=="camera.auth_file" || k=="output.url";
}
bool channel_id(const std::string& s) {
    return s=="cam1" || s=="cam2" || s=="cam3" || s=="cam4";
}
struct Section { Values values; const char* error=nullptr; };
Config from_values(Values values) {
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

std::vector<Config> load_configs(std::istream& input,const std::string& selected) {
    require(selected.empty() || channel_id(selected),"invalid channel selector");
    std::map<std::string,Section> sections;
    std::set<std::string> declared;
    std::string section,line; bool sectioned=false;
    std::size_t total=0;
    auto parse_line=[&]() {
        auto s=trim(line); line.clear();
        if(s.empty() || s[0]=='#') return;
        if(s[0]=='[') {
            require(s.back()==']',"invalid configuration section");
            auto name=s.substr(1,s.size()-2);
            require(name=="gateway" || (name.compare(0,8,"channel.")==0 && channel_id(name.substr(8))),"unknown configuration section");
            require(sections[""].values.empty() && !sections[""].error,"cannot mix legacy keys and sections");
            require(declared.insert(name).second,"duplicate configuration section");
            section=name; sectioned=true; sections[section]; return;
        }
        auto& data=sections[section];
        auto bad=[&](const char* error) { if(!data.error) data.error=error; };
        auto pos=s.find('=');
        if(pos==std::string::npos) { bad("expected key=value"); return; }
        auto key=trim(s.substr(0,pos)),value=trim(s.substr(pos+1));
        bool valid=section.empty() ? shared_key(key)||channel_key(key) :
                   section=="gateway" ? shared_key(key) : channel_key(key);
        if(!valid) { bad("unknown configuration key"); return; }
        if(value.empty()) { bad("empty configuration value"); return; }
        if(!data.values.emplace(key,value).second) bad("duplicate configuration key");
    };
    char ch;
    while(input.get(ch)) {
        require(++total<=16384,"configuration exceeds 16 KiB");
        require(ch!='\0',"NUL in configuration");
        if(ch!='\n') { require(line.size()<2048,"configuration line exceeds 2048 bytes"); line+=ch; }
        else parse_line();
    }
    require(!input.bad(),"configuration read failed"); parse_line();
    if(!sectioned) {
        require(selected.empty(),"channel selector requires sectioned configuration");
        auto& legacy=sections[""]; if(legacy.error) throw std::runtime_error(legacy.error);
        return {from_values(legacy.values)};
    }
    require(declared.count("gateway"),"missing gateway section");
    auto& common=sections["gateway"];
    if(common.error) throw std::runtime_error(common.error);
    // Validate all common values even for a selected worker.
    auto base=common.values;
    base["camera.url"]="rtsp://camera.invalid:554/input";
    base["output.url"]="rtsp://127.0.0.1:8554/signed/input";
    from_values(base);
    require(selected.empty() || declared.count("channel."+selected),"selected channel not configured");
    std::vector<Config> result;
    for(const auto& name:declared) {
        if(name=="gateway" || (!selected.empty() && name!="channel."+selected)) continue;
        auto& data=sections[name]; if(data.error) throw std::runtime_error(data.error);
        auto values=common.values;
        values.insert(data.values.begin(),data.values.end());
        auto config=from_values(values); config.channel_id=name.substr(8);
        result.emplace_back(std::move(config));
    }
    require(!result.empty(),"no channels configured");
    // A distinct port per process avoids wildcard/hostname alias bind conflicts.
    const std::regex output(R"(^rtsp://[A-Za-z0-9][A-Za-z0-9.-]*:([0-9]{1,5})/[A-Za-z0-9_./-]+$)");
    for(const auto& c:result) {
        auto colon=c.output_url.find(':',7), slash=c.output_url.find('/',colon);
        auto port=std::stoul(c.output_url.substr(colon+1,slash-colon-1));
        for(const auto& name:declared) {
            if(name=="gateway" || name=="channel."+c.channel_id) continue;
            auto it=sections[name].values.find("output.url"); std::smatch match;
            if(it!=sections[name].values.end() && std::regex_match(it->second,match,output))
                require(std::stoul(match[1].str())!=port,"channels require distinct output ports");
        }
    }
    return result;
}
Config load_config(std::istream& input,const std::string& selected) {
    auto configs=load_configs(input,selected);
    require(configs.size()==1,"multiple channels require --channel selector");
    return configs.front();
}
}
