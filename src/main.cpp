// jww2dxf: convert Jw_cad drawings (.jww) to DXF
// SPDX-License-Identifier: MIT
#include "jww.hpp"
#include "output.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifndef JWW_TOOLS_VERSION
#define JWW_TOOLS_VERSION "0.1.0"
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
        "  -q             quiet (no warnings)\n"
        "  --version\n",
        stderr);
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
    bool json = false, quiet = false;
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
    std::FILE* f = out == "-" ? stdout : jww::u8fopen(out, "wb");
    if (!f) { std::fprintf(stderr, "jww2dxf: cannot write %s\n", out.c_str()); return 1; }
    if (json) jww::write_json(f, dw);
    else jww::write_dxf(f, dw, opt);
    bool err = std::ferror(f);
    if (f != stdout) err |= std::fclose(f) != 0;
    else std::fflush(f);
    if (err) { std::fprintf(stderr, "jww2dxf: write error on %s\n", out.c_str()); return 1; }
    // A drawing whose figure list could not be read completely is a failure.
    for (auto& w : dw.warnings)
        if (w.rfind("read stopped", 0) == 0) return 3;
    return 0;
}
