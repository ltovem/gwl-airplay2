#include "airplay2/crypto.h"

#include <algorithm>
#include <cctype>

namespace gwl::airplay2 {
namespace {

bool hex_value(char c, std::uint8_t& value) {
    if (c >= '0' && c <= '9') { value = static_cast<std::uint8_t>(c - '0'); return true; }
    if (c >= 'a' && c <= 'f') { value = static_cast<std::uint8_t>(10 + c - 'a'); return true; }
    if (c >= 'A' && c <= 'F') { value = static_cast<std::uint8_t>(10 + c - 'A'); return true; }
    return false;
}

bool decode_hex(const std::string& text, std::vector<std::uint8_t>& out) {
    if (text.empty() || (text.size() & 1u)) return false;
    out.clear();
    out.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        std::uint8_t hi = 0, lo = 0;
        if (!hex_value(text[i], hi) || !hex_value(text[i + 1], lo)) return false;
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return true;
}

bool decode_base64(const std::string& input, std::vector<std::uint8_t>& out) {
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int buffer = 0;
    int bits = 0;
    out.clear();
    for (const unsigned char c : input) {
        if (std::isspace(c)) continue;
        if (c == '=') break;
        const char* p = std::find(std::begin(table), std::end(table) - 1, static_cast<char>(c));
        if (p == std::end(table) - 1) return false;
        buffer = (buffer << 6) | static_cast<int>(p - table);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xff));
        }
    }
    return !out.empty();
}

} // namespace

bool CryptoParameters::valid() const noexcept {
    return !encrypted_key.empty() && iv.size() == 16;
}

bool extract_crypto_parameters(const AirPlaySdp& sdp, CryptoParameters& result) {
    result = {};
    result.fingerprint = sdp.fingerprint;

    if (!sdp.rsaaeskey.empty()) {
        if (!decode_base64(sdp.rsaaeskey, result.encrypted_key) &&
            !decode_hex(sdp.rsaaeskey, result.encrypted_key)) {
            result = {};
            return false;
        }
    }

    if (!sdp.aesiv.empty()) {
        if (!decode_hex(sdp.aesiv, result.iv)) {
            if (!decode_base64(sdp.aesiv, result.iv)) {
                result = {};
                return false;
            }
        }
    }

    return result.valid();
}

} // namespace gwl::airplay2
