// SPDX-License-Identifier: Apache-2.0
#include "nanexus_gateway/media.hpp"
#include <stdexcept>
#include <utility>
namespace nanexus {
namespace {
void need(bool b,const char* s) { if(!b) throw std::runtime_error(s); }
uint16_t u16(const uint8_t* p) { return uint16_t(p[0])<<8 | p[1]; }
uint32_t u32(const uint8_t* p) { return uint32_t(u16(p))<<16 | u16(p+2); }
class Bits {
    std::string rbsp_; size_t pos_=0;
public:
    explicit Bits(const std::string& n) {
        unsigned zeros=0;
        for(size_t i=1;i<n.size() && rbsp_.size()<4096;++i) {
            auto b=uint8_t(n[i]);
            if(zeros>=2 && b==3) { zeros=0; continue; }
            rbsp_+=char(b); zeros=b==0?zeros+1:0;
        }
    }
    unsigned bits(unsigned n) {
        need(n<=32 && pos_+n<=rbsp_.size()*8,"truncated H264 header");
        unsigned v=0; while(n--) { v=(v<<1)|((uint8_t(rbsp_[pos_/8])>>(7-pos_%8))&1); ++pos_; } return v;
    }
    unsigned ue() {
        unsigned n=0; while(!bits(1)) need(++n<25,"H264 header bound");
        return ((1u<<n)-1)+bits(n);
    }
    int se() { auto n=ue(); return n&1 ? int((n+1)/2) : -int(n/2); }
};
void progressive_sps(const std::string& n) {
    Bits b(n); auto profile=b.bits(8); b.bits(16); b.ue();
    if(profile==100 || profile==110 || profile==122 || profile==244 || profile==44 || profile==83 || profile==86 || profile==118 || profile==128 || profile==138 || profile==139 || profile==134 || profile==135) {
        auto chroma=b.ue(); need(chroma<=3,"unsupported chroma format");
        if(chroma==3) need(!b.bits(1),"separate colour planes unsupported");
        b.ue(); b.ue(); b.bits(1);
        if(b.bits(1)) for(unsigned i=0;i<(chroma==3?12u:8u);++i) if(b.bits(1)) {
            int last=8,next=8;
            for(unsigned j=0;j<(i<6?16u:64u);++j) { if(next) next=(last+b.se()+256)%256; last=next?next:last; }
        }
    } else need(profile==66 || profile==77 || profile==88,"unsupported H264 profile");
    b.ue(); auto poc=b.ue(); need(poc<=2,"unsupported picture order");
    if(poc==0) b.ue();
    if(poc==1) { b.bits(1); b.se(); b.se(); auto count=b.ue(); need(count<=255,"picture order bound"); while(count--) b.se(); }
    b.ue(); b.bits(1); b.ue(); b.ue();
    need(b.bits(1)==1,"interlaced H264 unsupported");
}
}
RtpView parse_rtp(const uint8_t* p,size_t n) {
    need(n>=12 && n<=65535 && (p[0]>>6)==2,"invalid RTP header");
    size_t offset=12+4*(p[0]&15); need(offset<=n,"invalid RTP CSRC");
    if(p[0]&16) { need(n-offset>=4,"invalid RTP extension"); auto size=4+size_t(u16(p+offset+2))*4; need(size<=n-offset,"invalid RTP extension length"); offset+=size; }
    if(p[0]&32) { auto pad=p[n-1]; need(pad && pad<=n-offset,"invalid RTP padding"); n-=pad; }
    need(offset<n,"empty RTP payload");
    RtpView v; v.payload=p+offset; v.size=n-offset; v.stamp=u32(p+4); v.ssrc=u32(p+8); v.sequence=u16(p+2); v.pt=p[1]&127; v.marker=p[1]&128; return v;
}
bool AuAdmission::before(const RtpView& p) {
    if(!boundary_) { if(p.marker) boundary_=true; return false; }
    if(!group_) { current_=AccessUnit(); bytes_=decoded_bytes_=expected_=fu_bytes_=0; stamp_=p.stamp; group_=true; }
    need(p.stamp==stamp_,"timestamp changed before complete AU"); marker_=p.marker;
    need(p.size>0 && p.size<=config_.max_au_bytes-bytes_,"input AU byte bound"); bytes_+=p.size;
    const auto* d=p.payload; auto type=d[0]&31;
    need(!(d[0]&128),"H264 forbidden bit");
    auto single=[&](const uint8_t* ptr,size_t size) {
        need(size && !(ptr[0]&128) && (ptr[0]&31)>=1 && (ptr[0]&31)<=23,"invalid NAL");
        need(size<=config_.max_nal_bytes && expected_<config_.max_nals,"NAL bound"); ++expected_;
    };
    if(type==28) {
        need(p.size>=3 && !(d[1]&32) && (d[1]&31)>=1 && (d[1]&31)<=23,"invalid FU header");
        bool first=d[1]&128,last=d[1]&64; need(!(first&&last),"invalid FU flags");
        uint8_t identity=(d[0]&0xe0)|(d[1]&31);
        if(first) { need(!fu_,"nested FU"); fu_=true; fu_identity_=identity; fu_bytes_=1; need(expected_<config_.max_nals,"NAL count bound"); }
        else need(fu_ && fu_identity_==identity,"FU identity discontinuity");
        need(p.size-2<=config_.max_nal_bytes-fu_bytes_,"fragmented NAL bound"); fu_bytes_+=p.size-2;
        if(last) { fu_=false; ++expected_; }
    } else if(type==24) {
        need(!fu_,"STAP interrupts FU"); size_t pos=1;
        while(pos<p.size) { need(p.size-pos>=2,"truncated STAP length"); auto size=u16(d+pos); pos+=2; need(size<=p.size-pos,"truncated STAP NAL"); single(d+pos,size); pos+=size; }
        need(pos>1,"empty STAP");
    } else { need(!fu_,"NAL interrupts FU"); single(d,p.size); }
    if(marker_) need(!fu_,"incomplete FU at AU marker");
    return true;
}
void AuAdmission::nal(const char* p,size_t n) {
    need(n && n<=config_.max_nal_bytes && current_.nals.size()<config_.max_nals,"decoded NAL bound");
    need(n<=config_.max_au_bytes-decoded_bytes_,"decoded AU bound"); decoded_bytes_+=n;
    current_.nals.emplace_back(p,n);
}
bool AuAdmission::complete(AccessUnit& out) {
    if(!marker_) return false;
    need(group_ && !fu_ && expected_==current_.nals.size(),"missing decoded NAL"); group_=false;
    bool sps=false,pps=false; unsigned first=0,vcl=0;
    for(const auto& n:current_.nals) {
        auto type=uint8_t(n[0])&31;
        if(type==7) { progressive_sps(n); sps=true; }
        else if(type==8) pps=true;
        else if(type==1 || type==5) {
            Bits b(n); auto mb=b.ue(),slice=b.ue(); need(slice<=9 && (slice%5==0 || slice%5==2),"only I/P slices supported");
            if(mb==0) ++first; else need(first==1,"slice before picture start");
            ++vcl; current_.random_access |= type==5;
        } else need(type==6 || type==9 || type==12,"unsupported H264 NAL type");
    }
    need(vcl && first==1,"AU must contain one picture start");
    if(!started_) { if(!(sps&&pps&&current_.random_access)) return false; started_=true; last_stamp_=stamp_; }
    else { auto delta=uint32_t(stamp_-last_stamp_); need(delta>0 && delta<=90000 && (ticks_+delta)/90>ticks_/90,"RTP timestamp discontinuity"); ticks_+=delta; last_stamp_=stamp_; }
    current_.pts_ms=1000+ticks_/90;
    out=std::move(current_); return true;
}
}
