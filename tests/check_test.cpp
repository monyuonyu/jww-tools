// Tests of the drawing check (--check) and --merge-collinear on synthetic drawings.
// SPDX-License-Identifier: MIT
#include "jww.hpp"
#include "output.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace jww;

static int failures = 0;
#define EXPECT(cond)                                                              \
    do {                                                                          \
        if (!(cond)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
    } while (0)

static Drawing blank() {
    Drawing d;
    d.format = "jww";
    d.paper = kNoPaper;  // no origin shift: DXF coordinates = drawing coordinates
    for (auto& g : d.groups) g.scale = 1;
    return d;
}
static Entity line(double x1, double y1, double x2, double y2, int layer = 0, int glayer = 0) {
    Entity e;
    e.kind = Kind::Line;
    e.p[0] = {x1, y1};
    e.p[1] = {x2, y2};
    e.b.layer = uint16_t(layer);
    e.b.glayer = uint16_t(glayer);
    return e;
}
static Entity arc(double cx, double cy, double r, double start, double sweep, bool full = false, double flat = 1, double tilt = 0) {
    Entity e;
    e.kind = Kind::Arc;
    e.p[0] = {cx, cy};
    e.r = r; e.start = start; e.sweep = sweep; e.full = full; e.flat = flat; e.tilt = tilt;
    return e;
}
static size_t count(const CheckResult& r, Issue::Type t) {
    size_t n = 0;
    for (auto& i : r.issues) n += i.type == t;
    return n;
}
static CheckResult run(const Drawing& d, double tol = 0.001) { return check_drawing(d, DxfOptions(), tol); }

int main() {
    const double kPi = 3.14159265358979323846;
    {   // exact duplicate, reversed duplicate, near duplicate within tol, not within tol
        Drawing d = blank();
        d.ents = {line(0, 0, 100, 0), line(100, 0, 0, 0), line(0, 0.0005, 100, -0.0005), line(0, 10, 100, 10), line(0, 10.01, 100, 10.01)};
        auto r = run(d);
        EXPECT(count(r, Issue::DuplicateLine) == 1);
        EXPECT(r.issues.size() == 1);
        EXPECT(r.issues[0].ents == (std::vector<size_t>{0, 1, 2}));
        EXPECT(r.issues[0].same_layer);
    }
    {   // the same line on two layers: reported, marked as across layers
        Drawing d = blank();
        d.ents = {line(0, 0, 50, 50, 0), line(0, 0, 50, 50, 3, 1)};
        auto r = run(d);
        EXPECT(count(r, Issue::DuplicateLine) == 1 && !r.issues[0].same_layer);
    }
    {   // collinear overlap, containment, end-to-end touch, gap, T junction, parallel offset
        Drawing d = blank();
        d.ents = {
            line(0, 0, 100, 0), line(60, 0, 150, 0),      // overlap 60..100
            line(0, 20, 100, 20), line(30, 20, 40, 20),    // contained
            line(0, 40, 100, 40), line(100, 40, 200, 40),  // touching at 100
            line(0, 60, 100, 60), line(101, 60, 200, 60),  // gap: nothing
            line(0, 80, 100, 80), line(50, 80, 50, 90),    // T junction: nothing
            line(0, 100, 100, 100), line(0, 100.5, 100, 100.5),  // parallel: nothing
            line(0, 120, 100, 120), line(100, 120, 200, 120),    // pieces of one curve (曲線属性): nothing
            line(0, 0, 70, 70), line(35, 35.0004, 140, 140),     // diagonal overlap within tol
        };
        d.ents[12].b.group = d.ents[13].b.group = 7;
        auto r = run(d);
        EXPECT(count(r, Issue::OverlapLines) == 3);
        EXPECT(count(r, Issue::TouchingLines) == 1);
        EXPECT(r.issues.size() == 4);
        for (auto& i : r.issues) {
            if (i.ents == std::vector<size_t>{0, 1}) {
                EXPECT(i.type == Issue::OverlapLines);
                EXPECT(std::fabs(std::fabs(i.a.x - i.b.x) - 40) < 1e-9);
            }
            if (i.type == Issue::TouchingLines) EXPECT(i.ents == (std::vector<size_t>{4, 5}) && std::fabs(i.a.x - 100) < 1e-9);
        }
    }
    {   // circles, ellipses and arcs
        Drawing d = blank();
        d.ents = {
            arc(0, 0, 10, 0, 2 * kPi, true), arc(0, 0, 10, 1, 2 * kPi, true),        // same circle
            arc(0, 0, 10.5, 0, 2 * kPi, true),                                       // other radius
            arc(50, 0, 10, 0, kPi / 2), arc(50, 0, 10, kPi / 2, -kPi / 2),           // same arc, other direction
            arc(50, 0, 10, 0, kPi / 3),                                              // shorter arc: not a duplicate
            arc(100, 0, 10, 0, 2 * kPi, true, 0.5), arc(100, 0, 5, 0, 2 * kPi, true, 2, kPi / 2),  // same ellipse
            arc(100, 0, 10, 0, 2 * kPi, true, 0.5, kPi / 2),                         // rotated: different
            arc(200, 0, 10, 0, 2 * kPi, true), line(0, 0, 1, 1),
        };
        d.ents[9].b.layer = 2;
        d.ents.push_back(arc(200, 0, 10, 0, 2 * kPi, true));
        d.ents.back().b.layer = 5;
        auto r = run(d);
        EXPECT(count(r, Issue::DuplicateCircle) == 3);
        EXPECT(count(r, Issue::DuplicateArc) == 1);
        bool across = false;
        for (auto& i : r.issues) {
            if (i.type == Issue::DuplicateArc) EXPECT(i.ents == (std::vector<size_t>{3, 4}));
            if (i.type == Issue::DuplicateCircle && i.ents == std::vector<size_t>{9, 11}) across = !i.same_layer;
        }
        EXPECT(across);
    }
    {   // scale and origin: coordinates and the tolerance are in the DXF's millimetres
        Drawing d = blank();
        d.paper = 3;  // A1
        d.groups[0].scale = 100;
        d.ents = {line(0, 0, 10, 0), line(10, 0.002, 20, 0.002)};  // 0.002 mm apart on paper = 0.2 mm real
        auto r = check_drawing(d, DxfOptions(), 0.1);
        EXPECT(r.issues.empty());
        r = check_drawing(d, DxfOptions(), 0.5);
        EXPECT(count(r, Issue::TouchingLines) == 1);
        Pt p = dxf_point(d, DxfOptions(), d.ents[0].b, d.ents[0].p[1]);
        if (!r.issues.empty()) EXPECT(std::fabs(r.issues[0].a.x - p.x) < 1e-6);
        // default tolerance: 1e-6 of the extent (2000 mm here), at least 1e-4
        EXPECT(std::fabs(check_drawing(d, DxfOptions(), 0).tol - 0.002) < 1e-9);
        Drawing small = blank();
        small.ents = {line(0, 0, 10, 0)};
        EXPECT(std::fabs(check_drawing(small, DxfOptions(), 0).tol - 1e-4) < 1e-12);
        d.ents.push_back(line(0, 0, 1e4, 0));
        EXPECT(std::fabs(check_drawing(d, DxfOptions(), 0).tol - 1.0) < 1e-6);
    }
    {   // merge: chains of collinear pieces with the same attributes on the same layer
        Drawing d = blank();
        d.ents = {
            line(0, 0, 40, 0), line(40, 0, 70, 0), line(60, 0, 100, 0), line(100, 0, 60, 0),  // -> 0..100
            line(0, 10, 50, 10), line(50, 10, 100, 10, 1),                                     // other layer: kept
            line(0, 20, 50, 20), line(50, 20, 100, 20),                                         // other colour: kept
            line(0, 30, 50, 30), line(50, 30, 100, 30),                                         // other line type: kept
            arc(0, 0, 5, 0, 1),
            line(100, 50, 0, 50), line(100, 50, 150, 50),                                       // keeps the first one's direction
        };
        d.ents[7].b.pen_color = 2;
        d.ents[9].b.pen_style = 2;
        size_t removed = merge_collinear(d, DxfOptions(), 0.001);
        EXPECT(removed == 4);
        EXPECT(d.ents.size() == 9);
        EXPECT(d.ents[0].p[0].x == 0 && d.ents[0].p[1].x == 100);
        EXPECT(d.ents[7].kind == Kind::Arc);
        EXPECT(d.ents[8].p[0].x == 150 && d.ents[8].p[1].x == 0 && d.ents[8].p[0].y == 50);
        EXPECT(check_drawing(d, DxfOptions(), 0.001).issues.size() == 3);  // the kept touching pairs
    }
    {   // speed: 150,000 lines (a grid of short segments, every tenth one drawn twice)
        Drawing d = blank();
        for (int i = 0; i < 150000; i++) {
            double x = (i % 400) * 10.0, y = (i / 400) * 10.0;
            d.ents.push_back(i % 2 ? line(x, y, x + 10, y) : line(x, y, x, y + 10));
            if (i % 10 == 0) d.ents.push_back(d.ents.back());
        }
        auto t0 = std::chrono::steady_clock::now();
        auto r = check_drawing(d, DxfOptions(), 0);
        double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("165,000 lines checked in %.2f s (%zu issues)\n", sec, r.issues.size());
        EXPECT(count(r, Issue::DuplicateLine) == 15000);
        EXPECT(count(r, Issue::OverlapLines) == 0);
        EXPECT(sec < 30);
    }
    if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    std::puts("check tests passed");
    return 0;
}
