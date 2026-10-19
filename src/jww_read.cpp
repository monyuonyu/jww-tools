// jww-tools: reader for Jw_cad drawings
// Written from "Jw_cadのデータ形式" (the format description published with Jw_cad)
// and the MFC CArchive serialization rules.
// SPDX-License-Identifier: MIT
#include "jww.hpp"

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <cmath>

namespace jww {

namespace {

struct Eof : std::runtime_error { Eof() : std::runtime_error("unexpected end of file") {} };

class Archive {
public:
    Archive(const uint8_t* d, size_t n) : d_(d), n_(n) {}
    size_t pos() const { return pos_; }
    void seek(size_t p) { pos_ = p; }
    size_t size() const { return n_; }
    void need(size_t k) { if (pos_ + k > n_) throw Eof(); }
    uint8_t u8() { need(1); return d_[pos_++]; }
    uint16_t u16() { need(2); uint16_t v = d_[pos_] | (d_[pos_ + 1] << 8); pos_ += 2; return v; }
    uint32_t u32() { need(4); uint32_t v; std::memcpy(&v, d_ + pos_, 4); pos_ += 4; return v; }  // little-endian hosts
    double f64() { need(8); double v; std::memcpy(&v, d_ + pos_, 8); pos_ += 8; return v; }
    void skip(size_t k) { need(k); pos_ += k; }
    const uint8_t* ptr() const { return d_ + pos_; }

    // MFC CString (CArchive operator>>): BYTE length, 0xFF -> WORD, 0xFFFF -> DWORD.
    // Unicode strings are announced by 0xFF 0xFFFE and then a length in UTF-16 units.
    std::string cstring() {
        bool wide = false;
        uint32_t len = u8();
        if (len == 0xFF) {
            len = u16();
            if (len == 0xFFFE) {
                wide = true;
                len = u8();
                if (len == 0xFF) {
                    len = u16();
                    if (len == 0xFFFF) len = u32();
                }
            } else if (len == 0xFFFF) {
                len = u32();
            }
        }
        if (wide) {
            unicode_seen = true;
            need(size_t(len) * 2);
            std::u16string w(len, u'\0');
            for (uint32_t i = 0; i < len; i++) w[i] = d_[pos_ + 2 * i] | (d_[pos_ + 2 * i + 1] << 8);
            pos_ += size_t(len) * 2;
            return utf16_to_utf8(w);
        }
        need(len);
        std::string s(reinterpret_cast<const char*>(d_ + pos_), len);
        pos_ += len;
        return sjis_to_utf8(s);
    }
    uint32_t count() {  // CArchive::ReadCount
        uint32_t c = u16();
        if (c == 0xFFFF) c = u32();
        return c;
    }
    bool unicode_seen = false;

private:
    const uint8_t* d_;
    size_t n_;
    size_t pos_ = 0;
};

// The archive's class/object map (CArchive::m_pLoadArray). Index 0 is the null object.
struct ObjectMap {
    std::vector<std::string> items{""};  // class name for class entries, "" for objects
    std::vector<bool> is_class{false};
    uint32_t add(const std::string& cls, bool klass) { items.push_back(cls); is_class.push_back(klass); return uint32_t(items.size() - 1); }
};

class Reader {
public:
    Reader(Archive& ar, Drawing& dw) : ar_(ar), dw_(dw) {}

    void base(Base& b) {
        b.group = ar_.u32();
        b.pen_style = ar_.u8();
        b.pen_color = ar_.u16();
        if (dw_.version >= 351) b.pen_width = ar_.u16();
        b.layer = ar_.u16();
        b.glayer = ar_.u16();
        b.flag = ar_.u16();
    }
    void line(Entity& e) {
        e.kind = Kind::Line;
        base(e.b);
        e.p[0].x = ar_.f64(); e.p[0].y = ar_.f64();
        e.p[1].x = ar_.f64(); e.p[1].y = ar_.f64();
    }
    void arc(Entity& e) {
        e.kind = Kind::Arc;
        base(e.b);
        e.p[0].x = ar_.f64(); e.p[0].y = ar_.f64();
        e.r = ar_.f64();
        e.start = ar_.f64();
        e.sweep = ar_.f64();
        e.tilt = ar_.f64();
        e.flat = ar_.f64();
        e.full = ar_.u32() != 0;
    }
    void point(Entity& e) {
        e.kind = Kind::Point;
        base(e.b);
        e.p[0].x = ar_.f64(); e.p[0].y = ar_.f64();
        e.temporary = ar_.u32() != 0;
        if (e.b.pen_style == 100) {
            e.code = ar_.u32();
            e.angle = ar_.f64();
            e.scale = ar_.f64();
        }
    }
    void text(Entity& e) {
        e.kind = Kind::Text;
        base(e.b);
        e.p[0].x = ar_.f64(); e.p[0].y = ar_.f64();
        e.p[1].x = ar_.f64(); e.p[1].y = ar_.f64();
        e.text_type = ar_.u32();
        e.size_x = ar_.f64();
        e.size_y = ar_.f64();
        e.spacing = ar_.f64();
        e.angle = ar_.f64();
        e.font = ar_.cstring();
        e.str = ar_.cstring();
    }
    void solid(Entity& e) {
        e.kind = Kind::Solid;
        base(e.b);
        for (int i = 0; i < 4; i++) { e.p[i].x = ar_.f64(); e.p[i].y = ar_.f64(); }
        if (e.b.pen_color == 10) e.color = ar_.u32();
    }
    void block(Entity& e) {
        e.kind = Kind::Block;
        base(e.b);
        e.p[0].x = ar_.f64(); e.p[0].y = ar_.f64();
        e.sx = ar_.f64();
        e.sy = ar_.f64();
        e.angle = ar_.f64();
        e.number = ar_.u32();
    }
    void dim(Entity& e) {
        e.kind = Kind::Dim;
        base(e.b);
        e.sub.resize(2);
        line(e.sub[0]);
        text(e.sub[1]);
        if (dw_.version >= 420) {
            e.sxf_mode = ar_.u16();
            e.sub.resize(8);
            line(e.sub[2]);
            line(e.sub[3]);
            for (int i = 4; i < 8; i++) point(e.sub[i]);
        }
    }

    // CArchive::ReadObject. Returns the class name of the object read ("" for null).
    std::string read_object_header() {
        uint32_t tag = ar_.u16();
        uint32_t ob;
        std::string cls;
        if (tag == 0x7FFF) {
            ob = ar_.u32();
        } else {
            ob = ((tag & 0x8000u) << 16) | (tag & 0x7FFFu);
        }
        if (tag == 0xFFFF) {
            uint16_t schema = ar_.u16();
            uint16_t len = ar_.u16();
            ar_.need(len);
            cls.assign(reinterpret_cast<const char*>(ar_.ptr()), len);
            ar_.skip(len);
            if (schema != dw_.version && !schema_warned_) {
                schema_warned_ = true;
                dw_.warnings.push_back("class schema " + std::to_string(schema) + " differs from file version " + std::to_string(dw_.version));
            }
            map_.add(cls, true);
        } else if (ob & 0x80000000u) {
            uint32_t idx = ob & 0x7FFFFFFFu;
            if (idx >= map_.items.size() || !map_.is_class[idx]) throw std::runtime_error("bad class index " + std::to_string(idx));
            cls = map_.items[idx];
        } else {
            if (ob == 0) return "";
            // A reference to an object read before. Jw_cad does not share figures, so this is unexpected.
            throw std::runtime_error("unsupported object back-reference " + std::to_string(ob));
        }
        map_.add("", false);
        return cls;
    }

    bool read_entity(Entity& e) {
        std::string cls = read_object_header();
        if (cls.empty()) return false;
        if (cls == "CDataSen") line(e);
        else if (cls == "CDataEnko") arc(e);
        else if (cls == "CDataTen") point(e);
        else if (cls == "CDataMoji") text(e);
        else if (cls == "CDataSolid") solid(e);
        else if (cls == "CDataBlock") block(e);
        else if (cls == "CDataSunpou") dim(e);
        else throw std::runtime_error("unknown figure class " + cls);
        return true;
    }

    void entity_list(std::vector<Entity>& out) {
        uint32_t n = ar_.count();
        out.reserve(out.size() + n);
        for (uint32_t i = 0; i < n; i++) {
            Entity e;
            if (read_entity(e)) out.push_back(std::move(e));
        }
    }

    void block_list(std::vector<BlockDef>& out) {
        uint32_t n = ar_.count();
        for (uint32_t i = 0; i < n; i++) {
            std::string cls = read_object_header();
            if (cls.empty()) continue;
            if (cls != "CDataList") throw std::runtime_error("unexpected class in block list: " + cls);
            BlockDef bd;
            base(bd.b);
            bd.number = ar_.u32();
            bd.referenced = ar_.u32() != 0;
            bd.time = ar_.u32();
            bd.name = ar_.cstring();
            auto k = bd.name.find("@@SfigorgFlag@@");
            if (k != std::string::npos) {
                bd.sfig_flag = std::atoi(bd.name.c_str() + k + 15);
                bd.name.erase(k);
            }
            entity_list(bd.ents);
            out.push_back(std::move(bd));
        }
    }

private:
    Archive& ar_;
    Drawing& dw_;
    ObjectMap map_;
    bool schema_warned_ = false;
};

// Is there a figure list starting at p? (count, then a new-class tag for "CData...")
bool looks_like_list(const uint8_t* d, size_t n, size_t p, uint32_t version) {
    size_t q = p + 2;
    if (p + 2 > n) return false;
    uint32_t c = d[p] | (d[p + 1] << 8);
    if (c == 0xFFFF) { q = p + 6; }
    if (q + 6 > n) return false;
    if (c == 0) return false;
    if (d[q] != 0xFF || d[q + 1] != 0xFF) return false;
    uint16_t len = d[q + 4] | (d[q + 5] << 8);
    if (len < 6 || len > 32 || q + 6 + len > n) return false;
    (void)version;
    return std::memcmp(d + q + 6, "CData", 5) == 0;
}

void read_header(Archive& ar, Drawing& dw) {
    const uint32_t v = dw.version;
    dw.memo = ar.cstring();
    dw.paper = ar.u32();
    dw.write_glayer = ar.u32();
    for (int g = 0; g < 16; g++) {
        LayerGroup& G = dw.groups[g];
        G.state = ar.u32();
        G.write_layer = ar.u32();
        G.scale = ar.f64();
        G.protect = ar.u32();
        for (int l = 0; l < 16; l++) {
            G.layers[l].state = ar.u32();
            G.layers[l].protect = ar.u32();
        }
    }
    ar.skip(14 * 4);  // dummy
    ar.skip(5 * 4);   // 寸法設定
    ar.skip(4);       // dummy
    ar.skip(4);       // 線描画の最大幅
    ar.skip(3 * 8);   // プリンタ原点, 倍率
    ar.skip(4 + 4);   // プリンタ設定, 目盛設定モード
    ar.skip(5 * 8);   // 目盛
    for (int g = 0; g < 16; g++)
        for (int l = 0; l < 16; l++) dw.groups[g].layers[l].name = ar.cstring();
    for (int g = 0; g < 16; g++) dw.groups[g].name = ar.cstring();
    ar.skip(8 + 8 + 4 + 8);  // 日影
    if (v >= 300) ar.skip(2 * 8);  // 天空図
    ar.skip(4);                    // 2.5D unit
    ar.skip(3 * 8 + 3 * 8);        // 画面倍率, 範囲記憶
    if (v >= 300) ar.skip(8 * (3 * 8 + 4));
    else ar.skip(4 * (3 * 8));
    if (v >= 300) ar.skip(3 * 8 + 4 + 3 * 8 + 4);  // 文字の描画状態
    ar.skip(10 * 8 + 8);  // 複線間隔, 両側複線
    for (int n = 0; n <= 9; n++) { PenDef& p = dw.pens[n]; p.color = ar.u32(); p.width = ar.u32(); }
    for (int n = 0; n <= 9; n++) { PenDef& p = dw.pens[n]; p.prt_color = ar.u32(); p.prt_width = ar.u32(); p.prt_point_r = ar.f64(); }
    for (int n = 2; n <= 9; n++) { auto& t = dw.ltypes[n]; t.pattern = ar.u32(); t.unit_dot = ar.u32(); t.pitch = ar.u32(); t.prt_pitch = ar.u32(); }
    for (int n = 11; n <= 15; n++) { auto& t = dw.ltypes[n]; t.pattern = ar.u32(); t.unit_dot = ar.u32(); t.pitch = ar.u32(); ar.u32(); t.prt_pitch = ar.u32(); }
    for (int n = 16; n <= 19; n++) { auto& t = dw.ltypes[n]; t.pattern = ar.u32(); t.unit_dot = ar.u32(); t.pitch = ar.u32(); t.prt_pitch = ar.u32(); }
    ar.skip(11 * 4);
    if (v >= 223) ar.skip(4 + 4 + 3 * 4 + 2 * 8 + 2 * 8 + 8);
    if (v >= 225) ar.skip(8 + 2 * 8 + 8);
    if (v >= 230) { ar.u32(); dw.solid_color_default = ar.u32(); }
    if (v >= 420) {
        for (int n = 0; n <= 256; n++) { PenDef& p = dw.pens[100 + n]; p.color = ar.u32(); p.width = ar.u32(); }
        for (int n = 0; n <= 256; n++) {
            PenDef& p = dw.pens[100 + n];
            p.name = ar.cstring();
            p.prt_color = ar.u32(); p.prt_width = ar.u32(); p.prt_point_r = ar.f64();
        }
        for (int n = 0; n <= 32; n++) { auto& t = dw.ltypes[30 + n]; t.pattern = ar.u32(); t.unit_dot = ar.u32(); t.pitch = ar.u32(); t.prt_pitch = ar.u32(); }
        for (int n = 0; n <= 32; n++) {
            auto& t = dw.ltypes[30 + n];
            t.name = ar.cstring();
            t.segments = ar.u32();
            for (int j = 0; j < 10; j++) t.seg[j] = ar.f64();
        }
    }
    for (int i = 1; i <= 10; i++) { dw.moji_x[i] = ar.f64(); dw.moji_y[i] = ar.f64(); dw.moji_d[i] = ar.f64(); dw.moji_col[i] = ar.u32(); }
    ar.skip(3 * 8 + 2 * 4);  // 書込み文字
    ar.skip(2 * 8);          // 文字位置整理
    ar.skip(4);              // 基準点ずれ flag
    ar.skip(6 * 8);          // ずれ位置
}

}  // namespace

Drawing read_buffer(const std::vector<uint8_t>& data) {
    Drawing dw;
    if (data.size() < 12) throw std::runtime_error("file too short");
    if (std::memcmp(data.data(), "JwwData.", 8) == 0) dw.format = "jww";
    else if (std::memcmp(data.data(), "JwsData.", 8) == 0) dw.format = "jws";
    else throw std::runtime_error("not a Jw_cad JWW/JWS file (signature)");
    Archive ar(data.data(), data.size());
    ar.skip(8);
    dw.version = ar.u32();
    if (dw.format == "jws") throw std::runtime_error("JWS symbol files are not supported yet");

    size_t list_pos = 0;
    try {
        read_header(ar, dw);
        list_pos = ar.pos();
        dw.header_complete = looks_like_list(data.data(), data.size(), list_pos, dw.version);
    } catch (const Eof&) {
        dw.header_complete = false;
    }
    if (!dw.header_complete) {
        // An empty drawing has a zero count followed by the (possibly empty) block list.
        if (list_pos && list_pos + 2 <= data.size() && data[list_pos] == 0 && data[list_pos + 1] == 0) {
            dw.header_complete = true;
        } else {
            // Fall back to searching for the figure list (first "CData..." class declaration).
            size_t found = 0;
            for (size_t i = 100; i + 12 < data.size(); i++) {
                if (looks_like_list(data.data(), data.size(), i, dw.version)) { found = i; break; }
            }
            if (!found) throw std::runtime_error("figure list not found");
            dw.list_found_by_scan = true;
            dw.warnings.push_back("header layout not recognised for version " + std::to_string(dw.version) + "; figure list found by search at offset " + std::to_string(found) + " (expected " + std::to_string(list_pos) + ")");
            list_pos = found;
        }
    }
    ar.seek(list_pos);
    Reader rd(ar, dw);
    try {
        rd.entity_list(dw.ents);
        rd.block_list(dw.blocks);
        if (dw.version >= 700 && ar.pos() + 4 <= ar.size()) {
            uint32_t n = ar.u32();
            for (uint32_t i = 0; i < n; i++) {
                Image im;
                im.name = ar.cstring();
                uint32_t sz = ar.u32();
                ar.need(sz);
                im.data.assign(ar.ptr(), ar.ptr() + sz);
                ar.skip(sz);
                dw.images.push_back(std::move(im));
            }
        }
    } catch (const std::exception& ex) {
        dw.warnings.push_back(std::string("read stopped at offset ") + std::to_string(ar.pos()) + ": " + ex.what());
    }
    dw.unicode_strings = ar.unicode_seen;
    return dw;
}

Drawing read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return read_buffer(d);
}

Pt paper_size(uint32_t code) {
    switch (code) {
        case 0: return {1189, 841};
        case 1: return {841, 594};
        case 2: return {594, 420};
        case 3: return {420, 297};
        case 4: return {297, 210};
        case 8: return {1682, 1189};
        case 9: return {2378, 1682};
        case 10: return {3364, 2378};
        case 11: return {4756, 3364};
        case 12: return {10000, 7071};
        case 13: return {50000, 35355};
        case 14: return {100000, 70711};
        default: return {420, 297};
    }
}

}  // namespace jww
