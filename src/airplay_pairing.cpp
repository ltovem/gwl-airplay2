#include "airplay2/airplay_pairing.h"

#include <algorithm>
#include <array>
#include <cstring>

#include <openssl/srp.h>
#include <openssl/rand.h>

namespace gwl::airplay2 {
namespace {

struct Tlv {
    std::uint8_t type;
    std::vector<std::uint8_t> value;
};

std::vector<Tlv> decode_tlv(const std::vector<std::uint8_t>& input) {
    std::vector<Tlv> out;
    std::size_t p = 0;
    while (p + 2 <= input.size()) {
        const auto type = input[p++];
        const auto len = input[p++];
        if (p + len > input.size()) return {};
        out.push_back({type, std::vector<std::uint8_t>(input.begin() + p, input.begin() + p + len)});
        p += len;
    }
    return p == input.size() ? out : std::vector<Tlv>{};
}

const std::vector<std::uint8_t>* find_tlv(const std::vector<Tlv>& tlvs, std::uint8_t type) {
    for (const auto& t : tlvs) if (t.type == type) return &t.value;
    return nullptr;
}

void add_tlv(std::vector<std::uint8_t>& out, std::uint8_t type, const std::vector<std::uint8_t>& value) {
    // HAP TLV8 fragments values larger than 255 bytes into adjacent records.
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

class SrpHolder {
public:
    SRP* srp = nullptr;
    SRP* create() {
        destroy();
        srp = SRP_new(SRP6a_server_method());
        return srp;
    }
    void destroy() {
        if (srp) SRP_free(srp);
        srp = nullptr;
    }
    ~SrpHolder() { destroy(); }
};

// RFC 5054 3072-bit group, required by HAP.
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
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        auto nibble = [](char c) -> unsigned char {
            if (c >= '0' && c <= '9') return static_cast<unsigned char>(c - '0');
            if (c >= 'A' && c <= 'F') return static_cast<unsigned char>(c - 'A' + 10);
            return static_cast<unsigned char>(c - 'a' + 10);
        };
        bytes[i] = static_cast<unsigned char>((nibble(hex[i * 2]) << 4) | nibble(hex[i * 2 + 1]));
    }
    return bytes;
}();

} // namespace

struct AirPlayTransientPairing::Impl {
    SrpHolder srp;
    std::vector<std::uint8_t> salt;
    std::vector<std::uint8_t> server_key;
    bool m2_sent = false;
    bool m4_sent = false;
};

AirPlayTransientPairing::AirPlayTransientPairing() : impl_(new Impl) {}
AirPlayTransientPairing::~AirPlayTransientPairing() { delete impl_; }

void AirPlayTransientPairing::reset() {
    impl_->srp.destroy();
    impl_->salt.clear();
    impl_->server_key.clear();
    shared_secret_.clear();
    complete_ = false;
    impl_->m2_sent = false;
    impl_->m4_sent = false;
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

    if (state->at(0) == 1) {
        const auto* method = find_tlv(tlvs, 0x00);
        const auto* flags = find_tlv(tlvs, 0x13);
        const bool transient = flags && !flags->empty() && ((flags->at(0) & 0x10) != 0);
        if (!method || method->empty() || method->at(0) != 0 || !transient) {
            response = {0x06, 0x01, 0x02, 0x07, 0x01, 0x06};
            error = "only transient pair-setup is supported";
            return true;
        }

        reset();
        impl_->srp.create();
        if (!impl_->srp.srp) { error = "SRP allocation failed"; return false; }
        SRP_set_username(impl_->srp.srp, "Pair-Setup");
        impl_->salt.resize(16);
        if (RAND_bytes(impl_->salt.data(), static_cast<int>(impl_->salt.size())) != 1) {
            error = "secure random generation failed"; return false;
        }
        const unsigned char g = 5;
        if (SRP_set_params(impl_->srp.srp, kN.data(), static_cast<int>(kN.size()), &g, 1,
                           impl_->salt.data(), static_cast<int>(impl_->salt.size())) != SRP_SUCCESS) {
            error = "SRP parameters rejected"; return false;
        }
        // Screenless AirPlay receivers use transient pairing with setup code 3939.
        if (SRP_set_auth_password(impl_->srp.srp, "3939") != SRP_SUCCESS) {
            error = "SRP password setup failed"; return false;
        }
        cstr* pub = nullptr;
        if (SRP_gen_pub(impl_->srp.srp, &pub) != SRP_SUCCESS || !pub) {
            if (pub) cstr_free(pub);
            error = "SRP public key generation failed"; return false;
        }
        impl_->server_key.assign(reinterpret_cast<std::uint8_t*>(pub->data),
                                 reinterpret_cast<std::uint8_t*>(pub->data) + pub->length);
        cstr_free(pub);

        add_tlv(response, 0x06, one_byte(2));
        add_tlv(response, 0x02, impl_->salt);
        add_tlv(response, 0x03, impl_->server_key);
        add_tlv(response, 0x13, one_byte(0x10));
        impl_->m2_sent = true;
        return true;
    }

    if (state->at(0) == 3 && impl_->m2_sent) {
        const auto* pub = find_tlv(tlvs, 0x03);
        const auto* proof = find_tlv(tlvs, 0x04);
        if (!pub || !proof || pub->empty() || proof->empty()) {
            error = "missing SRP public key or proof"; return false;
        }
        cstr* key = nullptr;
        if (SRP_compute_key(impl_->srp.srp, &key, pub->data(), static_cast<int>(pub->size())) != SRP_SUCCESS || !key) {
            if (key) cstr_free(key);
            error = "SRP shared secret calculation failed"; return false;
        }
        shared_secret_.assign(reinterpret_cast<std::uint8_t*>(key->data),
                              reinterpret_cast<std::uint8_t*>(key->data) + key->length);
        cstr_free(key);

        if (SRP_verify(impl_->srp.srp, proof->data(), static_cast<int>(proof->size())) != SRP_SUCCESS) {
            response = {0x06, 0x01, 0x04, 0x07, 0x01, 0x02};
            error = "SRP client proof verification failed";
            return true;
        }
        cstr* server_proof = nullptr;
        if (SRP_respond(impl_->srp.srp, &server_proof) != SRP_SUCCESS || !server_proof) {
            if (server_proof) cstr_free(server_proof);
            error = "SRP server proof generation failed"; return false;
        }
        std::vector<std::uint8_t> proof_bytes(reinterpret_cast<std::uint8_t*>(server_proof->data),
                                               reinterpret_cast<std::uint8_t*>(server_proof->data) + server_proof->length);
        cstr_free(server_proof);
        add_tlv(response, 0x06, one_byte(4));
        add_tlv(response, 0x04, proof_bytes);
        complete_ = true;
        impl_->m4_sent = true;
        return true;
    }

    error = "unexpected pair-setup state";
    return false;
}

} // namespace gwl::airplay2
