// SPDX-License-Identifier: Apache-2.0
#include "nanexus_gateway/signer.hpp"
#include "onvif_media_signing_signer.h"
#include <openssl/crypto.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
namespace nanexus {
namespace {
void need(bool b,const char* s) { if(!b) throw std::runtime_error(s); }
void check(MediaSigningReturnCode r) { need(r==OMS_OK,"official signing API failed"); }
struct Secret {
    std::string bytes;
    ~Secret() { if(!bytes.empty()) OPENSSL_cleanse(&bytes[0],bytes.size()); }
};
void load(const std::string& path,std::string& bytes) {
    std::ifstream in(path,std::ios::binary); need(bool(in),"identity open failed");
    char c;
    while(in.get(c)) { need(bytes.size()<65536,"identity exceeds 64 KiB"); bytes+=c; }
    need(!in.bad() && !bytes.empty(),"identity read failed");
}
}
Signer::Signer(const Config& c):config_(c),context_(onvif_media_signing_create(OMS_CODEC_H264),onvif_media_signing_free) {
    need(bool(context_),"signing context creation failed");
    Secret key; std::string chain; load(c.key_file,key.bytes); load(c.certificate_file,chain);
    check(onvif_media_signing_set_emulation_prevention_before_signing(context_.get(),true));
    check(onvif_media_signing_set_signing_key_pair(context_.get(),key.bytes.data(),key.bytes.size(),chain.data(),chain.size(),false));
    check(onvif_media_signing_set_max_signing_frames(context_.get(),c.max_signing_frames));
}
Signer::~Signer()=default;
AccessUnit Signer::sign(const AccessUnit& input,bool final) {
    need(!ended_ && !failed_,"signing epoch is closed");
    try {
        need(!input.nals.empty() && input.nals.size()<=config_.max_nals,"signing AU count bound");
        AccessUnit out; out.pts_ms=input.pts_ms; out.random_access=input.random_access;
        size_t bytes=0;
        auto append=[&](const char* p,size_t n) {
            need(n && n<=config_.max_nal_bytes && out.nals.size()<config_.max_nals,"signed NAL bound");
            need(n+4<=config_.max_au_bytes-bytes,"signed AU bound"); bytes+=n+4; out.nals.emplace_back(p,n);
        };
        // Reserve all original bytes before any signing mutation; inserted SEIs use remainder.
        size_t originals=0;
        for(const auto& n:input.nals) { need(!n.empty() && n.size()<=config_.max_nal_bytes && n.size()+4<=config_.max_au_bytes-originals,"original AU bound"); originals+=n.size()+4; }
        auto utc=(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count()+11644473600000000LL)*10;
        auto drain=[&](const uint8_t* peek,size_t size,bool eos) {
            for(unsigned i=0;i<64;++i) {
                uint8_t* raw=nullptr; size_t n=0;
                auto rc=onvif_media_signing_get_sei(context_.get(),&raw,&n,nullptr,peek,size,nullptr);
                std::unique_ptr<uint8_t,decltype(&std::free)> owned(raw,&std::free); check(rc);
                if(!n) return;
                need(raw && n>4 && n<=config_.max_nal_bytes+4 && raw[0]==0 && raw[1]==0 && raw[2]==0 && raw[3]==1,"generated SEI framing/bound");
                need(n<=config_.max_au_bytes-bytes && n<=config_.max_au_bytes-originals,"generated SEI aggregate bound");
                originals+=n;
                if(!eos) check(onvif_media_signing_add_nalu_for_signing(context_.get(),raw,n,utc));
                append(reinterpret_cast<char*>(raw+4),n-4);
            }
            throw std::runtime_error("generated SEI count bound");
        };
        for(const auto& nal:input.nals) {
            // Official parser supports raw NALs; framing here also protects short NAL reads.
            std::string framed("\0\0\0\1",4); framed+=nal;
            drain(reinterpret_cast<const uint8_t*>(framed.data()),framed.size(),false);
            check(onvif_media_signing_add_nalu_for_signing(context_.get(),reinterpret_cast<const uint8_t*>(framed.data()),framed.size(),utc));
            append(nal.data(),nal.size());
        }
        if(final) { check(onvif_media_signing_set_end_of_stream(context_.get())); drain(nullptr,0,true); ended_=true; }
        return out;
    } catch(...) { failed_=true; throw; }
}
}
