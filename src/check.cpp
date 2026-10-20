// Drawing check: duplicate and overlapping lines, duplicate circles and arcs
// SPDX-License-Identifier: MIT
#include "jww.hpp"
#include "output.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace jww {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kHuge = 1e12;  // figures further out than this (corrupt data) are not examined

// The DXF writer's coordinate transform for top-level figures.
struct Space {
    const Drawing& dw;
    const DxfOptions& o;
    double ox = 0, oy = 0;
    Space(const Drawing& d, const DxfOptions& opt) : dw(d), o(opt) {
        if (!o.origin_center && dw.paper != kNoPaper) {
            ox = paper_size(dw.paper).x / 2;
            oy = paper_size(dw.paper).x / 2 / std::sqrt(2.0);
        }
    }
    double scale(const Base& b) const {
        double s = dw.groups[(o.group_scale ? b.glayer : dw.write_glayer) & 15].scale;
        if (o.paper_units || !(s > 0)) return 1;
        return s;
    }
    Pt pt(const Base& b, Pt p) const { double s = scale(b); return {(p.x + ox) * s, (p.y + oy) * s}; }
};

bool fine(Pt p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::fabs(p.x) < kHuge && std::fabs(p.y) < kHuge; }
double dist(Pt a, Pt b) { return std::hypot(a.x - b.x, a.y - b.y); }
int layer_key(const Base& b) { return (b.glayer & 15) * 16 + (b.layer & 15); }

struct UF {
    std::vector<int> p;
    explicit UF(size_t n) : p(n) { std::iota(p.begin(), p.end(), 0); }
    int f(int x) { while (p[x] != x) { p[x] = p[p[x]]; x = p[x]; } return x; }
    void u(int a, int b) { a = f(a); b = f(b); if (a != b) p[std::max(a, b)] = std::min(a, b); }
};

// Uniform grid; cell keys are hashed, so different cells may share a bucket (callers
// always test the geometry of what they find).
struct Grid {
    double cell;
    std::unordered_map<uint64_t, std::vector<int>> m;
    explicit Grid(double c) : cell(c) {}
    int64_t ci(double v) const { return (int64_t)std::floor(v / cell); }
    static uint64_t key(int64_t x, int64_t y) { return uint64_t(x) * 0x9E3779B97F4A7C15ULL ^ (uint64_t(y) + 0x632BE59BD9B4E019ULL); }
    void add(int64_t x, int64_t y, int v) { m[key(x, y)].push_back(v); }
    const std::vector<int>* at(int64_t x, int64_t y) const {
        auto it = m.find(key(x, y));
        return it == m.end() ? nullptr : &it->second;
    }
};

struct Ln { size_t ent; Pt p[2]; double len; int key; uint32_t curve; };  // curve: 曲線属性番号 (0 = none)
struct Ar { size_t ent; Pt c; double r; bool full, circle; Pt s, m, e, q1, q3, ax, bx; int key; };

// collinear pair of lines that overlap (kind 1) or touch end to end (kind 2)
struct Pair { int i, j, kind; Pt a, b; };

struct Analysis {
    double tol = 0, extent = 0;
    std::vector<Ln> L;
    std::vector<Ar> A;
    std::vector<std::vector<int>> dup_lines, dup_arcs;  // groups (indices into L / A)
    std::vector<Pair> pairs;
};

double seg_dist(Pt p, const Ln& l) {
    double dx = l.p[1].x - l.p[0].x, dy = l.p[1].y - l.p[0].y;
    double t = ((p.x - l.p[0].x) * dx + (p.y - l.p[0].y) * dy) / (dx * dx + dy * dy);
    t = std::max(0.0, std::min(1.0, t));
    return dist(p, {l.p[0].x + t * dx, l.p[0].y + t * dy});
}

std::vector<std::vector<int>> groups_of(UF& uf, size_t n) {
    std::unordered_map<int, std::vector<int>> g;
    for (size_t i = 0; i < n; i++) g[uf.f(int(i))].push_back(int(i));
    std::vector<std::vector<int>> out;
    for (auto& [r, v] : g) if (v.size() > 1) out.push_back(v);
    std::sort(out.begin(), out.end());
    return out;
}

Analysis analyze(const Drawing& dw, const DxfOptions& opt, double tol_in, bool with_arcs) {
    Analysis an;
    Space sp(dw, opt);
    // extent of the drawing (lines and arcs)
    double minx = HUGE_VAL, miny = HUGE_VAL, maxx = -HUGE_VAL, maxy = -HUGE_VAL;
    auto grow = [&](Pt p, double r) {
        minx = std::min(minx, p.x - r); maxx = std::max(maxx, p.x + r);
        miny = std::min(miny, p.y - r); maxy = std::max(maxy, p.y + r);
    };
    for (size_t i = 0; i < dw.ents.size(); i++) {
        const Entity& e = dw.ents[i];
        if (e.kind == Kind::Line) {
            Pt a = sp.pt(e.b, e.p[0]), b = sp.pt(e.b, e.p[1]);
            if (!fine(a) || !fine(b)) continue;
            grow(a, 0); grow(b, 0);
            an.L.push_back({i, {a, b}, dist(a, b), layer_key(e.b), e.b.group});
        } else if (e.kind == Kind::Arc && with_arcs) {
            double s = sp.scale(e.b);
            Pt c = sp.pt(e.b, e.p[0]);
            double r = e.r * s;
            if (!fine(c) || !std::isfinite(r) || !(r > 0) || r > kHuge || !std::isfinite(e.flat) || !(e.flat > 0) ||
                !std::isfinite(e.start) || !std::isfinite(e.sweep) || !std::isfinite(e.tilt))
                continue;
            grow(c, r * std::max(1.0, e.flat));
            Ar a;
            a.ent = i; a.c = c; a.r = r; a.key = layer_key(e.b);
            a.full = e.full || std::fabs(e.sweep) >= 2 * kPi - 1e-12;
            a.circle = std::fabs(e.flat - 1.0) < 1e-9;
            double ct = std::cos(e.tilt), st = std::sin(e.tilt), fl = e.flat;
            auto at = [&](double t) {
                double x = r * std::cos(t), y = r * fl * std::sin(t);
                return Pt{c.x + x * ct - y * st, c.y + x * st + y * ct};
            };
            a.s = at(e.start); a.e = at(e.start + e.sweep); a.m = at(e.start + e.sweep / 2);
            a.q1 = at(e.start + e.sweep / 4); a.q3 = at(e.start + e.sweep * 3 / 4);
            a.ax = at(0); a.bx = at(kPi / 2);
            an.A.push_back(a);
        }
    }
    an.extent = maxx >= minx ? std::max(maxx - minx, maxy - miny) : 0;
    double tol = tol_in > 0 ? tol_in : std::max(an.extent * 1e-6, 1e-4);
    an.tol = tol;
    // lines shorter than the tolerance are left alone
    an.L.erase(std::remove_if(an.L.begin(), an.L.end(), [&](const Ln& l) { return !(l.len > tol); }), an.L.end());
    const int n = int(an.L.size());

    // ---- duplicate lines: both end points within tol, either direction
    UF dup(n);
    {
        Grid g(tol);
        for (int k = 0; k < n; k++)
            for (int e = 0; e < 2; e++) g.add(g.ci(an.L[k].p[e].x), g.ci(an.L[k].p[e].y), k * 2 + e);
        for (int k = 0; k < n; k++) {
            const Ln& a = an.L[k];
            int64_t cx = g.ci(a.p[0].x), cy = g.ci(a.p[0].y);
            for (int dx = -1; dx <= 1; dx++)
                for (int dy = -1; dy <= 1; dy++) {
                    auto v = g.at(cx + dx, cy + dy);
                    if (!v) continue;
                    for (int en : *v) {
                        int j = en / 2, e = en % 2;
                        if (j <= k) continue;
                        const Ln& b = an.L[j];
                        if (dist(a.p[0], b.p[e]) <= tol && dist(a.p[1], b.p[1 - e]) <= tol) dup.u(k, j);
                    }
                }
        }
        an.dup_lines = groups_of(dup, n);
    }

    // ---- collinear lines that overlap or touch: some end point of one lies on the other
    if (n > 1) {
        double mean = 0;
        for (auto& l : an.L) mean += l.len;
        mean /= n;
        Grid g(std::max({mean, 4 * tol, an.extent / 4096}));
        for (int k = 0; k < n; k++) {
            const Ln& l = an.L[k];
            double x0 = std::min(l.p[0].x, l.p[1].x), x1 = std::max(l.p[0].x, l.p[1].x);
            double dxl = l.p[1].x - l.p[0].x, dyl = l.p[1].y - l.p[0].y;
            for (int64_t cx = g.ci(x0 - tol), c1 = g.ci(x1 + tol); cx <= c1; cx++) {
                double ua = std::max(x0, cx * g.cell - tol), ub = std::min(x1, (cx + 1) * g.cell + tol);
                double ylo, yhi;
                if (std::fabs(dxl) < 1e-12 * (1 + std::fabs(dyl))) {
                    ylo = std::min(l.p[0].y, l.p[1].y); yhi = std::max(l.p[0].y, l.p[1].y);
                } else {
                    if (ua > ub) continue;
                    double ya = l.p[0].y + (ua - l.p[0].x) * dyl / dxl, yb = l.p[0].y + (ub - l.p[0].x) * dyl / dxl;
                    ylo = std::min(ya, yb); yhi = std::max(ya, yb);
                }
                for (int64_t cy = g.ci(ylo - tol), cy1 = g.ci(yhi + tol); cy <= cy1; cy++) g.add(cx, cy, k);
            }
        }
        std::unordered_set<uint64_t> seen;
        std::vector<std::pair<int, int>> cand;
        for (int k = 0; k < n; k++)
            for (int e = 0; e < 2; e++) {
                Pt p = an.L[k].p[e];
                auto v = g.at(g.ci(p.x), g.ci(p.y));
                if (!v) continue;
                for (int j : *v) {
                    if (j == k || dup.f(j) == dup.f(k)) continue;
                    if (seg_dist(p, an.L[j]) > tol) continue;
                    int a = std::min(j, k), b = std::max(j, k);
                    if (seen.insert(uint64_t(a) << 32 | uint32_t(b)).second) cand.push_back({a, b});
                }
            }
        std::sort(cand.begin(), cand.end());
        for (auto [i, j] : cand) {
            const Ln& lo = an.L[i].len >= an.L[j].len ? an.L[i] : an.L[j];
            const Ln& sh = &lo == &an.L[i] ? an.L[j] : an.L[i];
            double ux = (lo.p[1].x - lo.p[0].x) / lo.len, uy = (lo.p[1].y - lo.p[0].y) / lo.len;
            auto off = [&](Pt q) { return (q.x - lo.p[0].x) * -uy + (q.y - lo.p[0].y) * ux; };
            auto along = [&](Pt q) { return (q.x - lo.p[0].x) * ux + (q.y - lo.p[0].y) * uy; };
            if (std::fabs(off(sh.p[0])) > tol || std::fabs(off(sh.p[1])) > tol) continue;
            double b0 = along(sh.p[0]), b1 = along(sh.p[1]);
            if (b0 > b1) std::swap(b0, b1);
            double s = std::max(0.0, b0), t = std::min(lo.len, b1);
            auto P = [&](double v) { return Pt{lo.p[0].x + ux * v, lo.p[0].y + uy * v}; };
            if (t - s > tol) an.pairs.push_back({i, j, 1, P(s), P(t)});
            // consecutive pieces of one Jw_cad curve (曲線) are short and nearly in line: not news
            else if (t - s >= -tol && !(lo.curve && lo.curve == sh.curve)) an.pairs.push_back({i, j, 2, P((s + t) / 2), P((s + t) / 2)});
        }
    }

    // ---- duplicate circles, ellipses and arcs
    if (with_arcs && !an.A.empty()) {
        const int m = int(an.A.size());
        UF uf(m);
        Grid g(tol);
        for (int k = 0; k < m; k++) g.add(g.ci(an.A[k].c.x), g.ci(an.A[k].c.y), k);
        auto near = [&](Pt a, Pt b) { return dist(a, b) <= tol; };
        auto refl = [](Pt p, Pt c) { return Pt{2 * c.x - p.x, 2 * c.y - p.y}; };
        auto axis_in = [&](Pt p, const Ar& o) {
            return near(p, o.ax) || near(p, refl(o.ax, o.c)) || near(p, o.bx) || near(p, refl(o.bx, o.c));
        };
        auto same = [&](const Ar& a, const Ar& b) {
            if (a.full != b.full || !near(a.c, b.c)) return false;
            if (a.full) {
                if (a.circle && b.circle) return std::fabs(a.r - b.r) <= tol;
                return axis_in(a.ax, b) && axis_in(a.bx, b) && axis_in(b.ax, a) && axis_in(b.bx, a);
            }
            if (!near(a.m, b.m)) return false;
            if (near(a.s, b.s) && near(a.e, b.e)) return near(a.q1, b.q1) && near(a.q3, b.q3);
            if (near(a.s, b.e) && near(a.e, b.s)) return near(a.q1, b.q3) && near(a.q3, b.q1);
            return false;
        };
        for (int k = 0; k < m; k++) {
            int64_t cx = g.ci(an.A[k].c.x), cy = g.ci(an.A[k].c.y);
            for (int dx = -1; dx <= 1; dx++)
                for (int dy = -1; dy <= 1; dy++) {
                    auto v = g.at(cx + dx, cy + dy);
                    if (!v) continue;
                    for (int j : *v)
                        if (j > k && same(an.A[k], an.A[j])) uf.u(k, j);
                }
        }
        an.dup_arcs = groups_of(uf, m);
    }
    return an;
}

}  // namespace

const char* issue_name(Issue::Type t) {
    switch (t) {
        case Issue::DuplicateLine: return "duplicate_lines";
        case Issue::OverlapLines: return "overlapping_lines";
        case Issue::TouchingLines: return "touching_lines";
        case Issue::DuplicateCircle: return "duplicate_circles";
        case Issue::DuplicateArc: return "duplicate_arcs";
    }
    return "?";
}
bool issue_is_info(Issue::Type t) { return t == Issue::TouchingLines; }

Pt dxf_point(const Drawing& dw, const DxfOptions& opt, const Base& b, Pt p) { return Space(dw, opt).pt(b, p); }

CheckResult check_drawing(const Drawing& dw, const DxfOptions& opt, double tol) {
    Analysis an = analyze(dw, opt, tol, true);
    CheckResult r;
    r.tol = an.tol; r.extent = an.extent; r.lines = an.L.size(); r.arcs = an.A.size();
    auto line_issue = [&](Issue::Type t, std::vector<int> idx) {
        std::sort(idx.begin(), idx.end(), [&](int a, int b) { return an.L[a].ent < an.L[b].ent; });
        Issue is;
        is.type = t;
        for (int k : idx) {
            is.ents.push_back(an.L[k].ent);
            is.ends.push_back(an.L[k].p[0]);
            is.ends.push_back(an.L[k].p[1]);
            if (an.L[k].key != an.L[idx[0]].key) is.same_layer = false;
        }
        return is;
    };
    for (auto& grp : an.dup_lines) {
        Issue is = line_issue(Issue::DuplicateLine, grp);
        is.a = is.ends[0]; is.b = is.ends[1];
        r.issues.push_back(is);
    }
    for (auto& p : an.pairs) {
        Issue is = line_issue(p.kind == 1 ? Issue::OverlapLines : Issue::TouchingLines, {p.i, p.j});
        is.a = p.a; is.b = p.b;
        r.issues.push_back(is);
    }
    for (auto& grp : an.dup_arcs) {
        std::vector<int> idx = grp;
        std::sort(idx.begin(), idx.end(), [&](int a, int b) { return an.A[a].ent < an.A[b].ent; });
        Issue is;
        is.type = an.A[idx[0]].full ? Issue::DuplicateCircle : Issue::DuplicateArc;
        for (int k : idx) {
            is.ents.push_back(an.A[k].ent);
            if (an.A[k].key != an.A[idx[0]].key) is.same_layer = false;
        }
        is.a = an.A[idx[0]].c; is.r = an.A[idx[0]].r;
        r.issues.push_back(is);
    }
    std::stable_sort(r.issues.begin(), r.issues.end(), [](const Issue& a, const Issue& b) {
        return a.type != b.type ? a.type < b.type : a.ents < b.ents;
    });
    return r;
}

size_t merge_collinear(Drawing& dw, const DxfOptions& opt, double tol) {
    Analysis an = analyze(dw, opt, tol, false);
    const int n = int(an.L.size());
    if (n < 2) return 0;
    auto mergeable = [&](int i, int j) {
        const Base& a = dw.ents[an.L[i].ent].b;
        const Base& b = dw.ents[an.L[j].ent].b;
        // pieces of Jw_cad curves (曲線属性) are left alone
        return !a.group && !b.group && an.L[i].key == an.L[j].key && a.pen_style == b.pen_style && a.pen_color == b.pen_color && a.pen_width == b.pen_width;
    };
    UF uf(n);
    for (auto& grp : an.dup_lines)
        for (size_t k = 1; k < grp.size(); k++)
            for (size_t q = 0; q < k; q++)
                if (mergeable(grp[q], grp[k])) { uf.u(grp[q], grp[k]); break; }
    for (auto& p : an.pairs)
        if (mergeable(p.i, p.j)) uf.u(p.i, p.j);
    std::vector<char> drop(dw.ents.size(), 0);
    size_t removed = 0;
    for (auto& grp : groups_of(uf, n)) {
        int lo = grp[0], first = grp[0];
        for (int k : grp) {
            if (an.L[k].len > an.L[lo].len) lo = k;
            if (an.L[k].ent < an.L[first].ent) first = k;
        }
        const Ln& L = an.L[lo];
        double ux = (L.p[1].x - L.p[0].x) / L.len, uy = (L.p[1].y - L.p[0].y) / L.len;
        double tmin = HUGE_VAL, tmax = -HUGE_VAL;
        int kmin = -1, emin = 0, kmax = -1, emax = 0;
        bool straight = true;
        for (int k : grp)
            for (int e = 0; e < 2; e++) {
                Pt q = an.L[k].p[e];
                double t = (q.x - L.p[0].x) * ux + (q.y - L.p[0].y) * uy;
                if (std::fabs((q.x - L.p[0].x) * -uy + (q.y - L.p[0].y) * ux) > tol) straight = false;
                if (t < tmin) { tmin = t; kmin = k; emin = e; }
                if (t > tmax) { tmax = t; kmax = k; emax = e; }
            }
        if (!straight) continue;  // a chain that bends by more than the tolerance stays as drawn
        Entity& keep = dw.ents[an.L[first].ent];
        Pt a = dw.ents[an.L[kmin].ent].p[emin], b = dw.ents[an.L[kmax].ent].p[emax];
        // keep the direction of the line that stays
        const Ln& F = an.L[first];
        if ((F.p[1].x - F.p[0].x) * ux + (F.p[1].y - F.p[0].y) * uy < 0) std::swap(a, b);
        keep.p[0] = a; keep.p[1] = b;
        for (int k : grp)
            if (k != first) { drop[an.L[k].ent] = 1; removed++; }
    }
    if (removed) {
        std::vector<Entity> out;
        out.reserve(dw.ents.size() - removed);
        for (size_t i = 0; i < dw.ents.size(); i++)
            if (!drop[i]) out.push_back(std::move(dw.ents[i]));
        dw.ents = std::move(out);
    }
    return removed;
}

}  // namespace jww
