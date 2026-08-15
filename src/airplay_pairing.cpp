#include "airplay2/airplay_pairing.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

#ifdef GWL_AIRPLAY2_HAS_OPENSSL
#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#endif

namespace gwl::airplay2 {

#ifndef GWL_AIRPLAY2_HAS_OPENSSL
struct AirPlayTransientPairing::Impl {};
AirPlayTransientPairing::AirPlayTransientPairing() : impl_(new Impl) {}
AirPlayTransientPairing::~AirPlayTransientPairing() { delete impl_; }
void AirPlayTransientPairing::reset() { complete_ = false; shared_secret_.clear(); }
bool AirPlayTransientPairing::handle(const std::vector<std::uint8_t>&, std::vector<std::uint8_t>& response, std::string& error) { response.clear(); error = "no crypto backend configured"; return false; }
#else
namespace {
struct Tlv { std::uint8_t type; std::vector<std::uint8_t> value; };
std::vector<Tlv> decode_tlv(const std::vector<std::uint8_t>& input) { std::vector<Tlv> out; std::size_t p=0; while(p+2<=input.size()){auto t=input[p++]; auto n=input[p++]; if(p+n>input.size()) return {}; out.push_back({t,{input.begin()+p,input.begin()+p+n}}); p+=n;} return p==input.size()?out:std::vector<Tlv>{}; }
const std::vector<std::uint8_t>* find_tlv(const std::vector<Tlv>& ts,std::uint8_t type){for(const auto&t:ts)if(t.type==type)return&t.value;return nullptr;}
void add_tlv(std::vector<std::uint8_t>& out,std::uint8_t type,const std::vector<std::uint8_t>& value){std::size_t off=0;do{auto n=std::min<std::size_t>(255,value.size()-off);out.push_back(type);out.push_back((std::uint8_t)n);out.insert(out.end(),value.begin()+off,value.begin()+off+n);off+=n;}while(off<value.size());}
std::vector<std::uint8_t> one(std::uint8_t v){return{v};}
const std::array<unsigned char,384> kN=[] { const char* h="FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E08A67CC74020BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7EDEE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3DC2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F83655D23DCA3AD961C62F356208552BB9ED529077096966D670C354E4ABC9804F1746C08CA18217C32905E462E36CE3BE39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9DE2BCBF6955817183995497CEA956AE515D2261898FA3A8A0AACAA68FFFFFFFFFFFFFFFF"; std::array<unsigned char,384>b{};auto n=[](char c)->unsigned char{return c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c-'a'+10;};for(size_t i=0;i<b.size();++i)b[i]=(n(h[i*2])<<4)|n(h[i*2+1]);return b;}();
std::vector<std::uint8_t> sha512(const std::vector<std::uint8_t>& d){std::vector<std::uint8_t>o(SHA512_DIGEST_LENGTH);SHA512(d.data(),d.size(),o.data());return o;}
std::vector<std::uint8_t> concat(std::initializer_list<std::vector<std::uint8_t>> ps){std::vector<std::uint8_t>o;for(auto&p:ps)o.insert(o.end(),p.begin(),p.end());return o;}
std::vector<std::uint8_t> bn_pad(const BIGNUM*b){std::vector<std::uint8_t>o(kN.size());BN_bn2binpad(b,o.data(),(int)o.size());return o;}
bool eq(const std::vector<std::uint8_t>&a,const std::vector<std::uint8_t>&b){return a.size()==b.size()&&CRYPTO_memcmp(a.data(),b.data(),a.size())==0;}
} // namespace
struct AirPlayTransientPairing::Impl { BIGNUM*N=nullptr,*g=nullptr,*b=nullptr,*v=nullptr,*B=nullptr;std::vector<std::uint8_t>salt,A,B_bytes;~Impl(){BN_free(N);BN_free(g);BN_clear_free(b);BN_clear_free(v);BN_free(B);}void reset(){BN_free(N);BN_free(g);BN_clear_free(b);BN_clear_free(v);BN_free(B);N=g=b=v=B=nullptr;salt.clear();A.clear();B_bytes.clear();}};
AirPlayTransientPairing::AirPlayTransientPairing():impl_(new Impl){} AirPlayTransientPairing::~AirPlayTransientPairing(){delete impl_;}
void AirPlayTransientPairing::reset(){impl_->reset();shared_secret_.clear();complete_=false;}
bool AirPlayTransientPairing::handle(const std::vector<std::uint8_t>&request,std::vector<std::uint8_t>&response,std::string&error){
 response.clear();error.clear();
 // HKP pair-setup is handled by the receiver's HKP path. The transient SRP
 // handler only accepts actual TLV8 frames, never the 32-byte HKP public key.
 const auto tlvs=decode_tlv(request); if(tlvs.empty()){error="invalid TLV8";return false;}
 const auto*state=find_tlv(tlvs,0x06); if(!state||state->empty()){error="missing pairing state";return false;}
 BN_CTX*ctx=BN_CTX_new();if(!ctx){error="BN context allocation failed";return false;}
 if(state->at(0)==1){const auto*m=find_tlv(tlvs,0x00);const auto*f=find_tlv(tlvs,0x13);bool transient=f&&!f->empty()&&((f->at(0)&0x10)!=0);if(!m||m->empty()||m->at(0)!=0||!transient){response={0x06,1,2,0x07,1,6};error="only transient pair-setup is supported";BN_CTX_free(ctx);return true;}reset();impl_->N=BN_bin2bn(kN.data(),(int)kN.size(),nullptr);impl_->g=BN_new();impl_->b=BN_new();impl_->v=BN_new();impl_->B=BN_new();if(!impl_->N||!impl_->g||!impl_->b||!impl_->v||!impl_->B){BN_CTX_free(ctx);error="SRP allocation failed";return false;}BN_set_word(impl_->g,5);impl_->salt.resize(16);if(RAND_bytes(impl_->salt.data(),16)!=1){BN_CTX_free(ctx);error="secure random generation failed";return false;}auto up=std::vector<uint8_t>{'P','a','i','r','-','S','e','t','u','p',':','3','9','3','9'};auto inner=sha512(up);auto xb=sha512(concat({impl_->salt,inner}));BIGNUM*x=BN_bin2bn(xb.data(),(int)xb.size(),nullptr),*vtmp=BN_new(),*gb=BN_new(),*kbn=BN_new();if(!x||!vtmp||!gb||!kbn){BN_free(x);BN_free(vtmp);BN_free(gb);BN_free(kbn);BN_CTX_free(ctx);error="SRP temporary allocation failed";return false;}std::vector<uint8_t>gp(kN.size(),0);gp.back()=5;auto kh=sha512(concat({std::vector<uint8_t>(kN.begin(),kN.end()),gp}));BN_mod_exp(vtmp,impl_->g,x,impl_->N,ctx);BN_copy(impl_->v,vtmp);BN_free(x);BN_free(vtmp);BN_bin2bn(kh.data(),(int)kh.size(),kbn);std::array<unsigned char,256>raw{};if(RAND_bytes(raw.data(),256)!=1){BN_free(gb);BN_free(kbn);BN_CTX_free(ctx);error="secure random generation failed";return false;}BN_bin2bn(raw.data(),256,impl_->b);BN_mod_exp(gb,impl_->g,impl_->b,impl_->N,ctx);BIGNUM*kv=BN_new();BN_mod_mul(kv,kbn,impl_->v,impl_->N,ctx);BN_mod_add(impl_->B,kv,gb,impl_->N,ctx);BN_free(kv);BN_free(gb);BN_free(kbn);impl_->B_bytes=bn_pad(impl_->B);BN_CTX_free(ctx);add_tlv(response,6,one(2));add_tlv(response,2,impl_->salt);add_tlv(response,3,impl_->B_bytes);add_tlv(response,0x13,one(0x10));return true;}
 if(state->at(0)==3&&impl_->B&&impl_->v){const auto*pub=find_tlv(tlvs,3),*proof=find_tlv(tlvs,4);if(!pub||!proof||pub->size()!=kN.size()||proof->size()!=SHA512_DIGEST_LENGTH){BN_CTX_free(ctx);error="invalid SRP M3";return false;}impl_->A=*pub;BIGNUM*A=BN_bin2bn(pub->data(),(int)pub->size(),nullptr),*u=BN_new(),*vu=BN_new(),*base=BN_new(),*S=BN_new();if(!A||!u||!vu||!base||!S){BN_free(A);BN_free(u);BN_free(vu);BN_free(base);BN_free(S);BN_CTX_free(ctx);error="SRP temporary allocation failed";return false;}if(BN_is_zero(A)||BN_cmp(A,impl_->N)>=0){BN_free(A);BN_free(u);BN_free(vu);BN_free(base);BN_free(S);BN_CTX_free(ctx);response={6,1,4,7,1,2};error="invalid SRP public key";return true;}auto uh=sha512(concat({bn_pad(A),impl_->B_bytes}));BN_bin2bn(uh.data(),(int)uh.size(),u);BN_mod_exp(vu,impl_->v,u,impl_->N,ctx);BN_mod_mul(base,A,vu,impl_->N,ctx);BN_mod_exp(S,base,impl_->b,impl_->N,ctx);auto K=sha512(bn_pad(S));shared_secret_=K;std::vector<uint8_t>xng(64);auto hn=sha512(std::vector<uint8_t>(kN.begin(),kN.end()));std::vector<uint8_t>gp(kN.size(),0);gp.back()=5;auto hg=sha512(gp);for(size_t i=0;i<xng.size();++i)xng[i]=hn[i]^hg[i];auto hi=sha512(std::vector<uint8_t>{'P','a','i','r','-','S','e','t','u','p'});auto M1=sha512(concat({xng,hi,impl_->salt,bn_pad(A),impl_->B_bytes,K}));if(!eq(M1,*proof)){BN_free(A);BN_free(u);BN_free(vu);BN_free(base);BN_free(S);BN_CTX_free(ctx);response={6,1,4,7,1,2};error="SRP client proof verification failed";return true;}auto M2=sha512(concat({bn_pad(A),M1,K}));add_tlv(response,6,one(4));add_tlv(response,4,M2);complete_=true;BN_free(A);BN_free(u);BN_free(vu);BN_free(base);BN_free(S);BN_CTX_free(ctx);return true;}
 BN_CTX_free(ctx);error="unexpected pair-setup state";return false;}
#endif
} // namespace gwl::airplay2
