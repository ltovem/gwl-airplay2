#include "airplay2/airplay_hkp_pairing.h"

#include <array>
#include <cstring>
#include <memory>

#ifdef GWL_AIRPLAY2_HAS_OPENSSL
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#endif

namespace gwl::airplay2 {

#ifndef GWL_AIRPLAY2_HAS_OPENSSL
struct AirPlayHkpPairing::Impl {};
AirPlayHkpPairing::AirPlayHkpPairing() : impl_(new Impl) {}
AirPlayHkpPairing::~AirPlayHkpPairing() { delete impl_; }
bool AirPlayHkpPairing::handle_pair_setup(const std::vector<std::uint8_t>&, std::vector<std::uint8_t>& response, std::string& error) { response.clear(); error = "OpenSSL crypto backend is required for AirPlay HKP pairing"; return false; }
bool AirPlayHkpPairing::handle_pair_verify(const std::vector<std::uint8_t>&, std::vector<std::uint8_t>& response, std::string& error) { response.clear(); error = "OpenSSL crypto backend is required for AirPlay HKP pairing"; return false; }
#else
namespace {
std::vector<std::uint8_t> sha512(const std::string& prefix, const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> input(prefix.begin(), prefix.end());
    input.insert(input.end(), data.begin(), data.end());
    std::vector<std::uint8_t> out(SHA512_DIGEST_LENGTH);
    SHA512(input.data(), input.size(), out.data());
    return out;
}
bool raw_public(EVP_PKEY* key, std::vector<std::uint8_t>& out) {
    std::size_t len = 32;
    out.resize(len);
    if (EVP_PKEY_get_raw_public_key(key, out.data(), &len) != 1) return false;
    out.resize(len);
    return true;
}
bool random32(std::array<std::uint8_t, 32>& out) { return RAND_bytes(out.data(), static_cast<int>(out.size())) == 1; }
bool aes128_ctr(const std::vector<std::uint8_t>& input, const std::vector<std::uint8_t>& key, const std::vector<std::uint8_t>& iv, std::vector<std::uint8_t>& output, std::size_t discard_prefix = 0) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;
    std::vector<std::uint8_t> discard(discard_prefix, 0);
    output.resize(input.size() + AES_BLOCK_SIZE);
    int n1 = 0, n2 = 0, dummy = 0;
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr, key.data(), iv.data()) == 1;
    if (ok && discard_prefix) ok = EVP_EncryptUpdate(ctx, discard.data(), &dummy, discard.data(), static_cast<int>(discard.size())) == 1;
    if (ok) ok = EVP_EncryptUpdate(ctx, output.data(), &n1, input.data(), static_cast<int>(input.size())) == 1;
    if (ok) ok = EVP_EncryptFinal_ex(ctx, output.data() + n1, &n2) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) { output.clear(); return false; }
    output.resize(static_cast<std::size_t>(n1 + n2));
    return true;
}
bool sign_ed25519(EVP_PKEY* key, const std::vector<std::uint8_t>& message, std::vector<std::uint8_t>& signature) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return false;
    std::size_t len = 64;
    signature.resize(len);
    bool ok = EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, key) == 1 && EVP_DigestSign(ctx, signature.data(), &len, message.data(), message.size()) == 1;
    EVP_MD_CTX_free(ctx);
    if (!ok) { signature.clear(); return false; }
    signature.resize(len);
    return true;
}
bool verify_ed25519(EVP_PKEY* key, const std::vector<std::uint8_t>& message, const std::vector<std::uint8_t>& signature) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return false;
    bool ok = EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, key) == 1 && EVP_DigestVerify(ctx, signature.data(), signature.size(), message.data(), message.size()) == 1;
    EVP_MD_CTX_free(ctx);
    return ok;
}
bool x25519_shared(EVP_PKEY* private_key, const std::vector<std::uint8_t>& peer_public, std::vector<std::uint8_t>& shared) {
    EVP_PKEY* peer = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, peer_public.data(), peer_public.size());
    if (!peer) return false;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(private_key, nullptr);
    std::size_t len = 32;
    shared.resize(len);
    bool ok = ctx && EVP_PKEY_derive_init(ctx) == 1 && EVP_PKEY_derive_set_peer(ctx, peer) == 1 && EVP_PKEY_derive(ctx, shared.data(), &len) == 1;
    if (ctx) EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(peer);
    if (!ok) { shared.clear(); return false; }
    shared.resize(len);
    return true;
}
}

struct AirPlayHkpPairing::Impl {
    EVP_PKEY* ed25519 = nullptr;
    EVP_PKEY* x25519 = nullptr;
    std::vector<std::uint8_t> ed_public;
    std::vector<std::uint8_t> client_curve_public;
    std::vector<std::uint8_t> client_ed_public;
    std::vector<std::uint8_t> shared;
    ~Impl() { EVP_PKEY_free(ed25519); EVP_PKEY_free(x25519); }
};

AirPlayHkpPairing::AirPlayHkpPairing() : impl_(new Impl) {}
AirPlayHkpPairing::~AirPlayHkpPairing() { delete impl_; }

bool AirPlayHkpPairing::handle_pair_setup(const std::vector<std::uint8_t>& request, std::vector<std::uint8_t>& response, std::string& error) {
    response.clear(); error.clear();
    if (request.size() != 32) { error = "expected 32-byte HKP pair-setup request"; return false; }
    if (!impl_->ed25519) {
        impl_->ed25519 = EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519");
        if (!impl_->ed25519 || !raw_public(impl_->ed25519, impl_->ed_public)) { error = "failed to generate Ed25519 identity key"; return false; }
    }
    response = impl_->ed_public;
    return response.size() == 32;
}

bool AirPlayHkpPairing::handle_pair_verify(const std::vector<std::uint8_t>& request, std::vector<std::uint8_t>& response, std::string& error) {
    response.clear(); error.clear();
    if (request.size() != 68) { error = "expected 68-byte HKP pair-verify request"; return false; }
    const std::uint8_t state = request[0];
    if (state == 1) {
        impl_->client_curve_public.assign(request.begin() + 4, request.begin() + 36);
        impl_->client_ed_public.assign(request.begin() + 36, request.end());
        if (!impl_->ed25519) { error = "pair-setup identity is not established"; return false; }
        EVP_PKEY_free(impl_->x25519);
        impl_->x25519 = nullptr;
        std::array<std::uint8_t, 32> seed{};
        if (!random32(seed)) { error = "failed to generate Curve25519 key"; return false; }
        impl_->x25519 = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, seed.data(), seed.size());
        if (!impl_->x25519) { error = "failed to create Curve25519 key"; return false; }
        std::vector<std::uint8_t> server_curve;
        if (!raw_public(impl_->x25519, server_curve) || !x25519_shared(impl_->x25519, impl_->client_curve_public, impl_->shared)) { error = "Curve25519 key agreement failed"; return false; }

        // AirPlay HKP signs ServerCurve || ClientCurve with the long-lived
        // Ed25519 identity generated by /pair-setup.
        std::vector<std::uint8_t> signed_message = server_curve;
        signed_message.insert(signed_message.end(), impl_->client_curve_public.begin(), impl_->client_curve_public.end());
        std::vector<std::uint8_t> signature;
        if (!sign_ed25519(impl_->ed25519, signed_message, signature)) { error = "Ed25519 signing failed"; return false; }

        // AirPlay's HKP pair-verify uses SHA-512(prefix || shared) rather
        // than HKDF. Unlike the pair-setup AES derivation, the verify IV is
        // NOT incremented before initializing the CTR stream.
        const auto key_hash = sha512("Pair-Verify-AES-Key", impl_->shared);
        const auto iv_hash = sha512("Pair-Verify-AES-IV", impl_->shared);
        std::vector<std::uint8_t> key(key_hash.begin(), key_hash.begin() + 16);
        std::vector<std::uint8_t> iv(iv_hash.begin(), iv_hash.begin() + 16);

        std::vector<std::uint8_t> encrypted;
        // The first 32 bytes of the CTR stream are consumed by the client's
        // challenge; only the following 64 bytes are returned.
        if (!aes128_ctr(signature, key, iv, encrypted, 32)) { error = "AES-CTR encryption failed"; return false; }
        response = server_curve;
        response.insert(response.end(), encrypted.begin(), encrypted.end());
        return response.size() == 96;
    }

    if (state == 0) {
        if (!impl_->shared.empty() && impl_->client_ed_public.size() == 32) {
            const std::vector<std::uint8_t> encrypted(request.begin() + 4, request.end());
            const auto key_hash = sha512("Pair-Verify-AES-Key", impl_->shared);
            const auto iv_hash = sha512("Pair-Verify-AES-IV", impl_->shared);
            std::vector<std::uint8_t> key(key_hash.begin(), key_hash.begin() + 16);
            std::vector<std::uint8_t> iv(iv_hash.begin(), iv_hash.begin() + 16);
            std::vector<std::uint8_t> signature;
            if (!aes128_ctr(encrypted, key, iv, signature, 32) || signature.size() != 64) { error = "AES-CTR decryption failed"; return false; }
            std::vector<std::uint8_t> server_curve;
            if (!raw_public(impl_->x25519, server_curve)) { error = "server Curve25519 key unavailable"; return false; }
            std::vector<std::uint8_t> signed_message = impl_->client_curve_public;
            signed_message.insert(signed_message.end(), server_curve.begin(), server_curve.end());
            EVP_PKEY* client_ed = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, impl_->client_ed_public.data(), 32);
            if (!client_ed) { error = "invalid client Ed25519 key"; return false; }
            const bool ok = verify_ed25519(client_ed, signed_message, signature);
            EVP_PKEY_free(client_ed);
            if (!ok) { error = "client Ed25519 signature verification failed"; return false; }
            verified_ = true;
            return true;
        }
        error = "pair-verify state is not established";
        return false;
    }
    error = "unsupported HKP pair-verify state";
    return false;
}
#endif
} // namespace gwl::airplay2
