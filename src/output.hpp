// SPDX-License-Identifier: MIT
#pragma once
#include <cstdio>
#include <string>
#include <vector>
#include "jww.hpp"

namespace jww {

struct DxfOptions {
    enum Version { R12, R2000, R2007 } version = R2000;
    bool explode_blocks = false;
    bool images = true;         // write IMAGE entities for ^@BM image placements
    std::string image_dir;      // where embedded images are extracted ("" = do not extract)
    std::string image_ref_prefix; // path written in the DXF for extracted images, e.g. "drawing_images/"
    std::string source_dir;     // directory of the .jww (to resolve relative image paths)
    bool hatch = false;         // write solids as HATCH (default: SOLID triangles/quads, read by more programs)
    bool paper_units = false;   // write paper millimetres instead of real size
    bool skip_temporary_points = false;  // Jw_cad's own DXF export writes 仮点 too
    double text_height_factor = 1.0;  // DXF text height = Jw_cad character height * factor
    bool group_scale = false;    // scale each layer group by its own scale (Jw_cad: the current group's scale for all)
    bool origin_center = false;  // keep the Jw_cad origin (paper centre) instead of the paper's lower-left corner
};

// ---- drawing check (duplicate / overlapping figures) ----------------------
// Coordinates and tolerances are in the DXF's model space (mm), i.e. after the same
// scale and origin shift write_dxf applies with the same options.
struct Issue {
    enum Type { DuplicateLine, OverlapLines, TouchingLines, DuplicateCircle, DuplicateArc } type;
    std::vector<size_t> ents;    // indices into Drawing::ents (sorted)
    bool same_layer = true;      // all figures on one layer (group x layer)
    Pt a, b;                     // overlap: the shared part a-b; touching: a = joint; arcs: a = centre
    double r = 0;                // arcs: radius
    std::vector<Pt> ends;        // lines: the two end points of each figure, in the order of ents
};
struct CheckResult {
    double tol = 0, extent = 0;
    size_t lines = 0, arcs = 0;  // figures examined
    std::vector<Issue> issues;
};
// tol <= 0: max(1e-6 * extent, 1e-4 mm).
CheckResult check_drawing(const Drawing& dw, const DxfOptions& opt, double tol);
const char* issue_name(Issue::Type t);
bool issue_is_info(Issue::Type t);
// Lines merged by --merge-collinear: collinear lines on the same layer with the same
// line type, colour and width that overlap or touch become one line (the first one, with
// the outermost end points of the group); the others are removed. Returns the number removed.
size_t merge_collinear(Drawing& dw, const DxfOptions& opt, double tol);
// DXF layer name of layer group/layer key (group * 16 + layer).
std::string dxf_layer_name(const Drawing& dw, int key);
// DXF model space position of a point of a top-level figure.
Pt dxf_point(const Drawing& dw, const DxfOptions& opt, const Base& b, Pt p);

void write_json(std::FILE* f, const Drawing& dw, const CheckResult* check = nullptr);
void write_dxf(std::FILE* f, const Drawing& dw, const DxfOptions& opt);

}  // namespace jww
