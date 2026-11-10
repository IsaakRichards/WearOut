// SPDX-License-Identifier: Apache-2.0
//
// Minimal protobuf (proto2 wire format) encoder/decoder.
//
// WearOut deliberately avoids a dependency on libprotobuf: the Sailfish SDK
// ships an old one, and the Wear OS schema is still being reverse engineered,
// so we want full control over unknown fields (we log them instead of
// dropping them).
#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace wearout::pb {

using Bytes = std::vector<uint8_t>;

enum class WireType : uint8_t {
    Varint = 0,
    Fixed64 = 1,
    LengthDelimited = 2,
    StartGroup = 3,   // deprecated, rejected
    EndGroup = 4,     // deprecated, rejected
    Fixed32 = 5,
};

struct DecodeError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Writer {
public:
    void varint(uint32_t field, uint64_t v);
    void int32(uint32_t field, int32_t v) { varint(field, static_cast<uint64_t>(static_cast<int64_t>(v))); }
    void int64(uint32_t field, int64_t v) { varint(field, static_cast<uint64_t>(v)); }
    void boolean(uint32_t field, bool v) { varint(field, v ? 1 : 0); }
    void bytes(uint32_t field, const uint8_t *data, size_t len);
    void bytes(uint32_t field, const Bytes &b) { bytes(field, b.data(), b.size()); }
    void string(uint32_t field, const std::string &s) {
        bytes(field, reinterpret_cast<const uint8_t *>(s.data()), s.size());
    }
    void message(uint32_t field, const Bytes &encoded) { bytes(field, encoded); }

    const Bytes &data() const { return m_buf; }
    Bytes take() { return std::move(m_buf); }

private:
    void tag(uint32_t field, WireType wt);
    void rawVarint(uint64_t v);
    Bytes m_buf;
};

// A single decoded field. For LengthDelimited the payload is in `bytes`;
// for every other wire type the numeric value is in `value`.
struct Field {
    uint32_t number = 0;
    WireType type = WireType::Varint;
    uint64_t value = 0;
    Bytes bytes;

    int32_t asInt32() const { return static_cast<int32_t>(value); }
    int64_t asInt64() const { return static_cast<int64_t>(value); }
    bool asBool() const { return value != 0; }
    std::string asString() const { return std::string(bytes.begin(), bytes.end()); }
};

// Parses a whole message into a flat list of fields (in wire order).
std::vector<Field> parse(const uint8_t *data, size_t len);
inline std::vector<Field> parse(const Bytes &b) { return parse(b.data(), b.size()); }

// Best-effort human readable dump of an arbitrary protobuf blob, recursing
// into length-delimited fields that look like nested messages. This is the
// main reverse-engineering aid: unknown traffic is printed with it.
std::string dump(const Bytes &b, int indent = 0, int maxDepth = 6);

} // namespace wearout::pb
