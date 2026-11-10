// SPDX-License-Identifier: Apache-2.0
#include "wearout/sha1.h"

#include <cstring>

namespace wearout {

namespace {
inline uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
} // namespace

std::array<uint8_t, 20> sha1(const uint8_t *data, size_t len) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};

    std::vector<uint8_t> msg(data, data + len);
    uint64_t bitLen = static_cast<uint64_t>(len) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56)
        msg.push_back(0);
    for (int i = 7; i >= 0; --i)
        msg.push_back(static_cast<uint8_t>(bitLen >> (i * 8)));

    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(msg[off + 4 * i]) << 24) | (uint32_t(msg[off + 4 * i + 1]) << 16) |
                   (uint32_t(msg[off + 4 * i + 2]) << 8) | uint32_t(msg[off + 4 * i + 3]);
        }
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }

    std::array<uint8_t, 20> out{};
    for (int i = 0; i < 5; ++i) {
        out[4 * i] = static_cast<uint8_t>(h[i] >> 24);
        out[4 * i + 1] = static_cast<uint8_t>(h[i] >> 16);
        out[4 * i + 2] = static_cast<uint8_t>(h[i] >> 8);
        out[4 * i + 3] = static_cast<uint8_t>(h[i]);
    }
    return out;
}

std::string base64UrlNoPad(const uint8_t *data, size_t len) {
    static const char *A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    size_t i = 0;
    for (; i + 2 < len; i += 3) {
        uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | data[i + 2];
        out += A[(v >> 18) & 63];
        out += A[(v >> 12) & 63];
        out += A[(v >> 6) & 63];
        out += A[v & 63];
    }
    size_t rem = len - i;
    if (rem == 1) {
        uint32_t v = uint32_t(data[i]) << 16;
        out += A[(v >> 18) & 63];
        out += A[(v >> 12) & 63];
    } else if (rem == 2) {
        uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8);
        out += A[(v >> 18) & 63];
        out += A[(v >> 12) & 63];
        out += A[(v >> 6) & 63];
    }
    return out;
}

std::string wearDigest(const std::vector<uint8_t> &bytes) {
    auto h = sha1(bytes.data(), bytes.size());
    return base64UrlNoPad(h.data(), h.size());
}

} // namespace wearout
