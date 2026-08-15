#include "airplay2/airplay_pairing.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

#ifdef GWL_AIRPLAY2_HAS_OPENSSL
#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#endif

namespace gwl::airplay2 {

#ifndef GWL_AIRPLAY2_HAS_OPENSSL

struct AirPlayTransientPairing::Impl {};
AirPlayTransientPairing::AirPlayTransientPairing() : impl_(new Impl) {}
AirPlayTransientPairing::~AirPlayTransientPairing() { delete impl_; }
void AirPlayTransientPairing::reset() { complete_ = false; shared_secret_.clear(); }
bool AirPlayTransientPairing::handle(const std::vector<std::uint8_t>&,
                                     std::vector<std::uint8_t>& response,
                                     std::string& error) {
    response.clear();
    error = "no portable SHA-512/BN crypto backend is configured for this target";
    return false;
}

#else

namespace {
struct Tlv { std::uint8_t type; std::vector<std::uint8_t> value; };

std::vector<Tlv> decode_tlv(const std::vector<std::uint8_t>& input) {
    std::vector<Tlv> out;
    std::size_t p = 0;
    while (p + 2 <= input.size()) {
        const auto type = input[p++];
        const auto len = input[p++];
        if (p + len > input.size()) return {};
        out.push_back({type, {input.begin() + p, input.begin() + p + len}});
        p += len;
    }
    return p == input.size() ? out : std::vector<Tlv>{};
}

const std::vector<std::uint8_t>* find_tlv(const std::vector<Tlv>& tlvs, std::uint8_t type) {
    for (const auto& t : tlvs) if (t.type == type) return &t.value;
    return nullptr;
}

void add_tlv(std::vector<std::uint8_t>& out, std::uint8_t type, const std::vector<std::uint8_t>& value) {
    std::size_t offset = 0;
    do {
        const auto n = std::min<std::size_t>(255, value.size() - offset);
        out.push_back(type);
        out.push_back(static_cast<std::uint8_t>(n));
        out.insert(out.end(), value.begin() + offset, value.begin() + offset + n);
        offset += n;
    } while (offset < value.size());
}

std::vector<std::uint8_t> one_byte(std::uint8_t v) { return {v}; }

const std::array<unsigned char, 384> kN = [] {
    const char* hex =
        "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E08"
        "8A67CC74020BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B"
        "302B0A6DF25F14374FE1356D6D51C245E485B576625E7EC6F44C42E9"
        "A637ED6B0BFF5CB6F406B7EDEE386BFB5A899FA5AE9F24117C4B1FE6"
        "49286651ECE45B3DC2007CB8A163BF0598DA48361C55D39A69163FA8"
        "FD24CF5F83655D23DCA3AD961C62F356208552BB9ED529077096966D"
        "670C354E4ABC9804F1746C08CA18217C32905E462E36CE3BE39E772C"
        "180E86039B2783A2EC07A28FB5C55DF06F4C52C9DE2BCBF695581718"
        "3995497CEA956AE515D2261898FA3A8A0AACAA68FFFFFFFFFFFFFFFF";
    std::array<unsigned char, 384> bytes{};
    auto nibble = [](char c) -> unsigned char {
        if (c >= '0' && c <= '9') return static_cast<unsigned char>(c - '0');
        if (c >= 'A' && c <= 'F') return static_cast<unsigned char>(c - 'A' + 10);
        return static_cast<unsigned char>(c - 'a' + 10);
    };
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<unsigned char>((nibble(hex[i * 2]) << 4) | nibble(hex[i * 2 + 1]));
    return bytes;
}();

std::vector<std::uint8_t> sha512(const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> out(SHA512_DIGEST_LENGTH);
    SHA512(data.data(), data.size(), out.data());
    return out;
}

std::vector<std::uint8_t> concat(std::initializer_list<std::vector<std::uint8_t>> parts) {
    std::vector<std::uint8_t> out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

std::vector<std::uint8_t> bn_pad(const BIGNUM* bn) {
    std::vector<std::uint8_t> out(kN.size());
    BN_bn2binpad(bn, out.data(), static_cast<int>(out.size()));
    return out;
}

bool equal_bytes(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    return a.size() == b.size() && CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

} // namespace

struct AirPlayTransientPairing::Impl {
    BIGNUM* N = nullptr;
    BIGNUM* g = nullptr;
    BIGNUM* b = nullptr;
    BIGNUM* v = nullptr;
    BIGNUM* B = nullptr;
    std::vector<std::uint8_t> salt;
    std::vector<std::uint8_t> A;
    std::vector<std::uint8_t> B_bytes;

    ~Impl() {
        BN_free(N); BN_free(g); BN_clear_free(b); BN_clear_free(v); BN_free(B);
    }
    void reset() {
        BN_free(N); BN_free(g); BN_clear_free(b); BN_clear_free(v); BN_free(B);
        N = g = b = v = B = nullptr;
        salt.clear(); A.clear(); B_bytes.clear();
    }
};

AirPlayTransientPairing::AirPlayTransientPairing() : impl_(new Impl) {}
AirPlayTransientPairing::~AirPlayTransientPairing() { delete impl_; }

void AirPlayTransientPairing::reset() {
    impl_->reset();
    shared_secret_.clear();
    complete_ = false;
}

bool AirPlayTransientPairing::handle(const std::vector<std::uint8_t>& request,
                                     std::vector<std::uint8_t>& response,
                                     std::string& error) {
    response.clear();
    error.clear();
    const auto tlvs = decode_tlv(request);
    if (tlvs.empty()) { error = "invalid TLV8"; return false; }
    const auto* state = find_tlv(tlvs, 0x06);
    if (!state || state->empty()) { error = "missing pairing state"; return false; }

    BN_CTX* ctx = BN_CTX_new();
    if (!ctx) { error = "BN context allocation failed"; return false; }

    if (state->at(0) == 1) {
        const auto* method = find_tlv(tlvs, 0x00);
        const auto* flags = find_tlv(tlvs, 0x13);
        const bool transient = flags && !flags->empty() && ((flags->at(0) & 0x10) != 0);
        if (!method || method->empty() || method->at(0) != 0 || !transient) {
            response = {0x06, 0x01, 0x02, 0x07, 0x01, 0x06};
            error = "only transient pair-setup is supported";
            BN_CTX_free(ctx);
            return true;
        }

        reset();
        impl_->N = BN_bin2bn(kN.data(), static_cast<int>(kN.size()), nullptr);
        impl_->g = BN_new();
        impl_->b = BN_new();
        impl_->v = BN_new();
        impl_->B = BN_new();
        if (!impl_->N || !impl_->g || !impl_->b || !impl_->v || !impl_->B) {
            BN_CTX_free(ctx); error = "SRP allocation failed"; return false;
        }
        BN_set_word(impl_->g, 5);
        impl_->salt.resize(16);
        if (RAND_bytes(impl_->salt.data(), static_cast<int>(impl_->salt.size())) != 1) {
            BN_CTX_free(ctx); error = "secure random generation failed"; return false;
        }

        // HAP transient pairing for screenless AirPlay receivers uses PIN 3939.
        const auto up = std::vector<std::uint8_t>{'P','a','i','r','-','S','e','t','u','p',':','3','9','3','9'};
        const auto inner = sha512(up);
        const auto x_bytes = sha512(concat({impl_->salt, inner}));
        BIGNUM* x = BN_bin2bn(x_bytes.data(), static_cast<int>(x_bytes.size()), nullptr);
        BIGNUM* vtmp = BN_new();
        BIGNUM* gb = BN_new();
        BIGNUM* kbn = BN_new();
        if (!x || !vtmp || !gb || !kbn) {
            BN_free(x); BN_free(vtmp); BN_free(gb); BN_free(kbn); BN_CTX_free(ctx);
            error = "SRP temporary allocation failed"; return false;
        }
        std::vector<std::uint8_t> gpad(kN.size(), 0); gpad.back() = 5;
        const auto k_hash = sha512(concat({std::vector<std::uint8_t>(kN.begin(), kN.end()), gpad}));
        BN_mod_exp(vtmp, impl_->g, x, impl_->N, ctx);
        BN_copy(impl_->v, vtmp);
        BN_free(x); BN_free(vtmp);
        BN_bin2bn(k_hash.data(), static_cast<int>(k_hash.size()), kbn);
        std::array<unsigned char, 256> braw{};
        if (RAND_bytes(braw.data(), static_cast<int>(braw.size())) != 1) {
            BN_free(gb); BN_free(kbn); BN_CTX_free(ctx); error = "secure random generation failed"; return false;
        }
        BN_bin2bn(braw.data(), static_cast<int>(braw.size()), impl_->b);
        BN_mod_exp(gb, impl_->g, impl_->b, impl_->N, ctx);
        BIGNUM* kv = BN_new();
        BN_mod_mul(kv, kbn, impl_->v, impl_->N, ctx);
        BN_mod_add(impl_->B, kv, gb, impl_->N, ctx);
        BN_free(kv); BN_free(gb); BN_free(kbn);
        impl_->B_bytes = bn_pad(impl_->B);
        BN_CTX_free(ctx);

        add_tlv(response, 0x06, one_byte(2));
        add_tlv(response, 0x02, impl_->salt);
        add_tlv(response, 0x03, impl_->B_bytes);
        add_tlv(response, 0x13, one_byte(0x10));
        return true;
    }

    if (state->at(0) == 3 && impl_->B && impl_->v) {
        const auto* pub = find_tlv(tlvs, 0x03);
        const auto* proof = find_tlv(tlvs, 0x04);
        if (!pub || !proof || pub->size() != kN.size() || proof->size() != SHA512_DIGEST_LENGTH) {
            BN_CTX_free(ctx); error = "invalid SRP M3"; return false;
        }
        impl_->A = *pub;
        BIGNUM* A = BN_bin2bn(pub->data(), static_cast<int>(pub->size()), nullptr);
        BIGNUM* u = BN_new();
        BIGNUM* vu = BN_new();
        BIGNUM* base = BN_new();
        BIGNUM* S = BN_new();
        if (!A || !u || !vu || !base || !S) {
            BN_free(A); BN_free(u); BN_free(vu); BN_free(base); BN_free(S); BN_CTX_free(ctx);
            error = "SRP temporary allocation failed"; return false;
        }
        if (BN_is_zero(A) || BN_cmp(A, impl_->N) >= 0) {
            BN_free(A); BN_free(u); BN_free(vu); BN_free(base); BN_free(S); BN_CTX_free(ctx);
            response = {0x06,0x01,0x04,0x07,0x01,0x02}; error = "invalid SRP public key"; return true;
        }
        const auto u_hash = sha512(concat({bn_pad(A), impl_->B_bytes}));
        BN_bin2bn(u_hash.data(), static_cast<int>(u_hash.size()), u);
        BN_mod_exp(vu, impl_->v, u, impl_->N, ctx);
        BN_mod_mul(base, A, vu, impl_->N, ctx);
        BN_mod_exp(S, base, impl_->b, impl_->N, ctx);
        const auto K = sha512(bn_pad(S));
        shared_secret_ = K;

        std::vector<std::uint8_t> xor_ng(64);
        const auto hn = sha512(std::vector<std::uint8_t>(kN.begin(), kN.end()));
        std::vector<std::uint8_t> gpad(kN.size(), 0); gpad.back() = 5;
        const auto hg = sha512(gpad);
        for (std::size_t i = 0; i < xor_ng.size(); ++i) xor_ng[i] = hn[i] ^ hg[i];
        const auto hi = sha512(std::vector<std::uint8_t>{'P','a','i','r','-','S','e','t','u','p'});
        const auto M1 = sha512(concat({xor_ng, hi, impl_->salt, bn_pad(A), impl_->B_bytes, K}));
        if (!equal_bytes(M1, *proof)) {
            BN_free(A); BN_free(u); BN_free(vu); BN_free(base); BN_free(S); BN_CTX_free(ctx);
            response = {0x06,0x01,0x04,0x07,0x01,0x02}; error = "SRP client proof verification failed"; return true;
        }
        const auto M2 = sha512(concat({bn_pad(A), M1, K}));
        add_tlv(response, 0x06, one_byte(4));
        add_tlv(response, 0x04, M2);
        complete_ = true;
        BN_free(A); BN_free(u); BN_free(vu); BN_free(base); BN_free(S); BN_CTX_free(ctx);
        return true;
    }

    BN_CTX_free(ctx);
    error = "unexpected pair-setup state";
    return false;
}

#endif

} // namespace gwl::airplay2
