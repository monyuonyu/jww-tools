// DXF writer for jww-tools
// SPDX-License-Identifier: MIT
#include "jww.hpp"
#include "output.hpp"
#include "image.hpp"

#include <filesystem>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace jww {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Jw_cad standard pen colours 1..9 -> AutoCAD Color Index.
// 1 水色, 2 黒(白), 3 緑, 4 黄, 5 ピンク, 6 青, 7 深緑/シアン, 8 赤, 9 灰(補助線色)
const int kPenAci[10] = {7, 4, 7, 3, 2, 6, 5, 4, 1, 8};

// Jw_cad standard line types 1..9 and their DXF names. Patterns are in paper millimetres
// (positive = dash, negative = gap, 0 = dot) taken from Jw_cad's own DXF export.
struct LtDef { const char* name; const char* desc; std::vector<double> pat; };
const LtDef kLt[] = {
    {"Continuous", "Solid line", {}},
    {"DASHED1", "--  --  --  --  --  --  --  --", {1.25, -1.25}},                 // 2 点線1
    {"DASHED2", "----    ----    ----    ----", {2.5, -2.5}},                       // 3 点線2
    {"DASHED3", "------  ------  ------  ------", {3.75, -1.25}},                   // 4 点線3
    {"CENTER1", "----- - ----- - ----- - ----- -", {6.25, -1.25, 1.25, -1.25}},   // 5 一点鎖1
    {"CENTER2", "----------  --  ----------  --", {12.5, -2.5, 2.5, -2.5}},       // 6 一点鎖2
    {"PHANTOM1", "--- - - --- - - --- - - --- - -", {3.25, -1.25, 1.25, -1.25, 1.25, -1.25}},  // 7 二点鎖1
    {"PHANTOM2", "--------  -  -  --------  -  -", {10.0, -2.5, 1.25, -2.5, 1.25, -2.5}},      // 8 二点鎖2
    {"DOT", "-   -   -   -   -   -   -   -", {0.625, -1.875}},                       // 9 補助線/点線
};
const char* ltype_name(int style) {
    if (style >= 2 && style <= 9) return kLt[style - 1].name;
    if (style >= 11 && style <= 15) { static const char* r[] = {"RAND1", "RAND2", "RAND3", "RAND4", "RAND5"}; return r[style - 11]; }
    if (style >= 16 && style <= 19) { static const char* l[] = {"LONG1", "LONG2", "LONG3", "LONG4"}; return l[style - 16]; }
    return "Continuous";
}
std::vector<double> ltype_pattern(int style) {
    if (style >= 2 && style <= 9) return kLt[style - 1].pat;
    if (style >= 11 && style <= 15) { double k = style - 11; return {1 + k, -(2 + k), 3 + k, -(4 + k)}; }
    switch (style) {
        case 16: return {32.5, -2.5, 2.5, -2.5};
        case 17: return {30.0, -2.5, 1.25, -2.5, 1.25, -2.5};
        case 18: return {37.5, -2.5};
        case 19: return {75.0, -5.0};
    }
    return {};
}

// Line weight (1/100 mm) for a pen. Jw_cad's printer widths are in 1/100 mm when the
// drawing uses "線幅を1/100mm単位とする"; otherwise they are dot counts.
int lineweight(const Drawing& dw, const Base& b) {
    static const int valid[] = {0, 5, 9, 13, 15, 18, 20, 25, 30, 35, 40, 50, 53, 60, 70, 80, 90, 100, 106, 120, 140, 158, 200, 211};
    int w = b.pen_width;
    if (w <= 0) {
        auto it = dw.pens.find(b.pen_color);
        w = it != dw.pens.end() ? int(it->second.prt_width) : 1;
        if (w < 100) w = w * 13 + 5;  // rough: dot widths -> 0.18, 0.31, ...
    }
    int best = 0;
    for (int v : valid) if (std::abs(v - w) < std::abs(best - w)) best = v;
    return best;
}

std::string sanitize_layer(std::string s) {
    for (char& c : s)
        if (std::strchr("<>/\\\":;?*|=`,", c)) c = '_';
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

class Writer {
public:
    Writer(std::FILE* f, const Drawing& dw, const DxfOptions& o) : f_(f), dw_(dw), o_(o) {}

    void run();

private:
    std::FILE* f_;
    const Drawing& dw_;
    const DxfOptions& o_;
    unsigned handle_ = 0x30;  // 0x1..0x2F are reserved for fixed objects
    bool r12() const { return o_.version == DxfOptions::R12; }
    bool utf8() const { return o_.version == DxfOptions::R2007; }

    // ---- low level -------------------------------------------------------
    std::string* out_ = nullptr;
    void put(int code, const char* v) {
        char b[8];
        std::snprintf(b, sizeof b, "%3d\n", code);
        *out_ += b; *out_ += v; *out_ += '\n';
    }
    void g(int code, const std::string& v) { put(code, v.c_str()); }
    void g(int code, int v) { put(code, std::to_string(v).c_str()); }
    void gd(int code, double v) {
        if (!std::isfinite(v)) v = 0;
        char buf[40];
        std::snprintf(buf, sizeof buf, "%.12g", v);
        if (!std::strpbrk(buf, ".eEn")) std::strcat(buf, ".0");
        put(code, buf);
    }
    void gs_raw(int code, const std::string& utf) { g(code, enc(utf)); }
    void gs(int code, const std::string& utf) {  // text value: a literal '^' is written as "^ "
        std::string t;
        for (char ch : utf) { t += ch; if (ch == '^') t += ' '; }
        g(code, enc(t));
    }
    std::string enc(const std::string& utf) const {
        std::string s;
        for (char c : utf) {
            if (c == '\r') continue;
            if (c == '\n') { s += ' '; continue; }
            s += c;
        }
        return utf8() ? s : utf8_to_sjis(s);
    }
    std::string hnd() { char b[16]; std::snprintf(b, sizeof b, "%X", handle_++); return b; }
    void handle(const std::string& owner = "") {
        if (r12()) return;
        g(5, hnd());
        if (!owner.empty()) g(330, owner);
    }
    void sub(const char* m) { if (!r12()) g(100, m); }

    // ---- geometry helpers ------------------------------------------------
    struct Ctx {
        double s = 1;           // layer group scale (paper mm -> real mm)
        double ox = 0, oy = 0;  // origin shift in paper mm (0 inside block definitions)
        std::string owner;      // owning block record handle
        bool in_block = false;
        int layer_key = -1;     // members of a block take the layer of the outermost INSERT
    };
    static double X(const Ctx& c, double x) { return (x + c.ox) * c.s; }
    static double Y(const Ctx& c, double y) { return (y + c.oy) * c.s; }
    double ox() const { return o_.origin_center || dw_.paper == kNoPaper ? 0 : paper_size(dw_.paper).x / 2; }
    double oy() const { return o_.origin_center || dw_.paper == kNoPaper ? 0 : paper_size(dw_.paper).x / 2 / std::sqrt(2.0); }
    double scale_of(const Base& b) const {
        double s = dw_.groups[(o_.group_scale ? b.glayer : dw_.write_glayer) & 15].scale;
        if (o_.paper_units || !(s > 0)) return 1;
        return s;
    }

    std::map<int, std::string> layer_names_;
    std::set<int> used_layers_;
    const Ctx* cur_ctx_ = nullptr;
    std::string layer(const Base& b) {
        if (cur_ctx_ && cur_ctx_->in_block) return "0";
        int key = (b.glayer & 15) * 16 + (b.layer & 15);
        if (cur_ctx_ && cur_ctx_->layer_key >= 0) key = cur_ctx_->layer_key;
        used_layers_.insert(key);
        return layer_name(key);
    }
    std::string layer_name(int key) {
        auto it = layer_names_.find(key);
        if (it != layer_names_.end()) return it->second;
        int gi = key / 16, li = key % 16;
        char buf[8];
        std::snprintf(buf, sizeof buf, "%X%X", gi, li);
        std::string n = buf;
        const auto& G = dw_.groups[gi];
        if (!G.name.empty()) n += "_" + G.name;
        if (!G.layers[li].name.empty()) n += "_" + G.layers[li].name;
        n = sanitize_layer(n);
        layer_names_[key] = n;
        return n;
    }

    // colour of a figure: ACI and optional true colour
    void color(const Base& b, uint32_t rgb = 0, bool has_rgb = false) {
        int c = b.pen_color;
        if (has_rgb) {
            uint32_t r = rgb & 0xFF, gg = (rgb >> 8) & 0xFF, bl = (rgb >> 16) & 0xFF;
            g(62, nearest_aci(r, gg, bl));
            if (!r12()) g(420, int((r << 16) | (gg << 8) | bl));
            return;
        }
        if (c >= 1 && c <= 9) { g(62, kPenAci[c]); return; }
        if (c >= 100) {
            auto it = dw_.pens.find(c);
            if (it != dw_.pens.end()) {
                uint32_t col = it->second.prt_color;
                uint32_t r = col & 0xFF, gg = (col >> 8) & 0xFF, bl = (col >> 16) & 0xFF;
                g(62, nearest_aci(r, gg, bl));
                if (!r12()) g(420, int((r << 16) | (gg << 8) | bl));
                return;
            }
        }
        g(62, 7);
    }
    static int nearest_aci(uint32_t r, uint32_t gg, uint32_t b) {
        static const uint8_t base[][3] = {{255, 0, 0}, {255, 255, 0}, {0, 255, 0}, {0, 255, 255}, {0, 0, 255}, {255, 0, 255}, {255, 255, 255}, {128, 128, 128}, {192, 192, 192}};
        static const int idx[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
        int best = 7;
        long bd = 1L << 40;
        for (int i = 0; i < 9; i++) {
            long dr = long(r) - base[i][0], dg = long(gg) - base[i][1], db = long(b) - base[i][2];
            long d = dr * dr + dg * dg + db * db;
            if (d < bd) { bd = d; best = idx[i]; }
        }
        if (r + gg + b == 0) return 7;  // black on white paper: AutoCAD shows 7 as black on white
        return best;
    }
    void common(const char* type, const Base& b, const Ctx& c, bool with_ltype = true, uint32_t rgb = 0, bool has_rgb = false) {
        g(0, type);
        handle(c.owner);
        sub("AcDbEntity");
        g(8, enc(layer(b)));
        if (with_ltype && b.pen_style != 1 && b.pen_style < 100) {
            g(6, ltype_name(b.pen_style));
            used_ltypes_.insert(b.pen_style);
            double ws = dw_.groups[dw_.write_glayer & 15].scale;
            if (!o_.paper_units && ws > 0 && c.s != ws) gd(48, c.s / ws);
        }
        color(b, rgb, has_rgb);
        if (!r12() && with_ltype) g(370, lineweight(dw_, b));
    }

    // ---- entities --------------------------------------------------------
    void line(const Entity& e, const Ctx& c) {
        common("LINE", e.b, c);
        sub("AcDbLine");
        gd(10, X(c, e.p[0].x)); gd(20, Y(c, e.p[0].y)); gd(30, 0);
        gd(11, X(c, e.p[1].x)); gd(21, Y(c, e.p[1].y)); gd(31, 0);
    }
    void arc(const Entity& e, const Ctx& c) {
        double cx = X(c, e.p[0].x), cy = Y(c, e.p[0].y), r = e.r * c.s;
        bool full = e.full || std::fabs(e.sweep) >= 2 * kPi - 1e-12;
        if (std::fabs(e.flat - 1.0) < 1e-9) {
            if (full) {
                common("CIRCLE", e.b, c);
                sub("AcDbCircle");
                gd(10, cx); gd(20, cy); gd(30, 0); gd(40, r);
                return;
            }
            double a0 = e.start + e.tilt, a1 = a0 + e.sweep;
            if (e.sweep < 0) std::swap(a0, a1);
            common("ARC", e.b, c);
            sub("AcDbCircle");
            gd(10, cx); gd(20, cy); gd(30, 0); gd(40, r);
            sub("AcDbArc");
            gd(50, norm_deg(a0 * 180 / kPi)); gd(51, norm_deg(a1 * 180 / kPi));
            return;
        }
        // Ellipse: radius along the tilt direction, the other axis = radius * flat.
        double major = r, ratio = e.flat, rot = e.tilt;
        double p0 = full ? 0 : e.start, p1 = full ? 2 * kPi : e.start + e.sweep;
        if (e.sweep < 0 && !full) std::swap(p0, p1);
        if (ratio > 1) {
            major = r * ratio;
            ratio = 1 / ratio;
            rot += kPi / 2;
            p0 -= kPi / 2;
            p1 -= kPi / 2;
        }
        if (r12()) {  // no ELLIPSE in R12: polyline approximation
            ellipse_poly(e, c, cx, cy, major, ratio, rot, p0, p1, full);
            return;
        }
        common("ELLIPSE", e.b, c);
        sub("AcDbEllipse");
        gd(10, cx); gd(20, cy); gd(30, 0);
        gd(11, major * std::cos(rot)); gd(21, major * std::sin(rot)); gd(31, 0);
        gd(210, 0); gd(220, 0); gd(230, 1);
        gd(40, std::fabs(ratio));
        gd(41, full ? 0 : norm_rad(p0));
        double e1 = full ? 2 * kPi : norm_rad(p1);
        if (!full && e1 <= norm_rad(p0)) e1 += 2 * kPi;
        gd(42, e1);
    }
    void ellipse_poly(const Entity& e, const Ctx& c, double cx, double cy, double a, double ratio, double rot, double p0, double p1, bool full) {
        int n = 72;
        common("POLYLINE", e.b, c);
        g(66, 1);
        gd(10, 0); gd(20, 0); gd(30, 0);
        g(70, full ? 1 : 0);
        for (int i = 0; i <= (full ? n - 1 : n); i++) {
            double t = p0 + (p1 - p0) * i / n;
            double x = a * std::cos(t), y = a * ratio * std::sin(t);
            g(0, "VERTEX");
            g(8, enc(layer(e.b)));
            gd(10, cx + x * std::cos(rot) - y * std::sin(rot));
            gd(20, cy + x * std::sin(rot) + y * std::cos(rot));
            gd(30, 0);
        }
        g(0, "SEQEND");
        g(8, enc(layer(e.b)));
    }
    static double norm_deg(double a) { a = std::fmod(a, 360.0); if (a < 0) a += 360; if (a >= 360 - 1e-12) a = 0; return a; }
    static double norm_rad(double a) { a = std::fmod(a, 2 * kPi); if (a < 0) a += 2 * kPi; return a; }

    void point(const Entity& e, const Ctx& c) {
        if (e.temporary && o_.skip_temporary_points) return;
        common("POINT", e.b, c, false);
        sub("AcDbPoint");
        gd(10, X(c, e.p[0].x)); gd(20, Y(c, e.p[0].y)); gd(30, 0);
    }

    std::map<std::string, std::string> styles_;  // font -> style name
    std::string style_for(const Entity& e) {
        std::string font = e.font.empty() ? "ＭＳ ゴシック" : e.font;
        bool vertical = (e.b.flag & 0x20) != 0;
        std::string key = font + (vertical ? "\x01" : "");
        auto it = styles_.find(key);
        if (it != styles_.end()) return it->second;
        std::string name = sanitize_layer(font + (vertical ? "_縦" : ""));
        styles_[key] = name;
        return name;
    }
    void text(const Entity& e, const Ctx& c) {
        if (e.str.empty()) return;
        if (e.str.rfind("^@BM", 0) == 0) { image(e, c); return; }
        // Jw_cad pads strings with trailing "・" to spread them between the start and end
        // points (均等割付); like Jw_cad's own DXF export, write the rest as a "Fit" text.
        std::string str = e.str;
        static const std::string dot = "\xE3\x83\xBB";  // U+30FB KATAKANA MIDDLE DOT
        bool fit = false;
        while (str.size() > dot.size() && str.compare(str.size() - dot.size(), dot.size(), dot) == 0) {
            str.erase(str.size() - dot.size());
            fit = true;
        }
        if (fit) {
            bool blank = true;
            for (size_t i = 0; i < str.size();) {
                if (str[i] == ' ') { i++; continue; }
                if (str.compare(i, 3, "\xE3\x80\x80") == 0) { i += 3; continue; }  // U+3000
                blank = false;
                break;
            }
            if (blank || (e.p[0].x == e.p[1].x && e.p[0].y == e.p[1].y)) { str = e.str; fit = false; }
        }
        double h = e.size_y * c.s * o_.text_height_factor;
        double w = e.size_y != 0 ? std::fabs(e.size_x / e.size_y) : 1;
        common("TEXT", e.b, c, false);
        sub("AcDbText");
        gd(10, X(c, e.p[0].x)); gd(20, Y(c, e.p[0].y)); gd(30, 0);
        gd(40, h > 0 ? h : 1);
        gs(1, str);
        double ang = norm_deg(e.angle);
        if (ang != 0) gd(50, ang);
        if (std::fabs(w - 1) > 1e-9) gd(41, w);
        if ((e.text_type / 10000) & 1) gd(51, 15);  // italic (斜体: +10000)
        g(7, enc(style_for(e)));
        if (fit) {
            g(72, 5);
            gd(11, X(c, e.p[1].x)); gd(21, Y(c, e.p[1].y)); gd(31, 0);
        }
        sub("AcDbText");
    }
    struct ImageRef { std::string img, reactor, def, owner; };
    struct ImageDef { std::string handle, path; int w = 0, h = 0; std::vector<std::string> reactors; };
    std::vector<ImageRef> image_refs_;
    std::map<std::string, ImageDef> image_defs_;  // by path
    std::vector<std::string> image_order_;

    static std::vector<std::string> split(const std::string& s, char sep) {
        std::vector<std::string> v(1);
        for (char ch : s) { if (ch == sep) v.emplace_back(); else v.back() += ch; }
        return v;
    }
    static bool read_file(const std::string& path, std::vector<uint8_t>& d) {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return false;
        uint8_t buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
        std::fclose(f);
        return true;
    }
    // "^@BM<file>,<width mm>[,<height mm>,...]" placed at the text's start point and angle.
    void image(const Entity& e, const Ctx& c) {
        if (!o_.images || r12()) return;
        auto f = split(e.str.substr(4), ',');
        if (f.empty() || f[0].empty()) return;
        std::string name = f[0];
        double wmm = f.size() > 1 ? std::atof(f[1].c_str()) : 0;
        double hmm = f.size() > 2 ? std::atof(f[2].c_str()) : 0;
        std::string ref = name;
        std::vector<uint8_t> data;
        if (name.rfind("%temp%", 0) == 0) {  // embedded in the drawing (Ver.7.00+)
            std::string base = name.substr(6);
            for (const auto& im : dw_.images) {
                if (im.name == base || im.name == base + ".gz") {
                    try { data = im.name == base ? im.data : gunzip(im.data); } catch (const std::exception&) { data.clear(); }
                    break;
                }
            }
            ref = o_.image_ref_prefix + base;
            if (!data.empty() && !o_.image_dir.empty() && !written_.count(base)) {
                written_.insert(base);
                std::error_code ec;
                std::filesystem::create_directories(std::filesystem::u8path(o_.image_dir), ec);
                std::FILE* out = std::fopen((std::filesystem::u8path(o_.image_dir) / std::filesystem::u8path(base)).string().c_str(), "wb");
                if (out) { std::fwrite(data.data(), 1, data.size(), out); std::fclose(out); }
            }
        } else {
            std::string p = name;
            for (char& ch : p) if (ch == '\\') ch = '/';
            if (p.rfind("./", 0) == 0) p = p.substr(2);
            bool abs = p.size() > 1 && (p[0] == '/' || p[1] == ':');
            std::string local = abs ? p : (o_.source_dir.empty() ? p : o_.source_dir + "/" + p);
            read_file(std::filesystem::u8path(local).string(), data);
        }
        int pw = 0, ph = 0;
        if (data.empty() || !image_size(data, pw, ph)) {
            pw = 1000;
            ph = wmm > 0 && hmm > 0 ? std::max(1, int(std::lround(1000 * hmm / wmm))) : 1000;
        }
        if (wmm <= 0) wmm = 100;
        if (hmm <= 0) hmm = wmm * ph / pw;
        ImageDef& d = image_defs_[ref];
        if (d.handle.empty()) { d.handle = hnd(); d.path = ref; d.w = pw; d.h = ph; image_order_.push_back(ref); }
        ImageRef r;
        r.reactor = hnd();
        d.reactors.push_back(r.reactor);
        double a = e.angle * kPi / 180;
        double ux = wmm * c.s / pw, vy = hmm * c.s / ph;
        g(0, "IMAGE");
        r.img = hnd();
        g(5, r.img);
        g(330, c.owner);
        sub("AcDbEntity");
        g(8, enc(layer(e.b)));
        sub("AcDbRasterImage");
        g(90, 0);
        gd(10, X(c, e.p[0].x)); gd(20, Y(c, e.p[0].y)); gd(30, 0);
        gd(11, ux * std::cos(a)); gd(21, ux * std::sin(a)); gd(31, 0);
        gd(12, -vy * std::sin(a)); gd(22, vy * std::cos(a)); gd(32, 0);
        gd(13, pw); gd(23, ph);
        g(340, d.handle);
        g(70, 7); g(280, 0); g(281, 50); g(282, 50); g(283, 0);
        g(360, r.reactor);
        g(71, 1); g(91, 2);
        gd(14, -0.5); gd(24, -0.5); gd(14, pw - 0.5); gd(24, ph - 0.5);
        image_refs_.push_back(r);
    }
    std::set<std::string> written_;

    void solid_quad(const Base& b, const Ctx& c, Pt a, Pt bb, Pt cc, Pt d, uint32_t rgb, bool has_rgb) {
        // polygon order a, bb, cc, d -> DXF SOLID 10=a 11=bb 12=d 13=cc
        common("SOLID", b, c, false, rgb, has_rgb);
        sub("AcDbTrace");
        gd(10, X(c, a.x)); gd(20, Y(c, a.y)); gd(30, 0);
        gd(11, X(c, bb.x)); gd(21, Y(c, bb.y)); gd(31, 0);
        gd(12, X(c, d.x)); gd(22, Y(c, d.y)); gd(32, 0);
        gd(13, X(c, cc.x)); gd(23, Y(c, cc.y)); gd(33, 0);
    }
    struct HV { double x, y, b; };
    // vertices of an (elliptic) arc from t0 to t1; circular arcs use bulges
    std::vector<HV> arc_verts(Pt ctr, double ra, double rb, double tilt, double t0, double t1, bool include_end) {
        std::vector<HV> v;
        auto at = [&](double t) {
            double x = ra * std::cos(t), y = rb * std::sin(t);
            return Pt{ctr.x + x * std::cos(tilt) - y * std::sin(tilt), ctr.y + x * std::sin(tilt) + y * std::cos(tilt)};
        };
        double sw = t1 - t0;
        if (std::fabs(ra - rb) <= 1e-12 * std::max(1.0, ra)) {
            int k = std::max(1, int(std::ceil(std::fabs(sw) / (kPi / 2) - 1e-9)));
            double b = std::tan(sw / k / 4);
            for (int i = 0; i < k; i++) { Pt q = at(t0 + sw * i / k); v.push_back({q.x, q.y, b}); }
        } else {
            double rmax = std::max(std::fabs(ra), std::fabs(rb));
            double step = rmax > 0 ? 2 * std::acos(1 - std::min(0.005 / rmax, 2e-4)) : kPi / 4;
            int n = std::max(8, std::min(720, int(std::ceil(std::fabs(sw) / std::max(step, 2 * kPi / 720)))));
            for (int i = 0; i < n; i++) { Pt q = at(t0 + sw * i / n); v.push_back({q.x, q.y, 0}); }
        }
        if (include_end) { Pt q = at(t1); v.push_back({q.x, q.y, 0}); }
        return v;
    }
    void hatch(const Entity& e, const Ctx& c, bool has_rgb) {
        std::vector<std::vector<HV>> paths;
        if (e.b.pen_style < 101) {
            std::vector<HV> v;
            for (const Pt& q : e.p) v.push_back({q.x, q.y, 0});
            paths.push_back(v);
        } else {
            Pt ctr = e.p[0];
            double r = e.p[1].x, flat = e.p[1].y, tilt = e.p[2].x, st = e.p[2].y, sw = e.p[3].x, kind = e.p[3].y;
            int style = e.b.pen_style;
            bool full = std::fabs(sw) >= 2 * kPi - 1e-9 || (style == 101 && kind == 100);
            if (full) { st = 0; sw = 2 * kPi; }
            if (style == 105 || style == 106) {
                double ri = kind, ra2 = ri, rb2 = ri * flat;
                if (style == 106) { ra2 = ri; rb2 = r * flat - (r - ri); }
                if (full) {
                    paths.push_back(arc_verts(ctr, r, r * flat, tilt, 0, 2 * kPi, false));
                    paths.push_back(arc_verts(ctr, ra2, rb2, tilt, 0, 2 * kPi, false));
                } else {
                    auto v = arc_verts(ctr, r, r * flat, tilt, st, st + sw, true);
                    auto w = arc_verts(ctr, ra2, rb2, tilt, st + sw, st, true);
                    v.insert(v.end(), w.begin(), w.end());
                    paths.push_back(v);
                }
            } else if (full) {
                paths.push_back(arc_verts(ctr, r, r * flat, tilt, 0, 2 * kPi, false));
            } else {
                auto v = arc_verts(ctr, r, r * flat, tilt, st, st + sw, true);
                if (kind == 0) v.insert(v.begin(), HV{ctr.x, ctr.y, 0});
                else if (kind == -1) {
                    double half = sw / 2, rr = r / std::cos(half);
                    double x = rr * std::cos(st + half), y = rr * flat * std::sin(st + half);
                    v.insert(v.begin(), HV{ctr.x + x * std::cos(tilt) - y * std::sin(tilt), ctr.y + x * std::sin(tilt) + y * std::cos(tilt), 0});
                }
                paths.push_back(v);
            }
        }
        common("HATCH", e.b, c, false, e.color, has_rgb);
        sub("AcDbHatch");
        gd(10, 0); gd(20, 0); gd(30, 0);
        gd(210, 0); gd(220, 0); gd(230, 1);
        g(2, "SOLID"); g(70, 1); g(71, 0);
        g(91, int(paths.size()));
        for (size_t k = 0; k < paths.size(); k++) {
            const auto& v = paths[k];
            bool bulges = false;
            for (auto& q : v) bulges |= q.b != 0;
            g(92, k == 0 ? 3 : 2);  // polyline (+ external for the first)
            g(72, bulges ? 1 : 0); g(73, 1); g(93, int(v.size()));
            for (auto& q : v) { gd(10, X(c, q.x)); gd(20, Y(c, q.y)); if (bulges) gd(42, q.b); }
            g(97, 0);
        }
        g(75, 0); g(76, 1); g(98, 0);
    }
    void solid(const Entity& e, const Ctx& c) {
        bool has_rgb = e.b.pen_color == 10;
        if (e.b.pen_style < 101) {
            // A solid without area is drawn by Jw_cad as a line of its colour.
            std::vector<Pt> v;
            for (const Pt& q : e.p)
                if (v.empty() || q.x != v.back().x || q.y != v.back().y) v.push_back(q);
            if (v.size() > 1 && v.front().x == v.back().x && v.front().y == v.back().y) v.pop_back();
            if (v.size() == 2) {
                common("LINE", e.b, c, true, e.color, has_rgb);
                sub("AcDbLine");
                gd(10, X(c, v[0].x)); gd(20, Y(c, v[0].y)); gd(30, 0);
                gd(11, X(c, v[1].x)); gd(21, Y(c, v[1].y)); gd(31, 0);
                return;
            }
            if (o_.hatch) { hatch(e, c, has_rgb); return; }
            solid_quad(e.b, c, e.p[0], e.p[1], e.p[2], e.p[3], e.color, has_rgb);
            return;
        }
        if (o_.hatch && e.b.pen_style != 111) { hatch(e, c, has_rgb); return; }
        // arc solids
        Pt ctr = e.p[0];
        double r = e.p[1].x, flat = e.p[1].y, tilt = e.p[2].x, st = e.p[2].y, sw = e.p[3].x, kind = e.p[3].y;
        auto at = [&](double rr, double t) {
            double x = rr * std::cos(t), y = rr * flat * std::sin(t);
            return Pt{ctr.x + x * std::cos(tilt) - y * std::sin(tilt), ctr.y + x * std::sin(tilt) + y * std::cos(tilt)};
        };
        int style = e.b.pen_style;
        bool fullc = std::fabs(sw) >= 2 * kPi - 1e-9 || (style == 101 && kind == 100) || (style == 111 && kind == 100);
        if (fullc) { st = 0; sw = 2 * kPi; }
        // segments: chord error below 0.005 paper mm and 0.02% of the radius (at most 720 per turn)
        double rmax = std::max(r, r * flat);
        double step = rmax > 0 ? 2 * std::acos(1 - std::min(0.005 / rmax, 2e-4)) : kPi / 4;
        int n = std::max(8, std::min(720, int(std::ceil(std::fabs(sw) / std::max(step, 2 * kPi / 720)))));
        if (style == 101) {
            if (kind == 0 || kind == 100) {  // sector / full circle
                for (int i = 0; i < n; i++) {
                    Pt a = at(r, st + sw * i / n), b2 = at(r, st + sw * (i + 1) / n);
                    solid_quad(e.b, c, ctr, a, b2, b2, e.color, has_rgb);
                }
            } else if (kind == 5) {  // segment (弓形): fan from the chord start
                Pt s0 = at(r, st);
                for (int i = 1; i < n; i++) {
                    Pt a = at(r, st + sw * i / n), b2 = at(r, st + sw * (i + 1) / n);
                    solid_quad(e.b, c, s0, a, b2, b2, e.color, has_rgb);
                }
            } else {  // -1: outside of the arc (between the arc and the corner of its tangents)
                Pt p0 = at(r, st), p1 = at(r, st + sw);
                // corner = intersection of the tangents at both ends
                double half = sw / 2;
                double rr = r / std::cos(half);
                Pt corner = at(rr, st + half);
                for (int i = 0; i < n; i++) {
                    Pt a = at(r, st + sw * i / n), b2 = at(r, st + sw * (i + 1) / n);
                    solid_quad(e.b, c, corner, a, b2, b2, e.color, has_rgb);
                }
                (void)p0; (void)p1;
            }
        } else if (style == 105 || style == 106) {  // ring
            double ri = kind;
            for (int i = 0; i < n; i++) {
                double t0 = st + sw * i / n, t1 = st + sw * (i + 1) / n;
                Pt a = at(r, t0), b2 = at(r, t1);
                Pt ia, ib;
                if (style == 105) {
                    double k = r != 0 ? ri / r : 0;
                    ia = Pt{ctr.x + (a.x - ctr.x) * k, ctr.y + (a.y - ctr.y) * k};
                    ib = Pt{ctr.x + (b2.x - ctr.x) * k, ctr.y + (b2.y - ctr.y) * k};
                } else {
                    double d = r - ri;
                    auto atd = [&](double t) {
                        double x = (r - d) * std::cos(t), y = (r * flat - d) * std::sin(t);
                        return Pt{ctr.x + x * std::cos(tilt) - y * std::sin(tilt), ctr.y + x * std::sin(tilt) + y * std::cos(tilt)};
                    };
                    ia = atd(t0); ib = atd(t1);
                }
                solid_quad(e.b, c, a, b2, ib, ia, e.color, has_rgb);
            }
        } else if (style == 111) {  // circumference band: draw as a thick-less ring of width 0 -> polyline
            for (int i = 0; i < n; i++) {
                Pt a = at(r, st + sw * i / n), b2 = at(r, st + sw * (i + 1) / n);
                Entity l;
                l.b = e.b; l.b.pen_style = 1;
                l.p[0] = a; l.p[1] = b2;
                line(l, c);
            }
        } else {
            for (int i = 0; i < n; i++) {
                Pt a = at(r, st + sw * i / n), b2 = at(r, st + sw * (i + 1) / n);
                solid_quad(e.b, c, ctr, a, b2, b2, e.color, has_rgb);
            }
        }
    }

    // blocks: one DXF block per (definition, scale) pair
    std::map<std::pair<uint32_t, double>, std::string> block_names_;
    std::map<uint32_t, const BlockDef*> defs_;
    std::string block_name(uint32_t num, double s) {
        auto k = std::make_pair(num, s);
        auto it = block_names_.find(k);
        if (it != block_names_.end()) return it->second;
        auto d = defs_.find(num);
        std::string base = d != defs_.end() && !d->second->name.empty() ? sanitize_layer(d->second->name) : "JWBLOCK" + std::to_string(num);
        std::string n = base;
        int dup = 1;
        for (bool clash = true; clash;) {
            clash = false;
            for (auto& [kk, v] : block_names_)
                if (v == n) { clash = true; n = base + "_" + std::to_string(++dup); break; }
        }
        block_names_[k] = n;
        pending_.push_back(k);
        return n;
    }
    std::vector<std::pair<uint32_t, double>> pending_;

    void insert(const Entity& e, const Ctx& c, int depth = 0) {
        if (!defs_.count(e.number)) return;
        // Jw_cad keeps text readable in mirrored blocks; a mirrored DXF INSERT would mirror it.
        if (o_.explode_blocks || (e.sx * e.sy < 0 && has_text(e.number, 0))) {
            explode(e, c, depth);
            return;
        }
        std::string name = block_name(e.number, c.s);
        common("INSERT", e.b, c, false);
        sub("AcDbBlockReference");
        g(2, enc(name));
        gd(10, X(c, e.p[0].x)); gd(20, Y(c, e.p[0].y)); gd(30, 0);
        if (e.sx != 1) gd(41, e.sx);
        if (e.sy != 1) gd(42, e.sy);
        if (e.angle != 0) gd(50, norm_deg(e.angle * 180 / kPi));
    }

    bool has_text(uint32_t num, int depth) {
        auto it = defs_.find(num);
        if (it == defs_.end() || depth > 32) return false;
        for (const auto& e : it->second->ents) {
            if (e.kind == Kind::Text || e.kind == Kind::Dim) return true;
            if (e.kind == Kind::Block && has_text(e.number, depth + 1)) return true;
        }
        return false;
    }
    // Exploding: transform each figure of the definition.
    struct Xf {
        double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;  // x' = a x + b y + tx
        Pt ap(Pt p) const { return {a * p.x + b * p.y + tx, c * p.x + d * p.y + ty}; }
    };
    void explode(const Entity& ins, const Ctx& c, int depth) {
        if (depth > 32) return;
        Xf x;
        double ca = std::cos(ins.angle), sa = std::sin(ins.angle);
        x.a = ca * ins.sx; x.b = -sa * ins.sy; x.c = sa * ins.sx; x.d = ca * ins.sy;
        x.tx = ins.p[0].x; x.ty = ins.p[0].y;
        Ctx c2 = c;
        if (c2.layer_key < 0 && !c2.in_block) c2.layer_key = (ins.b.glayer & 15) * 16 + (ins.b.layer & 15);
        explode_with(*defs_[ins.number], x, c2, depth);
    }
    void explode_with(const BlockDef& bd, const Xf& x, const Ctx& c, int depth) {
        for (const Entity& e0 : bd.ents) {
            Entity e = e0;
            if (e.kind == Kind::Block) {
                Xf y;
                double ca = std::cos(e.angle), sa = std::sin(e.angle);
                Xf inner;
                inner.a = ca * e.sx; inner.b = -sa * e.sy; inner.c = sa * e.sx; inner.d = ca * e.sy;
                inner.tx = e.p[0].x; inner.ty = e.p[0].y;
                y.a = x.a * inner.a + x.b * inner.c; y.b = x.a * inner.b + x.b * inner.d;
                y.c = x.c * inner.a + x.d * inner.c; y.d = x.c * inner.b + x.d * inner.d;
                Pt t = x.ap({inner.tx, inner.ty});
                y.tx = t.x; y.ty = t.y;
                if (defs_.count(e.number) && depth < 32) explode_with(*defs_[e.number], y, c, depth + 1);
                continue;
            }
            transform(e, x);
            emit(e, c, depth + 1);
        }
    }
    void transform(Entity& e, const Xf& x) {
        double sx = std::hypot(x.a, x.c), sy = std::hypot(x.b, x.d);
        double rot = std::atan2(x.c, x.a);
        bool mirror = x.a * x.d - x.b * x.c < 0;
        switch (e.kind) {
            case Kind::Line: e.p[0] = x.ap(e.p[0]); e.p[1] = x.ap(e.p[1]); break;
            case Kind::Point: e.p[0] = x.ap(e.p[0]); break;
            case Kind::Text:
                e.p[0] = x.ap(e.p[0]); e.p[1] = x.ap(e.p[1]);
                e.size_x *= sx; e.size_y *= sy;
                {   // Jw_cad keeps text readable: the baseline direction is transformed, the glyphs are not mirrored
                    double t = e.angle * kPi / 180, cx_ = std::cos(t), sy_ = std::sin(t);
                    e.angle = std::atan2(x.c * cx_ + x.d * sy_, x.a * cx_ + x.b * sy_) * 180 / kPi;
                }
                break;
            case Kind::Solid:
                if (e.b.pen_style >= 101) {
                    e.p[0] = x.ap(e.p[0]);
                    e.p[1].x *= sx;
                    if (sx != 0) e.p[1].y *= sy / sx;
                    e.p[2].x += rot;
                    if (mirror) { e.p[2].x = -e.p[2].x + 2 * rot; e.p[2].y = -e.p[2].y - e.p[3].x; }
                    if (e.b.pen_style == 105 || e.b.pen_style == 106) e.p[3].y *= sx;
                } else
                    for (auto& p : e.p) p = x.ap(p);
                break;
            case Kind::Arc: {
                e.p[0] = x.ap(e.p[0]);
                if (std::fabs(sx - sy) < 1e-12 * std::max(1.0, sx)) {
                    e.r *= sx;
                    e.tilt += rot;
                    if (mirror) { e.tilt = 2 * rot - e.tilt; e.start = -e.start - e.sweep; }
                } else {
                    // non-uniform scaling of an axis-aligned (in block space) arc
                    e.r *= sx;
                    e.flat *= sx != 0 ? sy / sx : 1;
                    e.tilt += rot;
                }
                break;
            }
            case Kind::Dim:
                for (auto& s : e.sub) transform(s, x);
                break;
            case Kind::Block: break;
        }
    }

    void emit(const Entity& e, const Ctx& c, int depth = 0) {
        const Ctx* saved = cur_ctx_;
        cur_ctx_ = &c;
        emit1(e, c, depth);
        cur_ctx_ = saved;
    }
    void emit1(const Entity& e, const Ctx& c, int depth) {
        switch (e.kind) {
            case Kind::Line: line(e, c); break;
            case Kind::Arc: arc(e, c); break;
            case Kind::Point: point(e, c); break;
            case Kind::Text: if (!is_setting_text(e)) text(e, c); break;
            case Kind::Solid: solid(e, c); break;
            case Kind::Block: insert(e, c, depth); break;
            case Kind::Dim:
                for (size_t i = 0; i < e.sub.size(); i++) {
                    const Entity& s = e.sub[i];
                    if (i >= 2 && s.kind == Kind::Line && s.p[0].x == s.p[1].x && s.p[0].y == s.p[1].y) continue;  // unused aux line
                    if (i >= 4) continue;  // dimension reference points are not drawn
                    Entity part = s;  // the parts live on the dimension's layer
                    part.b.layer = e.b.layer;
                    part.b.glayer = e.b.glayer;
                    emit(part, c, depth);
                }
                break;
        }
    }
    // Jw_cad keeps some application settings as text figures placed at (0,-1000) on pen 9.
    static bool is_setting_text(const Entity& e) {
        static const char* keys[] = {"Printer_Orientation = ", "Printer_PaperSize = ", "Printer_D2dBMP = ", "Printer_BmpZENTAI = ", "View_Direct2d = ", "Draw_BmpTOUKA = "};
        if (e.p[0].x != 0 || e.p[0].y != -1000) return false;
        for (auto k : keys)
            if (e.str.rfind(k, 0) == 0) return true;
        return false;
    }

    // ---- sections --------------------------------------------------------
    void header(double minx, double miny, double maxx, double maxy);
    void tables(const std::vector<std::string>& block_list, const std::vector<std::string>& br);
    std::string font_file(const std::string& font);
    std::string cur_table_;
    double ext_[4] = {0, 0, 0, 0};
    std::set<int> used_ltypes_;
    void bounds(double& minx, double& miny, double& maxx, double& maxy);
};

void Writer::bounds(double& minx, double& miny, double& maxx, double& maxy) {
    minx = miny = 1e300;
    maxx = maxy = -1e300;
    auto add = [&](double x, double y) {
        if (!std::isfinite(x) || !std::isfinite(y)) return;
        minx = std::min(minx, x); maxx = std::max(maxx, x);
        miny = std::min(miny, y); maxy = std::max(maxy, y);
    };
    for (const auto& e : dw_.ents) {
        double s = scale_of(e.b);
        const double X0 = ox(), Y0 = oy();
        switch (e.kind) {
            case Kind::Line: add((e.p[0].x + X0) * s, (e.p[0].y + Y0) * s); add((e.p[1].x + X0) * s, (e.p[1].y + Y0) * s); break;
            case Kind::Arc: add((e.p[0].x - e.r + X0) * s, (e.p[0].y - e.r + Y0) * s); add((e.p[0].x + e.r + X0) * s, (e.p[0].y + e.r + Y0) * s); break;
            case Kind::Point: case Kind::Text: case Kind::Block: add((e.p[0].x + X0) * s, (e.p[0].y + Y0) * s); break;
            case Kind::Solid: if (e.b.pen_style < 101) for (auto& p : e.p) add((p.x + X0) * s, (p.y + Y0) * s); else add((e.p[0].x + X0) * s, (e.p[0].y + Y0) * s); break;
            case Kind::Dim: for (auto& q : e.sub) if (q.kind == Kind::Line) { add((q.p[0].x + X0) * s, (q.p[0].y + Y0) * s); add((q.p[1].x + X0) * s, (q.p[1].y + Y0) * s); } break;
        }
    }
    if (minx > maxx) minx = miny = maxx = maxy = 0;
}

void Writer::header(double minx, double miny, double maxx, double maxy) {
    g(0, "SECTION"); g(2, "HEADER");
    g(9, "$ACADVER"); g(1, r12() ? "AC1009" : utf8() ? "AC1021" : "AC1015");
    g(9, "$DWGCODEPAGE"); g(3, "ANSI_932");  // R2007+ text is UTF-8 regardless
    g(9, "$INSBASE"); gd(10, 0); gd(20, 0); gd(30, 0);
    g(9, "$EXTMIN"); gd(10, minx); gd(20, miny); gd(30, 0);
    g(9, "$EXTMAX"); gd(10, maxx); gd(20, maxy); gd(30, 0);
    double ws = o_.paper_units ? 1 : dw_.groups[dw_.write_glayer & 15].scale;
    if (!(ws > 0)) ws = 1;
    Pt ps = paper_size(dw_.paper);
    double lx = o_.origin_center ? -ps.x / 2 : 0, ly = o_.origin_center ? -ps.y / 2 : 0;
    g(9, "$LIMMIN"); gd(10, lx * ws); gd(20, ly * ws);
    g(9, "$LIMMAX"); gd(10, (lx + ps.x) * ws); gd(20, (ly + ps.y) * ws);
    g(9, "$LTSCALE"); gd(40, ws);
    if (!r12()) {
        g(9, "$INSUNITS"); g(70, 4);
        g(9, "$MEASUREMENT"); g(70, 1);
        g(9, "$LWDISPLAY"); g(290, 0);
        { char hb[16]; std::snprintf(hb, sizeof hb, "%X", handle_); g(9, "$HANDSEED"); g(5, hb); }
    }
    g(0, "ENDSEC");
}

void Writer::tables(const std::vector<std::string>& block_list, const std::vector<std::string>& br) {
    g(0, "SECTION"); g(2, "TABLES");
    auto table = [&](const char* name, const char* h, int n) {
        g(0, "TABLE"); g(2, name);
        if (!r12()) { g(5, h); g(330, "0"); sub("AcDbSymbolTable"); }
        g(70, n);
    };
    auto rec = [&](const char* type, const char* h, const char* rsub) {
        g(0, type);
        if (!r12()) { g(5, h ? std::string(h) : hnd()); g(330, cur_table_); sub("AcDbSymbolTableRecord"); sub(rsub); }
    };
    // VPORT
    cur_table_ = "8"; table("VPORT", "8", 1);
    rec("VPORT", nullptr, "AcDbViewportTableRecord");
    g(2, "*ACTIVE"); g(70, 0);
    gd(10, 0); gd(20, 0); gd(11, 1); gd(21, 1);
    gd(12, (ext_[0] + ext_[2]) / 2); gd(22, (ext_[1] + ext_[3]) / 2);
    gd(40, std::max(1.0, (ext_[3] - ext_[1]) * 1.1)); gd(41, 1.4142);
    g(0, "ENDTAB");
    // LTYPE
    std::set<int> styles;
    for (int s : used_ltypes_) styles.insert(s);
    cur_table_ = "5"; table("LTYPE", "5", int(styles.size()) + 3);
    if (!r12()) {
        rec("LTYPE", "14", "AcDbLinetypeTableRecord"); g(2, "ByBlock"); g(70, 0); g(3, ""); g(72, 65); g(73, 0); gd(40, 0);
        rec("LTYPE", "15", "AcDbLinetypeTableRecord"); g(2, "ByLayer"); g(70, 0); g(3, ""); g(72, 65); g(73, 0); gd(40, 0);
    }
    rec("LTYPE", "16", "AcDbLinetypeTableRecord"); g(2, "Continuous"); g(70, 0); g(3, "Solid line"); g(72, 65); g(73, 0); gd(40, 0);
    for (int st : styles) {
        auto pat = ltype_pattern(st);
        rec("LTYPE", nullptr, "AcDbLinetypeTableRecord");
        g(2, ltype_name(st)); g(70, 0);
        g(3, st >= 2 && st <= 9 ? kLt[st - 1].desc : ltype_name(st));
        g(72, 65); g(73, int(pat.size()));
        double total = 0;
        for (double v : pat) total += std::fabs(v);
        gd(40, total);
        for (double v : pat) { gd(49, v); if (!r12()) g(74, 0); }
    }
    g(0, "ENDTAB");
    // LAYER
    cur_table_ = "2"; table("LAYER", "2", int(used_layers_.size()) + 1);
    rec("LAYER", "10", "AcDbLayerTableRecord"); g(2, "0"); g(70, 0); g(62, 7); g(6, "Continuous");
    if (!r12()) { g(370, -3); g(390, "F"); }
    for (int key : used_layers_) {
        int gi = key / 16, li = key % 16;
        bool off = dw_.groups[gi].state == 0 || dw_.groups[gi].layers[li].state == 0;
        rec("LAYER", nullptr, "AcDbLayerTableRecord");
        g(2, enc(layer_name(key)));
        g(70, 0);
        g(62, off ? -7 : 7);
        g(6, "Continuous");
        if (!r12()) { g(370, -3); g(390, "F"); }
    }
    g(0, "ENDTAB");
    // STYLE
    cur_table_ = "3"; table("STYLE", "3", int(styles_.size()) + 1);
    rec("STYLE", "11", "AcDbTextStyleTableRecord");
    g(2, "Standard"); g(70, 0); gd(40, 0); gd(41, 1); gd(50, 0); g(71, 0); gd(42, 2.5); g(3, "txt"); g(4, "");
    for (auto& [key, name] : styles_) {
        bool vertical = !key.empty() && key.back() == '\x01';
        std::string font = vertical ? key.substr(0, key.size() - 1) : key;
        rec("STYLE", nullptr, "AcDbTextStyleTableRecord");
        g(2, enc(name)); g(70, vertical ? 4 : 0); gd(40, 0); gd(41, 1); gd(50, 0); g(71, 0); gd(42, 2.5);
        g(3, enc(font_file(font))); g(4, "");
        if (!r12()) { g(1001, "ACAD"); gs(1000, font); g(1071, 0x80 | (128 << 8) | (1 << 16)); }  // charset SHIFTJIS
    }
    g(0, "ENDTAB");
    cur_table_ = "6"; table("VIEW", "6", 0); g(0, "ENDTAB");
    cur_table_ = "7"; table("UCS", "7", 0); g(0, "ENDTAB");
    cur_table_ = "9"; table("APPID", "9", 1);
    rec("APPID", "12", "AcDbRegAppTableRecord"); g(2, "ACAD"); g(70, 0);
    g(0, "ENDTAB");
    cur_table_ = "A"; table("DIMSTYLE", "A", 1);
    if (!r12()) sub("AcDbDimStyleTable");
    g(0, "DIMSTYLE");
    if (!r12()) { g(105, "27"); g(330, "A"); sub("AcDbSymbolTableRecord"); sub("AcDbDimStyleTableRecord"); }
    g(2, "Standard"); g(70, 0);
    g(0, "ENDTAB");
    if (!r12()) {
        cur_table_ = "1"; table("BLOCK_RECORD", "1", int(block_list.size()) + 2);
        rec("BLOCK_RECORD", "1F", "AcDbBlockTableRecord"); g(2, "*Model_Space");
        rec("BLOCK_RECORD", "1B", "AcDbBlockTableRecord"); g(2, "*Paper_Space");
        for (size_t i = 0; i < block_list.size(); i++) {
            rec("BLOCK_RECORD", br[i].c_str(), "AcDbBlockTableRecord");
            g(2, enc(block_list[i]));
        }
        g(0, "ENDTAB");
    }
    g(0, "ENDSEC");
}

std::string Writer::font_file(const std::string& font) {
    // Map common Japanese Windows fonts to their files (helps AutoCAD-compatible readers).
    if (font.find("明朝") != std::string::npos) return "msmincho.ttc";
    if (font.find("ゴシック") != std::string::npos) return "msgothic.ttc";
    return font;
}

void Writer::run() {
    for (const auto& b : dw_.blocks) defs_[b.number] = &b;

    // ENTITIES first: this collects the layers, styles, line types and blocks in use.
    std::string ents, blocks, head, tabs, objs;
    out_ = &ents;
    for (const auto& e : dw_.ents) {
        Ctx c;
        c.s = scale_of(e.b);
        c.ox = ox(); c.oy = oy();
        c.owner = "1F";
        emit(e, c);
    }
    // BLOCKS: one definition per (block, scale) used, including nested ones.
    out_ = &blocks;
    if (!r12()) {
        g(0, "BLOCK"); g(5, "20"); g(330, "1F"); sub("AcDbEntity"); g(8, "0"); sub("AcDbBlockBegin");
        g(2, "*Model_Space"); g(70, 0); gd(10, 0); gd(20, 0); gd(30, 0); g(3, "*Model_Space"); g(1, "");
        g(0, "ENDBLK"); g(5, "21"); g(330, "1F"); sub("AcDbEntity"); g(8, "0"); sub("AcDbBlockEnd");
        g(0, "BLOCK"); g(5, "1C"); g(330, "1B"); sub("AcDbEntity"); g(67, 1); g(8, "0"); sub("AcDbBlockBegin");
        g(2, "*Paper_Space"); g(70, 0); gd(10, 0); gd(20, 0); gd(30, 0); g(3, "*Paper_Space"); g(1, "");
        g(0, "ENDBLK"); g(5, "1D"); g(330, "1B"); sub("AcDbEntity"); g(67, 1); g(8, "0"); sub("AcDbBlockEnd");
    }
    std::vector<std::string> block_list, br;
    for (size_t i = 0; i < pending_.size(); i++) {
        auto k = pending_[i];
        const BlockDef& bd = *defs_[k.first];
        std::string name = block_names_[k];
        std::string rh = hnd();
        block_list.push_back(name);
        br.push_back(rh);
        g(0, "BLOCK");
        handle(rh);
        sub("AcDbEntity");
        g(8, "0");
        sub("AcDbBlockBegin");
        g(2, enc(name));
        g(70, 0);
        gd(10, 0); gd(20, 0); gd(30, 0);
        g(3, enc(name));
        g(1, "");
        Ctx c;
        c.s = k.second;
        c.owner = rh;
        c.in_block = true;
        for (const auto& e : bd.ents) emit(e, c);  // may append to pending_
        g(0, "ENDBLK");
        handle(rh);
        sub("AcDbEntity");
        g(8, "0");
        sub("AcDbBlockEnd");
    }
    bounds(ext_[0], ext_[1], ext_[2], ext_[3]);
    out_ = &tabs;
    tables(block_list, br);
    out_ = &objs;
    if (!r12()) {
        g(0, "SECTION"); g(2, "OBJECTS");
        g(0, "DICTIONARY"); g(5, "C"); g(330, "0"); sub("AcDbDictionary"); g(281, 1);
        g(3, "ACAD_GROUP"); g(350, "D");
        if (!image_order_.empty()) { g(3, "ACAD_IMAGE_DICT"); g(350, "E"); }
        g(0, "DICTIONARY"); g(5, "D"); g(330, "C"); sub("AcDbDictionary"); g(281, 1);
        if (!image_order_.empty()) {
            g(0, "DICTIONARY"); g(5, "E"); g(330, "C"); sub("AcDbDictionary"); g(281, 1);
            std::set<std::string> names;
            for (auto& path : image_order_) {
                std::string n = path;
                auto k = n.find_last_of("/\\%");
                if (k != std::string::npos) n = n.substr(k + 1);
                auto dot = n.find_last_of('.');
                if (dot != std::string::npos) n.erase(dot);
                n = sanitize_layer(n);
                std::string u = n;
                for (int i = 2; names.count(u); i++) u = n + "_" + std::to_string(i);
                names.insert(u);
                g(3, enc(u)); g(350, image_defs_[path].handle);
            }
            for (auto& path : image_order_) {
                const ImageDef& d = image_defs_[path];
                g(0, "IMAGEDEF"); g(5, d.handle);
                g(102, "{ACAD_REACTORS");
                g(330, "E");
                for (auto& r : d.reactors) g(330, r);
                g(102, "}");
                g(330, "E");
                sub("AcDbRasterImageDef");
                g(90, 0); gs_raw(1, d.path);
                gd(10, d.w); gd(20, d.h); gd(11, 1); gd(21, 1);
                g(280, 1); g(281, 0);
            }
            for (size_t i = 0; i < image_refs_.size(); i++) {
                g(0, "IMAGEDEF_REACTOR"); g(5, image_refs_[i].reactor); g(330, image_refs_[i].img);
                sub("AcDbRasterImageDefReactor"); g(90, 2); g(330, image_refs_[i].img);
            }
        }
        g(0, "ENDSEC");
    }
    g(0, "EOF");
    out_ = &head;
    header(ext_[0], ext_[1], ext_[2], ext_[3]);
    if (!r12()) {
        g(0, "SECTION"); g(2, "CLASSES");
        if (!image_order_.empty()) {
            const char* cls[][3] = {{"IMAGE", "AcDbRasterImage", "127"}, {"IMAGEDEF", "AcDbRasterImageDef", "0"}, {"IMAGEDEF_REACTOR", "AcDbRasterImageDefReactor", "1"}, {"RASTERVARIABLES", "AcDbRasterVariables", "0"}};
            for (auto& k : cls) {
                g(0, "CLASS"); g(1, k[0]); g(2, k[1]); g(3, "ISM"); g(90, std::atoi(k[2])); g(280, 0); g(281, k[0] == std::string("IMAGE") ? 1 : 0);
            }
        }
        g(0, "ENDSEC");
    }

    std::fwrite(head.data(), 1, head.size(), f_);
    std::fwrite(tabs.data(), 1, tabs.size(), f_);
    const char* bs = "  0\nSECTION\n  2\nBLOCKS\n";
    std::fputs(bs, f_);
    std::fwrite(blocks.data(), 1, blocks.size(), f_);
    std::fputs("  0\nENDSEC\n  0\nSECTION\n  2\nENTITIES\n", f_);
    std::fwrite(ents.data(), 1, ents.size(), f_);
    std::fputs("  0\nENDSEC\n", f_);
    std::fwrite(objs.data(), 1, objs.size(), f_);
}

}  // namespace

void write_dxf(std::FILE* f, const Drawing& dw, const DxfOptions& opt) {
    Writer w(f, dw, opt);
    w.run();
}

}  // namespace jww
