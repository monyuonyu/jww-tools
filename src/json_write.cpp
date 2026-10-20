// SPDX-License-Identifier: MIT
#include "jww.hpp"
#include "output.hpp"

#include <cstdio>
#include <cmath>

namespace jww {

namespace {
void js(std::FILE* f, const std::string& s) {
    std::fputc('"', f);
    for (unsigned char c : s) {
        switch (c) {
            case '"': std::fputs("\\\"", f); break;
            case '\\': std::fputs("\\\\", f); break;
            case '\n': std::fputs("\\n", f); break;
            case '\r': std::fputs("\\r", f); break;
            case '\t': std::fputs("\\t", f); break;
            default:
                if (c < 0x20) std::fprintf(f, "\\u%04x", c);
                else std::fputc(c, f);
        }
    }
    std::fputc('"', f);
}
void num(std::FILE* f, double v) {
    if (!std::isfinite(v)) { std::fputs("null", f); return; }
    std::fprintf(f, "%.17g", v);
}
const char* kind_name(Kind k) {
    switch (k) {
        case Kind::Line: return "line";
        case Kind::Arc: return "arc";
        case Kind::Point: return "point";
        case Kind::Text: return "text";
        case Kind::Solid: return "solid";
        case Kind::Block: return "block";
        case Kind::Dim: return "dim";
    }
    return "?";
}
void ent(std::FILE* f, const Entity& e) {
    std::fprintf(f, "{\"type\":\"%s\",\"group\":%u,\"pen_style\":%u,\"pen_color\":%u,\"pen_width\":%u,\"layer\":%u,\"glayer\":%u,\"flag\":%u",
                 kind_name(e.kind), e.b.group, e.b.pen_style, e.b.pen_color, e.b.pen_width, e.b.layer, e.b.glayer, e.b.flag);
    auto pt = [&](const char* k, const Pt& p) { std::fprintf(f, ",\"%s\":[", k); num(f, p.x); std::fputc(',', f); num(f, p.y); std::fputc(']', f); };
    auto d = [&](const char* k, double v) { std::fprintf(f, ",\"%s\":", k); num(f, v); };
    switch (e.kind) {
        case Kind::Line: pt("p1", e.p[0]); pt("p2", e.p[1]); break;
        case Kind::Arc: pt("center", e.p[0]); d("r", e.r); d("start", e.start); d("sweep", e.sweep); d("tilt", e.tilt); d("flat", e.flat); std::fprintf(f, ",\"full\":%s", e.full ? "true" : "false"); break;
        case Kind::Point: pt("p", e.p[0]); std::fprintf(f, ",\"temporary\":%s", e.temporary ? "true" : "false"); if (e.b.pen_style == 100) { std::fprintf(f, ",\"code\":%u", e.code); d("angle", e.angle); d("scale", e.scale); } break;
        case Kind::Text: pt("start", e.p[0]); pt("end", e.p[1]); std::fprintf(f, ",\"text_type\":%u", e.text_type); d("size_x", e.size_x); d("size_y", e.size_y); d("spacing", e.spacing); d("angle", e.angle); std::fputs(",\"font\":", f); js(f, e.font); std::fputs(",\"string\":", f); js(f, e.str); break;
        case Kind::Solid: pt("p1", e.p[0]); pt("p4", e.p[1]); pt("p2", e.p[2]); pt("p3", e.p[3]); if (e.b.pen_color == 10) std::fprintf(f, ",\"rgb\":%u", e.color); break;
        case Kind::Block: pt("base", e.p[0]); d("sx", e.sx); d("sy", e.sy); d("angle", e.angle); std::fprintf(f, ",\"number\":%u", e.number); break;
        case Kind::Dim:
            std::fprintf(f, ",\"sxf_mode\":%u,\"parts\":[", e.sxf_mode);
            for (size_t i = 0; i < e.sub.size(); i++) { if (i) std::fputc(',', f); ent(f, e.sub[i]); }
            std::fputc(']', f);
            break;
    }
    std::fputc('}', f);
}
}  // namespace

void write_json(std::FILE* f, const Drawing& dw, const CheckResult* check) {
    std::fprintf(f, "{\"format\":\"%s\",\"version\":%u,\"memo\":", dw.format.c_str(), dw.version);
    js(f, dw.memo);
    std::fprintf(f, ",\"paper\":%u,\"write_glayer\":%u,\"header_complete\":%s,\"list_found_by_scan\":%s,\"unicode_strings\":%s,\n\"groups\":[",
                 dw.paper, dw.write_glayer, dw.header_complete ? "true" : "false", dw.list_found_by_scan ? "true" : "false", dw.unicode_strings ? "true" : "false");
    for (int g = 0; g < 16; g++) {
        const auto& G = dw.groups[g];
        std::fprintf(f, "%s\n{\"state\":%u,\"write_layer\":%u,\"protect\":%u,\"scale\":", g ? "," : "", G.state, G.write_layer, G.protect);
        num(f, G.scale);
        std::fputs(",\"name\":", f); js(f, G.name);
        std::fputs(",\"layers\":[", f);
        for (int l = 0; l < 16; l++) {
            std::fprintf(f, "%s{\"state\":%u,\"protect\":%u,\"name\":", l ? "," : "", G.layers[l].state, G.layers[l].protect);
            js(f, G.layers[l].name);
            std::fputc('}', f);
        }
        std::fputs("]}", f);
    }
    std::fputs("],\n\"pens\":{", f);
    bool first = true;
    for (auto& [n, p] : dw.pens) {
        std::fprintf(f, "%s\"%d\":{\"color\":%u,\"width\":%u,\"prt_color\":%u,\"prt_width\":%u,\"prt_point_r\":", first ? "" : ",", n, p.color, p.width, p.prt_color, p.prt_width);
        num(f, p.prt_point_r);
        if (!p.name.empty()) { std::fputs(",\"name\":", f); js(f, p.name); }
        std::fputc('}', f);
        first = false;
    }
    std::fputs("},\n\"entities\":[", f);
    for (size_t i = 0; i < dw.ents.size(); i++) { std::fputs(i ? ",\n" : "\n", f); ent(f, dw.ents[i]); }
    std::fputs("],\n\"blocks\":[", f);
    for (size_t i = 0; i < dw.blocks.size(); i++) {
        const auto& b = dw.blocks[i];
        std::fprintf(f, "%s{\"number\":%u,\"referenced\":%s,\"time\":%u,\"sfig_flag\":%d,\"layer\":%u,\"glayer\":%u,\"name\":", i ? ",\n" : "\n", b.number, b.referenced ? "true" : "false", b.time, b.sfig_flag, b.b.layer, b.b.glayer);
        js(f, b.name);
        std::fputs(",\"entities\":[", f);
        for (size_t j = 0; j < b.ents.size(); j++) { std::fputs(j ? ",\n" : "\n", f); ent(f, b.ents[j]); }
        std::fputs("]}", f);
    }
    std::fputs("],\n\"images\":[", f);
    for (size_t i = 0; i < dw.images.size(); i++) {
        std::fprintf(f, "%s{\"name\":", i ? "," : "");
        js(f, dw.images[i].name);
        std::fprintf(f, ",\"size\":%zu}", dw.images[i].data.size());
    }
    std::fputs("],\n\"warnings\":[", f);
    for (size_t i = 0; i < dw.warnings.size(); i++) { if (i) std::fputc(',', f); js(f, dw.warnings[i]); }
    std::fputc(']', f);
    if (check) {
        auto pt = [&](const Pt& p) { std::fputc('[', f); num(f, p.x); std::fputc(',', f); num(f, p.y); std::fputc(']', f); };
        std::fputs(",\n\"check\":{\"tol\":", f); num(f, check->tol);
        std::fputs(",\"extent\":", f); num(f, check->extent);
        std::fprintf(f, ",\"lines\":%zu,\"arcs\":%zu,\"coordinates\":\"dxf\"},\n\"issues\":[", check->lines, check->arcs);
        for (size_t i = 0; i < check->issues.size(); i++) {
            const Issue& is = check->issues[i];
            std::fprintf(f, "%s{\"type\":\"%s\",\"severity\":\"%s\",\"same_layer\":%s,\"entities\":[", i ? ",\n" : "\n", issue_name(is.type),
                         issue_is_info(is.type) ? "info" : "warning", is.same_layer ? "true" : "false");
            for (size_t k = 0; k < is.ents.size(); k++) std::fprintf(f, "%s%zu", k ? "," : "", is.ents[k]);
            std::fputs("],\"layers\":[", f);
            for (size_t k = 0; k < is.ents.size(); k++) {
                if (k) std::fputc(',', f);
                const Base& b = dw.ents[is.ents[k]].b;
                js(f, dxf_layer_name(dw, (b.glayer & 15) * 16 + (b.layer & 15)));
            }
            std::fputc(']', f);
            switch (is.type) {
                case Issue::DuplicateLine: case Issue::OverlapLines: case Issue::TouchingLines:
                    std::fputs(",\"lines\":[", f);
                    for (size_t k = 0; k + 1 < is.ends.size(); k += 2) {
                        std::fputs(k ? ",[" : "[", f); pt(is.ends[k]); std::fputc(',', f); pt(is.ends[k + 1]); std::fputc(']', f);
                    }
                    std::fputc(']', f);
                    if (is.type == Issue::OverlapLines) { std::fputs(",\"overlap\":[", f); pt(is.a); std::fputc(',', f); pt(is.b); std::fputc(']', f); }
                    if (is.type == Issue::TouchingLines) { std::fputs(",\"at\":", f); pt(is.a); }
                    break;
                case Issue::DuplicateCircle: case Issue::DuplicateArc:
                    std::fputs(",\"center\":", f); pt(is.a);
                    std::fputs(",\"r\":", f); num(f, is.r);
                    break;
            }
            std::fputc('}', f);
        }
        std::fputc(']', f);
    }
    std::fputs("}\n", f);
}

}  // namespace jww
