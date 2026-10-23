#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ada::mcp {

/// Standard base64 (RFC 4648, with padding) of raw bytes.
std::string base64_encode(std::span<const std::byte> bytes);

/// Inverse of `base64_encode`; throws `std::invalid_argument` on malformed input.
std::vector<std::byte> base64_decode(const std::string& text);

/// Little-endian float32 / uint32 arrays as base64, the encoding Studio's
/// typed arrays read directly. Coordinates go out as float32 because that is
/// what the GPU draws with; the kernel keeps double everywhere internally.
std::string encode_f32(const std::vector<double>& values);
std::string encode_u32(const std::vector<std::uint32_t>& values);

} // namespace ada::mcp
