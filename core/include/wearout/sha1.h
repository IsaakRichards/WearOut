// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wearout {

// Small self-contained SHA-1. Used only for the integrity digest that the
// Wearable transport attaches to every MessagePiece; it is not a security
// boundary (the Bluetooth bond provides the link encryption).
std::array<uint8_t, 20> sha1(const uint8_t *data, size_t len);

// URL-safe base64 alphabet (A-Z a-z 0-9 - _) without padding, which is what
// the Wearable transport uses for MessagePiece.digest.
std::string base64UrlNoPad(const uint8_t *data, size_t len);

// digest == base64UrlNoPad(sha1(bytes))
std::string wearDigest(const std::vector<uint8_t> &bytes);

} // namespace wearout
