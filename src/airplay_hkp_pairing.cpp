#include "airplay2/airplay_hkp_pairing.h"

#include <array>
#include <cstring>
#include <memory>

#ifdef GWL_AIRPLAY2_HAS_OPENSSL
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#endif

namespace gwl::airplay2 {
#ifndef GWL_AIRPLAY2_HAS_OPENSSL
struct AirPlayHkpPairing::Impl {};
AirPlayHkpPairing::AirPlayHkpPairing():impl_(new Impl){} AirPlayHkpPairing::~AirPlayHkpPairing(){delete impl_;}
bool AirPlayHkpPairing::handle_pair_setup(const std::vector<std::uint8_t>&,std::vector<std::uint8_t>&r,std::string&e){r.clear();e="OpenSSL crypto backend is required for AirPlay HKP pairing";return false;}
bool AirPlayHkpPairing::handle_pair_verify(const std::vector<std::uint8_t>&,std::vector<std::uint8_t>&r,std::string&e){r.clear();e="OpenSSL crypto backend is required for AirPlay HKP pairing";return false;}
#else
namespace {
std::vector<std::uint8_t> sha512(const std::string&p,const std::vector<std::uint8_t>&d){std::vector<std::uint8_t>i(p.begin(),p.end());i.insert(i.end(),d.begin(),d.end());std::vector<std::uint8_t>o(SHA512_DIGEST_LENGTH);SHA512(i.data(),i.size(),o.data());return o;}
bool raw_public(EVP_PKEY*k,std::vector<std::uint8_t>&o){size_t n=32;o.resize(n);if(EVP_PKEY_get_raw_public_key(k,o.data(),&n)!=1)return false;o.resize(n);return true;}
bool random32(std::array<std::uint8_t,32>&o){return RAND_bytes(o.data(),32)==1;}
bool aes_ctr(const std::vector<std::uint8_t>&in,const std::vector<std::uint8_t>&key,const std::vector<std::uint8_t>&iv,std::vector<std::uint8_t>&out){EVP_CIPHER_CTX*c=EVP_CIPHER_CTX_new();if(!c)return false;out.resize(in.size()+16);int n1=0,n2=0;bool ok=EVP_EncryptInit_ex(c,EVP_aes_128_ctr(),nullptr,key.data(),iv.data())==1&&EVP_EncryptUpdate(c,out.data(),&n1,in.data(),(int)in.size())==1&&EVP_EncryptFinal_ex(c,out.data()+n1,&n2)==1;EVP_CIPHER_CTX_free(c);if(!ok){out.clear();return false;}out.resize(n1+n2);return true;}
bool sign(EVP_PKEY*k,const std::vector<std::uint8_t>&m,std::vector<std::uint8_t>&s){EVP_MD_CTX*c=EVP_MD_CTX_new();if(!c)return false;size_t n=64;s.resize(64);bool ok=EVP_DigestSignInit(c,nullptr,nullptr,nullptr,k)==1&&EVP_DigestSign(c,s.data(),&n,m.data(),m.size())==1;EVP_MD_CTX_free(c);if(!ok)s.clear();else s.resize(n);return ok;}
bool verify(EVP_PKEY*k,const std::vector<std::uint8_t>&m,const std::vector<std::uint8_t>&s){EVP_MD_CTX*c=EVP_MD_CTX_new();if(!c)return false;bool ok=EVP_DigestVerifyInit(c,nullptr,nullptr,nullptr,k)==1&&EVP_DigestVerify(c,s.data(),s.size(),m.data(),m.size())==1;EVP_MD_CTX_free(c);return ok;}
bool xshared(EVP_PKEY*priv,const std::vector<std::uint8_t>&pub,std::vector<std::uint8_t>&out){EVP_PKEY*p=EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519,nullptr,pub.data(),pub.size());if(!p)return false;EVP_PKEY_CTX*c=EVP_PKEY_CTX_new(priv,nullptr);size_t n=32;out.resize(32);bool ok=c&&EVP_PKEY_derive_init(c)==1&&EVP_PKEY_derive_set_peer(c,p)==1&&EVP_PKEY_derive(c,out.data(),&n)==1;if(c)EVP_PKEY_CTX_free(c);EVP_PKEY_free(p);if(!ok)out.clear();else out.resize(n);return ok;}
}
struct AirPlayHkpPairing::Impl{EVP_PKEY*ed=nullptr;EVP_PKEY*x=nullptr;std::vector<std::uint8_t>edpub,clientcurve,cliented,shared;~Impl(){EVP_PKEY_free(ed);EVP_PKEY_free(x);}};
AirPlayHkpPairing::AirPlayHkpPairing():impl_(new Impl){} AirPlayHkpPairing::~AirPlayHkpPairing(){delete impl_;}
bool AirPlayHkpPairing::handle_pair_setup(const std::vector<std::uint8_t>&req,std::vector<std::uint8_t>&res,std::string&e){res.clear();e.clear();if(req.size()!=32){e="expected 32-byte HKP pair-setup request";return false;}if(!impl_->ed){impl_->ed=EVP_PKEY_Q_keygen(nullptr,nullptr,"ED25519");if(!impl_->ed||!raw_public(impl_->ed,impl_->edpub)){e="failed to generate Ed25519 identity key";return false;}}res=impl_->edpub;return true;}
bool AirPlayHkpPairing::handle_pair_verify(const std::vector<std::uint8_t>&req,std::vector<std::uint8_t>&res,std::string&e){res.clear();e.clear();if(req.size()!=68){e="expected 68-byte HKP pair-verify request";return false;}uint8_t state=req[0];
if(state==1){impl_->clientcurve.assign(req.begin()+4,req.begin()+36);impl_->cliented.assign(req.begin()+36,req.end());if(!impl_->ed){e="pair-setup identity is not established";return false;}EVP_PKEY_free(impl_->x);impl_->x=nullptr;std::array<uint8_t,32>seed{};if(!random32(seed)){e="random failed";return false;}impl_->x=EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519,nullptr,seed.data(),32);if(!impl_->x){e="x25519 key failed";return false;}std::vector<uint8_t>server;if(!raw_public(impl_->x,server)||!xshared(impl_->x,impl_->clientcurve,impl_->shared)){e="x25519 agreement failed";return false;}
std::vector<uint8_t>msg=impl_->clientcurve;msg.insert(msg.end(),server.begin(),server.end());std::vector<uint8_t>sig;if(!sign(impl_->ed,msg,sig)){e="Ed25519 signing failed";return false;}auto kh=sha512("Pair-Verify-AES-Key",impl_->shared),ih=sha512("Pair-Verify-AES-IV",impl_->shared);std::vector<uint8_t>key(kh.begin(),kh.begin()+16),iv(ih.begin(),ih.begin()+16),enc;if(!aes_ctr(sig,key,iv,enc)){e="AES encryption failed";return false;}res=server;res.insert(res.end(),enc.begin(),enc.end());return res.size()==96;}
if(state==0){if(impl_->shared.empty()||impl_->cliented.size()!=32){e="pair-verify state is not established";return false;}std::vector<uint8_t>enc(req.begin()+4,req.end());auto kh=sha512("Pair-Verify-AES-Key",impl_->shared),ih=sha512("Pair-Verify-AES-IV",impl_->shared);std::vector<uint8_t>key(kh.begin(),kh.begin()+16),iv(ih.begin(),ih.begin()+16),sig;if(!aes_ctr(enc,key,iv,sig)||sig.size()!=64){e="AES decryption failed";return false;}std::vector<uint8_t>server;if(!raw_public(impl_->x,server)){e="server key unavailable";return false;}std::vector<uint8_t>msg=impl_->clientcurve;msg.insert(msg.end(),server.begin(),server.end());EVP_PKEY*cp=EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519,nullptr,impl_->cliented.data(),32);if(!cp){e="invalid client Ed25519 key";return false;}bool ok=verify(cp,msg,sig);EVP_PKEY_free(cp);if(!ok){e="client Ed25519 signature verification failed";return false;}verified_=true;return true;}e="unsupported HKP pair-verify state";return false;}
#endif
}