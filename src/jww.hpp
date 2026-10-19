// jww-tools: reader for Jw_cad drawings (.jww / .jws)
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <map>

namespace jww {

// Common attributes of every Jw_cad figure (CData).
struct Base {
    uint32_t group = 0;     // 曲線属性番号
    uint8_t pen_style = 1;  // 線種番号 (1..9 standard, 11..15 random, 16..19 double-length, 31.. SXF)
    uint16_t pen_color = 1; // 線色番号 (1..9, 100+ SXF extended)
    uint16_t pen_width = 0; // 線幅 (Ver.3.51+)
    uint16_t layer = 0;     // レイヤ番号 0..15
    uint16_t glayer = 0;    // レイヤグループ番号 0..15
    uint16_t flag = 0;      // 属性フラグ
};

struct Pt { double x = 0, y = 0; };

enum class Kind { Line, Arc, Point, Text, Solid, Block, Dim };

struct Entity {
    Kind kind = Kind::Line;
    Base b;
    // Line: p[0]-p[1]
    // Arc: p[0]=center, r, start, sweep, tilt (radians), flat, full
    // Point: p[0], temporary, code, angle, scale
    // Text: p[0]=start, p[1]=end, text_type, size_x, size_y, spacing, angle (deg), font, str
    // Solid: p[0..3] = the corners in drawing order (stored as m_start, m_end, m_DPoint2, m_DPoint3);
    //        color (RGB, when pen_color==10)
    //        pen_style>=101: arc solid; p[0]=center, p[1]=(r, flat), p[2]=(tilt, start), p[3]=(sweep, kind)
    // Block: p[0]=base point, sx, sy, angle (rad), number
    // Dim: sub[0]=line, sub[1]=text, sxf_mode, sub[2..3]=aux lines, sub[4..7]=points
    Pt p[4];
    double r = 0, start = 0, sweep = 0, tilt = 0, flat = 1;
    bool full = false;
    bool temporary = false;
    uint32_t code = 0;
    double angle = 0, scale = 0;
    uint32_t text_type = 0;
    double size_x = 0, size_y = 0, spacing = 0;
    std::string font, str;  // UTF-8
    uint32_t color = 0;
    double sx = 1, sy = 1;
    uint32_t number = 0;
    uint16_t sxf_mode = 0;
    std::vector<Entity> sub;
};

struct BlockDef {
    Base b;
    uint32_t number = 0;
    bool referenced = false;
    uint32_t time = 0;
    std::string name;      // UTF-8, "@@SfigorgFlag@@n" suffix removed
    int sfig_flag = 0;     // 複合図形種別 (Ver.4.10+): 1..4, 0 = none
    std::vector<Entity> ents;
};

struct Image {
    std::string name;
    std::vector<uint8_t> data;
};

struct Layer {
    uint32_t state = 2, protect = 0;
    std::string name;
};
struct LayerGroup {
    uint32_t state = 2, write_layer = 0, protect = 0;
    double scale = 1;
    std::string name;
    Layer layers[16];
};

struct PenDef { uint32_t color = 0, width = 1, prt_color = 0, prt_width = 1; double prt_point_r = 0; std::string name; };
struct LineTypeDef { uint32_t pattern = 0, unit_dot = 0, pitch = 0, prt_pitch = 0; std::string name; uint32_t segments = 0; double seg[10] = {0}; };

constexpr uint32_t kNoPaper = 0xFFFFFFFF;  // symbols (.jws) have no sheet

struct Drawing {
    std::string format;   // "jww" or "jws"
    uint32_t version = 0;
    std::string memo;
    uint32_t paper = 0;   // 図面サイズ code
    uint32_t write_glayer = 0;
    LayerGroup groups[16];
    bool header_complete = false;   // header parsed field by field up to the figure list
    bool list_found_by_scan = false;
    std::map<int, PenDef> pens;     // 0..9, 100..356
    std::map<int, LineTypeDef> ltypes; // 2..9, 11..15, 16..19, 30..62
    uint32_t solid_color_default = 0;
    double moji_x[11] = {0}, moji_y[11] = {0}, moji_d[11] = {0};
    uint32_t moji_col[11] = {0};
    std::vector<Entity> ents;
    std::vector<BlockDef> blocks;
    std::vector<Image> images;
    std::vector<std::string> warnings;
    bool unicode_strings = false;
};

// Reads a .jww/.jws file. Throws std::runtime_error on fatal errors; recoverable problems go to warnings.
Drawing read_file(const std::string& path);
Drawing read_buffer(const std::vector<uint8_t>& data);

// Encoding helpers
std::string sjis_to_utf8(const std::string& s);
std::string utf8_to_sjis(const std::string& s, bool* lossy = nullptr);
std::string utf16_to_utf8(const std::u16string& s);

// fopen() for UTF-8 paths (uses _wfopen on Windows).
std::FILE* u8fopen(const std::string& path, const char* mode);

// Paper size in mm (width, height) for a 図面サイズ code.
Pt paper_size(uint32_t code);

}  // namespace jww
