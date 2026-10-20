// jww2dxf: convert Jw_cad drawings (.jww) to DXF
// SPDX-License-Identifier: MIT
#include "jww.hpp"
#include "output.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifndef JWW_TOOLS_VERSION
#define JWW_TOOLS_VERSION "0.1.1"
#endif

static void usage() {
    std::fputs(
        "jww2dxf " JWW_TOOLS_VERSION " - convert Jw_cad drawings (.jww) to DXF\n"
        "usage: jww2dxf [options] input.jww [output.dxf]\n"
        "  -o FILE        output file (default: input with .dxf, '-' for stdout)\n"
        "  --json         dump the parsed drawing as JSON instead of DXF\n"
        "  --r12          write DXF R12 (AC1009)\n"
        "  --r2000        write DXF R2000 (AC1015, Shift_JIS text; default)\n"
        "  --utf8         write DXF R2007 (AC1021, UTF-8 text)\n"
        "  --explode      expand blocks instead of writing INSERTs\n"
        "  --hatch        write solids (ソリッド) as HATCH instead of SOLID pieces\n"
        "  --no-images    do not write IMAGE entities (embedded images are otherwise\n"
        "                 extracted to <output>_images/)\n"
        "  --paper        keep paper millimetres (do not apply layer group scales)\n"
        "  --group-scale  scale each layer group by its own scale (default: like Jw_cad,\n"
        "                 every group at the scale of the current write group)\n"
        "  --center       keep the Jw_cad origin (paper centre); default: like Jw_cad,\n"
        "                 the lower-left corner of the paper is (0,0)\n"
        "  --text-factor F  DXF text height = Jw_cad character height * F (default 1)\n"
        "  --no-temp      do not write temporary points (仮点)\n"
        "  --check        report duplicate/overlapping lines and duplicate circles/arcs\n"
        "                 on stderr (alias --warn-overlaps; --json always lists them)\n"
        "  --strict       with --check: exit 4 if duplicates or overlaps were found\n"
        "  --tol MM       tolerance in DXF millimetres (default: 1e-6 of the drawing\n"
        "                 extent, at least 0.0001)\n"
        "  --merge-collinear  merge collinear overlapping/touching lines (same layer,\n"
        "                 line type, colour and width) into one line in the DXF\n"
        "  -q             quiet (no warnings)\n"
        "  --version\n",
        stderr);
}

// Summary of --check on stderr: counts per kind, then the first few of each kind.
static void print_check(const std::string& in, const jww::Drawing& dw, const jww::CheckResult& r) {
    const char* nm = in.c_str();
    std::fprintf(stderr, "jww2dxf: %s: check: %zu lines, %zu circles/arcs; tolerance %.6g mm (coordinates as in the DXF)\n",
                 nm, r.lines, r.arcs, r.tol);
    static const struct { jww::Issue::Type t; const char* label; const char* unit; } kinds[] = {
        {jww::Issue::DuplicateLine, "duplicate lines", "group"},
        {jww::Issue::OverlapLines, "overlapping collinear lines", "pair"},
        {jww::Issue::DuplicateCircle, "duplicate circles/ellipses", "group"},
        {jww::Issue::DuplicateArc, "duplicate arcs", "group"},
        {jww::Issue::TouchingLines, "collinear lines touching end to end (info: could be one line)", "pair"},
    };
    auto layer = [&](size_t ent) {
        const auto& b = dw.ents[ent].b;
        return jww::dxf_layer_name(dw, (b.glayer & 15) * 16 + (b.layer & 15));
    };
    const size_t limit = 10;
    for (auto& k : kinds) {
        size_t n = 0, same = 0;
        for (auto& is : r.issues)
            if (is.type == k.t) { n++; same += is.same_layer; }
        if (!n) continue;
        std::fprintf(stderr, "jww2dxf: %s: %s: %zu %s%s (%zu on one layer, %zu across layers)\n", nm, k.label, n, k.unit,
                     n == 1 ? "" : "s", same, n - same);
        size_t shown = 0;
        for (auto& is : r.issues) {
            if (is.type != k.t) continue;
            if (shown++ == limit) { std::fprintf(stderr, "    ... (--json lists all)\n"); break; }
            std::string ids, lays;
            for (size_t e : is.ents) {
                ids += (ids.empty() ? "#" : " #") + std::to_string(e);
                std::string l = layer(e);
                if (lays.find("[" + l + "]") == std::string::npos) lays += "[" + l + "]";
            }
            switch (is.type) {
                case jww::Issue::DuplicateLine:
                    std::fprintf(stderr, "    %s %s (%.10g, %.10g)-(%.10g, %.10g)\n", ids.c_str(), lays.c_str(), is.a.x, is.a.y, is.b.x, is.b.y);
                    break;
                case jww::Issue::OverlapLines:
                    std::fprintf(stderr, "    %s %s overlap (%.10g, %.10g)-(%.10g, %.10g)\n", ids.c_str(), lays.c_str(), is.a.x, is.a.y, is.b.x, is.b.y);
                    break;
                case jww::Issue::TouchingLines:
                    std::fprintf(stderr, "    %s %s at (%.10g, %.10g)\n", ids.c_str(), lays.c_str(), is.a.x, is.a.y);
                    break;
                default:
                    std::fprintf(stderr, "    %s %s centre (%.10g, %.10g) r %.10g\n", ids.c_str(), lays.c_str(), is.a.x, is.a.y, is.r);
            }
        }
    }
    if (r.issues.empty()) std::fprintf(stderr, "jww2dxf: %s: check: no duplicates or overlaps\n", nm);
}

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <vector>
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    // Use the UTF-16 command line so that Japanese file names work whatever the ANSI code page is.
    std::vector<std::string> u8args;
    std::vector<char*> u8argv;
    {
        int wargc = 0;
        LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
        if (wargv) {
            for (int i = 0; i < wargc; i++) {
                int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
                std::string s(n > 0 ? n - 1 : 0, '\0');
                if (n > 1) WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, &s[0], n, nullptr, nullptr);
                u8args.push_back(s);
            }
            LocalFree(wargv);
            for (auto& s : u8args) u8argv.push_back(&s[0]);
            argc = int(u8argv.size());
            argv = u8argv.data();
        }
    }
#endif
    std::string in, out;
    bool json = false, quiet = false, check = false, strict = false, merge = false;
    double tol = 0;
    jww::DxfOptions opt;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (!std::strcmp(a, "--json")) json = true;
        else if (!std::strcmp(a, "--r12")) opt.version = jww::DxfOptions::R12;
        else if (!std::strcmp(a, "--r2000")) opt.version = jww::DxfOptions::R2000;
        else if (!std::strcmp(a, "--utf8")) opt.version = jww::DxfOptions::R2007;
        else if (!std::strcmp(a, "--explode")) opt.explode_blocks = true;
        else if (!std::strcmp(a, "--hatch")) opt.hatch = true;
        else if (!std::strcmp(a, "--no-images")) opt.images = false;
        else if (!std::strcmp(a, "--paper")) opt.paper_units = true;
        else if (!std::strcmp(a, "--group-scale")) opt.group_scale = true;
        else if (!std::strcmp(a, "--center")) opt.origin_center = true;
        else if (!std::strcmp(a, "--text-factor") && i + 1 < argc) opt.text_height_factor = std::atof(argv[++i]);
        else if (!std::strcmp(a, "--no-temp")) opt.skip_temporary_points = true;
        else if (!std::strcmp(a, "--check") || !std::strcmp(a, "--warn-overlaps")) check = true;
        else if (!std::strcmp(a, "--strict")) strict = check = true;
        else if (!std::strcmp(a, "--merge-collinear")) merge = true;
        else if (!std::strcmp(a, "--tol") && i + 1 < argc) {
            tol = std::atof(argv[++i]);
            if (!(tol > 0)) { std::fprintf(stderr, "jww2dxf: --tol needs a positive number\n"); return 2; }
        }
        else if (!std::strcmp(a, "-q")) quiet = true;
        else if (!std::strcmp(a, "-o") && i + 1 < argc) out = argv[++i];
        else if (!std::strcmp(a, "--version")) { std::puts("jww2dxf " JWW_TOOLS_VERSION); return 0; }
        else if (!std::strcmp(a, "-h") || !std::strcmp(a, "--help")) { usage(); return 0; }
        else if (a[0] == '-' && a[1]) { std::fprintf(stderr, "jww2dxf: unknown option %s\n", a); usage(); return 2; }
        else if (in.empty()) in = a;
        else if (out.empty()) out = a;
        else { usage(); return 2; }
    }
    if (in.empty()) { usage(); return 2; }
    if (out.empty()) {
        if (json) out = "-";
        else {
            out = in;
            auto dot = out.find_last_of('.');
            auto sl = out.find_last_of("/\\");
            if (dot != std::string::npos && (sl == std::string::npos || dot > sl)) out.erase(dot);
            out += ".dxf";
        }
    }
    jww::Drawing dw;
    try {
        dw = jww::read_file(in);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "jww2dxf: %s: %s\n", in.c_str(), e.what());
        return 1;
    }
    if (!quiet)
        for (auto& w : dw.warnings) std::fprintf(stderr, "jww2dxf: %s: warning: %s\n", in.c_str(), w.c_str());
    if (!json) {
        auto sl = in.find_last_of("/\\");
        opt.source_dir = sl == std::string::npos ? "." : in.substr(0, sl);
        if (out != "-") {
            std::string stem = out;
            auto dot = stem.find_last_of('.');
            auto s2 = stem.find_last_of("/\\");
            if (dot != std::string::npos && (s2 == std::string::npos || dot > s2)) stem.erase(dot);
            opt.image_dir = stem + "_images";
            opt.image_ref_prefix = (s2 == std::string::npos ? stem : stem.substr(s2 + 1)) + "_images/";
        }
    }
    jww::CheckResult chk;
    bool found = false;
    if (check || json) {
        chk = jww::check_drawing(dw, opt, tol);
        for (auto& is : chk.issues) found |= !jww::issue_is_info(is.type);
        if (check) print_check(in, dw, chk);
    }
    if (merge && !json) {
        size_t n = jww::merge_collinear(dw, opt, tol);
        if (!quiet && n) std::fprintf(stderr, "jww2dxf: %s: --merge-collinear: %zu line(s) merged into others\n", in.c_str(), n);
    }
    std::FILE* f = out == "-" ? stdout : jww::u8fopen(out, "wb");
    if (!f) { std::fprintf(stderr, "jww2dxf: cannot write %s\n", out.c_str()); return 1; }
    if (json) jww::write_json(f, dw, &chk);
    else jww::write_dxf(f, dw, opt);
    bool err = std::ferror(f);
    if (f != stdout) err |= std::fclose(f) != 0;
    else std::fflush(f);
    if (err) { std::fprintf(stderr, "jww2dxf: write error on %s\n", out.c_str()); return 1; }
    // A drawing whose figure list could not be read completely is a failure.
    for (auto& w : dw.warnings)
        if (w.rfind("read stopped", 0) == 0) return 3;
    if (strict && found) return 4;
    return 0;
}
