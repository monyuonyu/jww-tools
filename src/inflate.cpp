// Minimal DEFLATE (RFC 1951) / gzip (RFC 1952) decoder for the images embedded in JWW files.
// SPDX-License-Identifier: MIT
#include "image.hpp"

#include <cstring>
#include <stdexcept>

namespace jww {

namespace {

struct Bits {
    const uint8_t* p;
    size_t n, pos = 0;
    uint32_t buf = 0;
    int cnt = 0;
    int bit() {
        if (cnt == 0) {
            if (pos >= n) throw std::runtime_error("inflate: truncated");
            buf = p[pos++];
            cnt = 8;
        }
        int b = buf & 1;
        buf >>= 1;
        cnt--;
        return b;
    }
    uint32_t bits(int k) {
        uint32_t v = 0;
        for (int i = 0; i < k; i++) v |= uint32_t(bit()) << i;
        return v;
    }
};

struct Huff {
    uint16_t count[16] = {0};
    uint16_t sym[320] = {0};
    void build(const uint8_t* len, int n) {
        std::memset(count, 0, sizeof count);
        for (int i = 0; i < n; i++) count[len[i]]++;
        count[0] = 0;
        uint16_t offs[16];
        offs[1] = 0;
        for (int i = 1; i < 15; i++) offs[i + 1] = offs[i] + count[i];
        for (int i = 0; i < n; i++)
            if (len[i]) sym[offs[len[i]]++] = uint16_t(i);
    }
    int decode(Bits& b) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; len++) {
            code |= b.bit();
            int c = count[len];
            if (code - c < first) return sym[index + (code - first)];
            index += c;
            first += c;
            first <<= 1;
            code <<= 1;
        }
        throw std::runtime_error("inflate: bad code");
    }
};

const uint16_t kLenBase[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void codes(Bits& b, std::vector<uint8_t>& out, const Huff& lit, const Huff& dist) {
    for (;;) {
        int s = lit.decode(b);
        if (s < 256) { out.push_back(uint8_t(s)); continue; }
        if (s == 256) return;
        s -= 257;
        if (s >= 29) throw std::runtime_error("inflate: bad length");
        size_t len = kLenBase[s] + b.bits(kLenExtra[s]);
        int d = dist.decode(b);
        if (d >= 30) throw std::runtime_error("inflate: bad distance");
        size_t off = kDistBase[d] + b.bits(kDistExtra[d]);
        if (off > out.size()) throw std::runtime_error("inflate: distance too far");
        size_t from = out.size() - off;
        for (size_t i = 0; i < len; i++) out.push_back(out[from + i]);
    }
}

}  // namespace

std::vector<uint8_t> inflate_raw(const uint8_t* p, size_t n) {
    Bits b{p, n};
    std::vector<uint8_t> out;
    int last;
    do {
        last = b.bit();
        int type = int(b.bits(2));
        if (type == 0) {
            b.cnt = 0;  // align to byte
            if (b.pos + 4 > n) throw std::runtime_error("inflate: truncated");
            uint16_t len = p[b.pos] | (p[b.pos + 1] << 8);
            b.pos += 4;
            if (b.pos + len > n) throw std::runtime_error("inflate: truncated");
            out.insert(out.end(), p + b.pos, p + b.pos + len);
            b.pos += len;
        } else if (type == 1) {
            uint8_t l[288];
            for (int i = 0; i < 144; i++) l[i] = 8;
            for (int i = 144; i < 256; i++) l[i] = 9;
            for (int i = 256; i < 280; i++) l[i] = 7;
            for (int i = 280; i < 288; i++) l[i] = 8;
            Huff lit, dist;
            lit.build(l, 288);
            uint8_t d[30];
            for (int i = 0; i < 30; i++) d[i] = 5;
            dist.build(d, 30);
            codes(b, out, lit, dist);
        } else if (type == 2) {
            int hlit = int(b.bits(5)) + 257, hdist = int(b.bits(5)) + 1, hclen = int(b.bits(4)) + 4;
            static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            uint8_t cl[19] = {0};
            for (int i = 0; i < hclen; i++) cl[order[i]] = uint8_t(b.bits(3));
            Huff clh;
            clh.build(cl, 19);
            uint8_t lens[320] = {0};
            int i = 0;
            while (i < hlit + hdist) {
                int s = clh.decode(b);
                if (s < 16) lens[i++] = uint8_t(s);
                else {
                    int rep, val = 0;
                    if (s == 16) {
                        if (i == 0) throw std::runtime_error("inflate: bad repeat");
                        val = lens[i - 1];
                        rep = 3 + int(b.bits(2));
                    } else if (s == 17) rep = 3 + int(b.bits(3));
                    else rep = 11 + int(b.bits(7));
                    if (i + rep > hlit + hdist) throw std::runtime_error("inflate: bad lengths");
                    while (rep--) lens[i++] = uint8_t(val);
                }
            }
            Huff lit, dist;
            lit.build(lens, hlit);
            dist.build(lens + hlit, hdist);
            codes(b, out, lit, dist);
        } else {
            throw std::runtime_error("inflate: bad block type");
        }
    } while (!last);
    return out;
}

std::vector<uint8_t> gunzip(const std::vector<uint8_t>& d) {
    if (d.size() < 18 || d[0] != 0x1F || d[1] != 0x8B || d[2] != 8) {
        // zlib stream (RFC 1950)?
        if (d.size() > 6 && (d[0] & 0x0F) == 8 && ((d[0] << 8) | d[1]) % 31 == 0) return inflate_raw(d.data() + 2, d.size() - 2);
        throw std::runtime_error("not gzip data");
    }
    size_t p = 10;
    uint8_t flg = d[3];
    if (flg & 4) { if (p + 2 > d.size()) throw std::runtime_error("gzip: truncated"); p += 2 + (d[p] | (d[p + 1] << 8)); }
    if (flg & 8) { while (p < d.size() && d[p]) p++; p++; }
    if (flg & 16) { while (p < d.size() && d[p]) p++; p++; }
    if (flg & 2) p += 2;
    if (p >= d.size()) throw std::runtime_error("gzip: truncated");
    return inflate_raw(d.data() + p, d.size() - p);
}

bool image_size(const std::vector<uint8_t>& d, int& w, int& h) {
    auto be16 = [&](size_t i) { return (d[i] << 8) | d[i + 1]; };
    auto le32 = [&](size_t i) { return int32_t(d[i] | (d[i + 1] << 8) | (d[i + 2] << 16) | (uint32_t(d[i + 3]) << 24)); };
    if (d.size() > 26 && d[0] == 'B' && d[1] == 'M') { w = le32(18); h = std::abs(le32(22)); return w > 0 && h > 0; }
    if (d.size() > 24 && d[0] == 0x89 && d[1] == 'P') { w = (d[16] << 24) | (d[17] << 16) | (d[18] << 8) | d[19]; h = (d[20] << 24) | (d[21] << 16) | (d[22] << 8) | d[23]; return w > 0 && h > 0; }
    if (d.size() > 10 && d[0] == 'G' && d[1] == 'I' && d[2] == 'F') { w = d[6] | (d[7] << 8); h = d[8] | (d[9] << 8); return w > 0 && h > 0; }
    if (d.size() > 4 && d[0] == 0xFF && d[1] == 0xD8) {
        size_t i = 2;
        while (i + 9 < d.size()) {
            if (d[i] != 0xFF) { i++; continue; }
            uint8_t m = d[i + 1];
            if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { i += 2; continue; }
            if ((m >= 0xC0 && m <= 0xCF) && m != 0xC4 && m != 0xC8 && m != 0xCC) { h = be16(i + 5); w = be16(i + 7); return w > 0 && h > 0; }
            i += 2 + be16(i + 2);
        }
    }
    return false;
}

}  // namespace jww
