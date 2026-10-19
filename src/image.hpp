// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace jww {
std::vector<uint8_t> inflate_raw(const uint8_t* p, size_t n);
std::vector<uint8_t> gunzip(const std::vector<uint8_t>& d);
// Pixel size of a BMP/PNG/GIF/JPEG image.
bool image_size(const std::vector<uint8_t>& d, int& w, int& h);
}  // namespace jww
