// SPDX-License-Identifier: Apache-2.0
#include "wearout/protobuf.h"

#include <cstdio>
#include <sstream>

namespace wearout::pb {

void Writer::rawVarint(uint64_t v) {
    while (v >= 0x80) {
        m_buf.push_back(static_cast<uint8_t>(v | 0x80));
        v >>= 7;
    }
    m_buf.push_back(static_cast<uint8_t>(v));
}

void Writer::tag(uint32_t field, WireType wt) {
    rawVarint((static_cast<uint64_t>(field) << 3) | static_cast<uint8_t>(wt));
}

void Writer::varint(uint32_t field, uint64_t v) {
    tag(field, WireType::Varint);
    rawVarint(v);
}

void Writer::bytes(uint32_t field, const uint8_t *data, size_t len) {
    tag(field, WireType::LengthDelimited);
    rawVarint(len);
    m_buf.insert(m_buf.end(), data, data + len);
}

namespace {

uint64_t readVarint(const uint8_t *data, size_t len, size_t &pos) {
    uint64_t result = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        if (pos >= len)
            throw DecodeError("truncated varint");
        uint8_t b = data[pos++];
        result |= static_cast<uint64_t>(b & 0x7f) << shift;
        if (!(b & 0x80))
            return result;
    }
    throw DecodeError("varint too long");
}

uint64_t readFixed(const uint8_t *data, size_t len, size_t &pos, int n) {
    if (pos + n > len)
        throw DecodeError("truncated fixed field");
    uint64_t v = 0;
    for (int i = 0; i < n; ++i)
        v |= static_cast<uint64_t>(data[pos + i]) << (8 * i);
    pos += n;
    return v;
}

} // namespace

std::vector<Field> parse(const uint8_t *data, size_t len) {
    std::vector<Field> out;
    size_t pos = 0;
    while (pos < len) {
        uint64_t key = readVarint(data, len, pos);
        Field f;
        f.number = static_cast<uint32_t>(key >> 3);
        f.type = static_cast<WireType>(key & 7);
        if (f.number == 0)
            throw DecodeError("field number 0");
        switch (f.type) {
        case WireType::Varint:
            f.value = readVarint(data, len, pos);
            break;
        case WireType::Fixed64:
            f.value = readFixed(data, len, pos, 8);
            break;
        case WireType::Fixed32:
            f.value = readFixed(data, len, pos, 4);
            break;
        case WireType::LengthDelimited: {
            uint64_t n = readVarint(data, len, pos);
            if (n > len - pos)
                throw DecodeError("truncated length-delimited field");
            f.bytes.assign(data + pos, data + pos + n);
            pos += n;
            break;
        }
        default:
            throw DecodeError("unsupported wire type " + std::to_string(static_cast<int>(f.type)));
        }
        out.push_back(std::move(f));
    }
    return out;
}

namespace {

bool looksPrintable(const Bytes &b) {
    if (b.empty())
        return true;
    for (uint8_t c : b) {
        if (c == '\n' || c == '\t')
            continue;
        if (c < 0x20 || c == 0x7f)
            return false;
    }
    return true;
}

std::string hex(const Bytes &b, size_t max = 48) {
    std::string s;
    char tmp[4];
    for (size_t i = 0; i < b.size() && i < max; ++i) {
        std::snprintf(tmp, sizeof tmp, "%02x", b[i]);
        s += tmp;
    }
    if (b.size() > max)
        s += "...(" + std::to_string(b.size()) + " bytes)";
    return s;
}

} // namespace

std::string dump(const Bytes &b, int indent, int maxDepth) {
    std::ostringstream os;
    std::string pad(indent * 2, ' ');
    std::vector<Field> fields;
    try {
        fields = parse(b);
    } catch (const DecodeError &) {
        os << pad << "<not a message: " << hex(b) << ">\n";
        return os.str();
    }
    for (const Field &f : fields) {
        os << pad << f.number << ": ";
        switch (f.type) {
        case WireType::Varint:
            os << f.asInt64() << "\n";
            break;
        case WireType::Fixed32:
            os << "fixed32 0x" << std::hex << f.value << std::dec << "\n";
            break;
        case WireType::Fixed64:
            os << "fixed64 0x" << std::hex << f.value << std::dec << "\n";
            break;
        case WireType::LengthDelimited: {
            bool nested = false;
            if (maxDepth > 0 && !f.bytes.empty() && !looksPrintable(f.bytes)) {
                try {
                    parse(f.bytes);
                    nested = true;
                } catch (const DecodeError &) {
                }
            }
            if (nested) {
                os << "{\n" << dump(f.bytes, indent + 1, maxDepth - 1) << pad << "}\n";
            } else if (looksPrintable(f.bytes)) {
                os << '"' << std::string(f.bytes.begin(), f.bytes.end()) << "\"\n";
            } else {
                os << "bytes " << hex(f.bytes) << "\n";
            }
            break;
        }
        default:
            os << "?\n";
        }
    }
    return os.str();
}

} // namespace wearout::pb
