// SPDX-License-Identifier: MIT
#pragma once
#include <cstdio>
#include "jww.hpp"

namespace jww {

struct DxfOptions {
    enum Version { R12, R2000, R2007 } version = R2000;
    bool explode_blocks = false;
    bool paper_units = false;   // write paper millimetres instead of real size
    bool skip_temporary_points = false;  // Jw_cad's own DXF export writes 仮点 too
    double text_height_factor = 1.0;  // DXF text height = Jw_cad character height * factor
    bool group_scale = false;    // scale each layer group by its own scale (Jw_cad: the current group's scale for all)
    bool origin_center = false;  // keep the Jw_cad origin (paper centre) instead of the paper's lower-left corner
};

void write_json(std::FILE* f, const Drawing& dw);
void write_dxf(std::FILE* f, const Drawing& dw, const DxfOptions& opt);

}  // namespace jww
