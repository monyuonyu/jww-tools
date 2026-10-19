// SPDX-License-Identifier: MIT
#include "jww.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#endif

namespace jww {

#include "cp932_table.inc"

namespace {
void put_utf8(std::string& o, uint32_t c) {
    if (c < 0x80) o += char(c);
    else if (c < 0x800) { o += char(0xC0 | (c >> 6)); o += char(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { o += char(0xE0 | (c >> 12)); o += char(0x80 | ((c >> 6) & 0x3F)); o += char(0x80 | (c & 0x3F)); }
    else { o += char(0xF0 | (c >> 18)); o += char(0x80 | ((c >> 12) & 0x3F)); o += char(0x80 | ((c >> 6) & 0x3F)); o += char(0x80 | (c & 0x3F)); }
}
template <size_t N>
int lookup(const uint16_t (&t)[N][2], uint32_t key) {
    size_t lo = 0, hi = N;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (t[mid][0] < key) lo = mid + 1; else hi = mid;
    }
    if (lo < N && t[lo][0] == key) return t[lo][1];
    return -1;
}
bool is_lead(uint8_t c) { return (c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC); }
}  // namespace

std::string sjis_to_utf8(const std::string& s) {
    std::string o;
    o.reserve(s.size() * 3 / 2);
    for (size_t i = 0; i < s.size(); i++) {
        uint8_t c = uint8_t(s[i]);
        if (c < 0x80) { o += char(c); continue; }
        if (is_lead(c) && i + 1 < s.size()) {
            int u = lookup(kCp932Double, (uint32_t(c) << 8) | uint8_t(s[i + 1]));
            if (u >= 0) { put_utf8(o, u); i++; continue; }
            // Unassigned (e.g. user-defined area): keep as U+FFFD
            put_utf8(o, 0xFFFD); i++; continue;
        }
        int u = lookup(kCp932Single, c);
        put_utf8(o, u >= 0 ? uint32_t(u) : 0xFFFD);
    }
    return o;
}

std::string utf16_to_utf8(const std::u16string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        uint32_t c = s[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            i++;
        }
        put_utf8(o, c);
    }
    return o;
}

// UTF-8 -> CP932. Characters that CP932 lacks become DXF "\U+XXXX" escapes.
std::string utf8_to_sjis(const std::string& s, bool* lossy) {
    std::string o;
    for (size_t i = 0; i < s.size();) {
        uint8_t c = uint8_t(s[i]);
        uint32_t u;
        int n;
        if (c < 0x80) { u = c; n = 1; }
        else if ((c >> 5) == 6) { u = c & 0x1F; n = 2; }
        else if ((c >> 4) == 14) { u = c & 0x0F; n = 3; }
        else { u = c & 0x07; n = 4; }
        for (int k = 1; k < n && i + k < s.size(); k++) u = (u << 6) | (uint8_t(s[i + k]) & 0x3F);
        i += n;
        if (u < 0x80) { o += char(u); continue; }
        int e = u < 0x10000 ? lookup(kCp932Encode, u) : -1;
        if (e >= 0) {
            if (e > 0xFF) o += char(e >> 8);
            o += char(e & 0xFF);
        } else {
            if (lossy) *lossy = true;
            char buf[32];
            if (u >= 0x10000)  // outside the BMP: a UTF-16 surrogate pair of escapes
                snprintf(buf, sizeof buf, "\\U+%04X\\U+%04X", 0xD800 + ((u - 0x10000) >> 10), 0xDC00 + ((u - 0x10000) & 0x3FF));
            else
                snprintf(buf, sizeof buf, "\\U+%04X", u);
            o += buf;
        }
    }
    return o;
}

std::FILE* u8fopen(const std::string& path, const char* mode) {
#ifdef _WIN32
    auto wide = [](const std::string& u) {
        int n = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, nullptr, 0);
        std::wstring w(n > 0 ? n - 1 : 0, L'\0');
        if (n > 1) MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, &w[0], n);
        return w;
    };
    return _wfopen(wide(path).c_str(), wide(mode).c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

}  // namespace jww
