#include "base64.hpp"

#include <array>
#include <bit>
#include <cstring>
#include <stdexcept>

namespace ada::mcp {

static_assert(std::endian::native == std::endian::little,
              "UI payloads are sent as the host's float32/uint32 bytes, which Studio reads as "
              "little-endian");

namespace {

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int decode_char(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

template<typename T>
std::span<const std::byte> as_bytes(const std::vector<T>& values) {
    return std::as_bytes(std::span<const T>(values));
}

} // namespace

std::string base64_encode(std::span<const std::byte> bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const auto n = (std::to_integer<std::uint32_t>(bytes[i]) << 16) |
                       (std::to_integer<std::uint32_t>(bytes[i + 1]) << 8) |
                       std::to_integer<std::uint32_t>(bytes[i + 2]);
        out += kAlphabet[(n >> 18) & 63U];
        out += kAlphabet[(n >> 12) & 63U];
        out += kAlphabet[(n >> 6) & 63U];
        out += kAlphabet[n & 63U];
    }
    const std::size_t rest = bytes.size() - i;
    if (rest == 1) {
        const auto n = std::to_integer<std::uint32_t>(bytes[i]) << 16;
        out += kAlphabet[(n >> 18) & 63U];
        out += kAlphabet[(n >> 12) & 63U];
        out += "==";
    } else if (rest == 2) {
        const auto n = (std::to_integer<std::uint32_t>(bytes[i]) << 16) |
                       (std::to_integer<std::uint32_t>(bytes[i + 1]) << 8);
        out += kAlphabet[(n >> 18) & 63U];
        out += kAlphabet[(n >> 12) & 63U];
        out += kAlphabet[(n >> 6) & 63U];
        out += '=';
    }
    return out;
}

std::vector<std::byte> base64_decode(const std::string& text) {
    if (text.size() % 4 != 0) {
        throw std::invalid_argument("base64 length is not a multiple of 4");
    }
    std::vector<std::byte> out;
    out.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        std::array<int, 4> v{};
        int padding = 0;
        for (std::size_t k = 0; k < 4; ++k) {
            const char c = text[i + k];
            if (c == '=' && i + 4 == text.size() && k >= 2) {
                v[k] = 0;
                ++padding;
                continue;
            }
            v[k] = decode_char(c);
            if (v[k] < 0 || padding > 0) {
                throw std::invalid_argument("malformed base64");
            }
        }
        const auto n = (static_cast<std::uint32_t>(v[0]) << 18) |
                       (static_cast<std::uint32_t>(v[1]) << 12) |
                       (static_cast<std::uint32_t>(v[2]) << 6) | static_cast<std::uint32_t>(v[3]);
        out.push_back(static_cast<std::byte>((n >> 16) & 255U));
        if (padding < 2) {
            out.push_back(static_cast<std::byte>((n >> 8) & 255U));
        }
        if (padding < 1) {
            out.push_back(static_cast<std::byte>(n & 255U));
        }
    }
    return out;
}

std::string encode_f32(const std::vector<double>& values) {
    std::vector<float> narrowed;
    narrowed.reserve(values.size());
    for (const double v : values) {
        narrowed.push_back(static_cast<float>(v));
    }
    return base64_encode(as_bytes(narrowed));
}

std::string encode_u32(const std::vector<std::uint32_t>& values) {
    return base64_encode(as_bytes(values));
}

} // namespace ada::mcp
