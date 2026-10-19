#!/usr/bin/env python3
"""Compare our jww2dxf output with Jw_cad's own DXF export (JWW2DXFConv.dll), entity by entity.

usage: jwwdiff.py OURS.dxf REF.dxf JWW.json [--verbose]
  JWW.json is `jww2dxf --json` of the source drawing (for the layer group scales and paper size).
Exit status 0 = everything matched.

Frames: Jw_cad's exporter puts the lower-left corner of the paper at (0,0)
(height = width/sqrt(2)); we keep the Jw_cad origin (centre of the paper).
The offset per layer group is (W/2, W/2/sqrt2) * scale.
"""
import json
import math
import os
import sys
import collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dxfcat import pairs  # noqa: E402

PAPER_W = {0: 1189, 1: 841, 2: 594, 3: 420, 4: 297, 8: 1682, 9: 2378, 10: 3364, 11: 4756, 12: 10000, 13: 50000, 14: 100000}


def read_entities(path):
    """Entities of the ENTITIES section as dicts of group code -> list of values."""
    ents = []
    sec = None
    prev0 = None
    cur = None
    for code, v in pairs(path):
        v = v.strip()
        if code == '0':
            if cur is not None and sec == 'ENTITIES':
                ents.append(cur)
            cur = None
            if v in ('SECTION', 'ENDSEC', 'EOF'):
                prev0 = v
                if v == 'ENDSEC':
                    sec = None
                continue
            if sec == 'ENTITIES':
                if v in ('VERTEX', 'SEQEND'):
                    cur = None
                    continue
                cur = {'type': v, 'g': collections.defaultdict(list), 'seq': []}
            prev0 = v
            continue
        if code == '2' and prev0 == 'SECTION':
            sec = v
            prev0 = None
            continue
        if cur is not None:
            c = int(code)
            cur['g'][c].append(v)
            cur['seq'].append((c, v))
    return ents


def f(e, c, i=0, d=0.0):
    try:
        return float(e['g'][c][i])
    except (IndexError, ValueError):
        return d


def s(e, c, d=''):
    v = e['g'].get(c)
    return v[0] if v else d


def hatch_polys(e):
    """Boundary paths of a HATCH as point lists (arcs approximated)."""
    seq = e['seq']
    polys = []
    i = 0
    n = len(seq)
    while i < n:
        c, v = seq[i]
        if c == 92:
            flags = int(v)
            i += 1
            pts = []
            if flags & 2:  # polyline
                has_bulge = 0
                while i < n and seq[i][0] not in (10,):
                    if seq[i][0] == 72:
                        has_bulge = int(seq[i][1])
                    i += 1
                verts = []
                while i < n and seq[i][0] in (10, 20, 42):
                    if seq[i][0] == 10:
                        verts.append([float(seq[i][1]), 0.0, 0.0])
                    elif seq[i][0] == 20:
                        verts[-1][1] = float(seq[i][1])
                    else:
                        verts[-1][2] = float(seq[i][1])
                    i += 1
                for k, (x, y, b) in enumerate(verts):
                    pts.append((x, y))
                    if has_bulge and b:
                        x2, y2, _ = verts[(k + 1) % len(verts)]
                        pts.extend(bulge_pts(x, y, x2, y2, b))
            else:  # edges
                while i < n and seq[i][0] != 93:
                    i += 1
                i += 1
                while i < n and seq[i][0] == 72:
                    et = int(seq[i][1])
                    i += 1
                    vals = collections.defaultdict(list)
                    while i < n and seq[i][0] not in (72, 92, 97, 75):
                        vals[seq[i][0]].append(float(seq[i][1]))
                        i += 1
                    if et == 1:
                        pts.append((vals[10][0], vals[20][0]))
                        pts.append((vals[11][0], vals[21][0]))
                    elif et == 2:
                        cx, cy, r, a0, a1 = vals[10][0], vals[20][0], vals[40][0], vals[50][0], vals[51][0]
                        ccw = vals[73][0] if vals[73] else 1
                        if not ccw:
                            a0, a1 = -a0, -a1
                        if a1 < a0:
                            a1 += 360
                        for k in range(257):
                            t = math.radians(a0 + (a1 - a0) * k / 256)
                            pts.append((cx + r * math.cos(t), cy + r * math.sin(t)))
                    elif et == 3:
                        cx, cy, mx, my, ratio, a0, a1 = vals[10][0], vals[20][0], vals[11][0], vals[21][0], vals[40][0], vals[50][0], vals[51][0]
                        ccw = vals[73][0] if vals[73] else 1
                        if not ccw:
                            a0, a1 = -a0, -a1
                        if a1 < a0:
                            a1 += 360
                        rot = math.atan2(my, mx)
                        a = math.hypot(mx, my)
                        for k in range(257):
                            t = math.radians(a0 + (a1 - a0) * k / 256)
                            x, y = a * math.cos(t), a * ratio * math.sin(t)
                            pts.append((cx + x * math.cos(rot) - y * math.sin(rot), cy + x * math.sin(rot) + y * math.cos(rot)))
                    if i < n and seq[i][0] == 97:
                        break
            if pts:
                polys.append(pts)
            continue
        i += 1
    return polys


def bulge_pts(x1, y1, x2, y2, b):
    ang = 4 * math.atan(b)
    d = math.hypot(x2 - x1, y2 - y1)
    if d == 0:
        return []
    r = d / (2 * math.sin(ang / 2))
    mx, my = (x1 + x2) / 2, (y1 + y2) / 2
    h = r * math.cos(ang / 2)
    nx, ny = -(y2 - y1) / d, (x2 - x1) / d
    cx, cy = mx + nx * h, my + ny * h
    a0 = math.atan2(y1 - cy, x1 - cx)
    out = []
    for k in range(1, 16):
        t = a0 + ang * k / 16
        out.append((cx + abs(r) * math.cos(t), cy + abs(r) * math.sin(t)))
    return out


def unesc(t):
    import re
    t = re.sub(r'\\U\+([0-9A-Fa-f]{4})', lambda m: chr(int(m.group(1), 16)), t)
    return t.encode('utf-16', 'surrogatepass').decode('utf-16')


def area(pts):
    a = 0.0
    for i in range(len(pts)):
        x1, y1 = pts[i]
        x2, y2 = pts[(i + 1) % len(pts)]
        a += x1 * y2 - x2 * y1
    return abs(a) / 2


def solid_poly(e):
    p = [(f(e, 10), f(e, 20)), (f(e, 11), f(e, 21)), (f(e, 13), f(e, 23)), (f(e, 12), f(e, 22))]
    return p


def attrs(e):
    return {'lt': s(e, 6, 'BYLAYER'), 'c': s(e, 62, '256'), 'tc': s(e, 420, ''), 'layer': s(e, 8), 'style': s(e, 7, ''), 'w': f(e, 41, d=1.0) if e['type'] == 'TEXT' else None}


def canon(e, off):
    r = canon0(e, off)
    if r:
        r[3]['_a'] = attrs(e)
    return r


def canon0(e, off):
    """(kind, layer code, feature vector, extra) for an entity, translated by off(layercode)."""
    t = e['type']
    lay = s(e, 8)
    code = lay[:2].upper() if len(lay) >= 2 else lay
    ox, oy = off(code)

    def P(cx, cy):
        return (f(e, cx) + ox, f(e, cy) + oy)
    if t == 'LINE':
        a, b = P(10, 20), P(11, 21)
        if b < a:
            a, b = b, a
        return 'LINE', code, a + b, {}
    if t in ('ARC', 'CIRCLE'):
        cx, cy = P(10, 20)
        r = f(e, 40)
        if t == 'CIRCLE':
            a0, a1 = 0.0, 360.0
        else:
            a0, a1 = f(e, 50), f(e, 51)
        if f(e, 230, d=1.0) < 0:  # OCS with a mirrored extrusion (e.g. ezdxf's explode of mirrored INSERTs)
            cx = -(cx - ox) + ox
            a0, a1 = 180 - a1, 180 - a0
        sweep = (a1 - a0) % 360
        if sweep < 1e-9 and t == 'ARC' and abs(a1 - a0) > 1e-9:
            sweep = 360
        if abs(sweep - 360) < 1e-6 or (t == 'ARC' and abs(a1 - a0 - 360) < 1e-6) or sweep == 0:
            return 'CIRCLE', code, (cx, cy, r), {}
        p0 = (cx + r * math.cos(math.radians(a0)), cy + r * math.sin(math.radians(a0)))
        p1 = (cx + r * math.cos(math.radians(a1)), cy + r * math.sin(math.radians(a1)))
        return 'ARC', code, (cx, cy, r) + p0 + p1, {}
    if t == 'ELLIPSE':
        cx, cy = P(10, 20)
        mx, my = f(e, 11), f(e, 21)
        ratio = f(e, 40)
        p0, p1 = f(e, 41), f(e, 42, d=2 * math.pi)
        if p1 <= p0:
            p1 += 2 * math.pi
        rot = math.atan2(my, mx)
        a = math.hypot(mx, my)
        if ratio > 1:  # invalid in DXF but written by some exporters: swap the axes
            a, ratio, rot, p0, p1 = a * ratio, 1 / ratio, rot + math.pi / 2, p0 - math.pi / 2, p1 - math.pi / 2

        def at(t_):
            x, y = a * math.cos(t_), a * ratio * math.sin(t_)
            return (cx + x * math.cos(rot) - y * math.sin(rot), cy + x * math.sin(rot) + y * math.cos(rot))
        full = abs((p1 - p0) - 2 * math.pi) < 1e-6
        mid = at((p0 + p1) / 2)
        if full:
            return 'ELLIPSE', code, (cx, cy, a, a * ratio, 0, 0, 0, 0, 0, 0), {}
        return 'ELLIPSE', code, (cx, cy, a, a * ratio) + at(p0) + at(p1) + mid, {}
    if t == 'POINT':
        return 'POINT', code, P(10, 20), {}
    if t == 'IMAGE':
        return 'IMAGE', code, P(10, 20), {}
    if t == 'TEXT' and s(e, 1).startswith('^@BM'):  # Jw_cad's exporter keeps image placements as text
        return 'IMAGE', code, P(10, 20), {}
    if t == 'TEXT':
        x, y = P(10, 20)
        return 'TEXT', code, (x, y), {'str': unesc(s(e, 1)), 'h': f(e, 40), 'rot': f(e, 50) % 360, 'w': f(e, 41, d=1.0)}
    if t in ('SOLID', 'HATCH'):
        polys = [solid_poly(e)] if t == 'SOLID' else hatch_polys(e)
        pts = [p for poly in polys for p in poly]
        if not pts:
            return None
        xs = [p[0] + ox for p in pts]
        ys = [p[1] + oy for p in pts]
        return 'FILL', code, (min(xs), min(ys), max(xs), max(ys)), {'area': sum(area(p) for p in polys)}
    return None


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    verbose = '--verbose' in sys.argv
    ours_p, ref_p, json_p = args[:3]
    jw = json.load(open(json_p, encoding='utf-8'))
    scales = [g['scale'] for g in jw['groups']]
    W = PAPER_W.get(jw['paper'], 420)

    def off_ours(code):
        if '--center' not in sys.argv:
            return (0, 0)
        try:
            gi = int(code[0], 16)
        except ValueError:
            return (0, 0)
        sc = scales[gi] if scales[gi] > 0 else 1
        return (W / 2 * sc, W / 2 / math.sqrt(2) * sc)

    ours = [c for c in (canon(e, off_ours) for e in read_entities(ours_p)) if c]
    ref = [c for c in (canon(e, lambda code: (0, 0)) for e in read_entities(ref_p)) if c]

    # fills: our tessellated arc solids are several SOLIDs; merge ours by identical-ish group later.
    ext = 1.0
    for c in ref:
        if c[0] != 'TEXT':
            ext = max(ext, max(abs(v) for v in c[2][:2]))
    tol = max(1e-3, 2e-6 * ext)

    buckets = collections.defaultdict(list)
    for i, c in enumerate(ref):
        buckets[(c[0], c[1])].append(i)
    used = set()
    stats = collections.Counter()
    problems = []
    height_ratios = []
    pairs_lt, pairs_c, pairs_layer, pairs_w = collections.Counter(), collections.Counter(), collections.Counter(), collections.Counter()

    def close(a, b, t):
        return len(a) == len(b) and all(abs(x - y) <= t * (1 + (abs(y) / ext if False else 0)) for x, y in zip(a, b))

    our_fill_bins = collections.defaultdict(list)
    for c in ours:
        if c[0] == 'FILL':
            our_fill_bins[c[1]].append(c)
            continue
        stats[c[0] + '.ours'] += 1
        cand = buckets.get((c[0], c[1]), [])
        alts = [c[2]]
        if c[0] == 'LINE':
            alts.append(c[2][2:] + c[2][:2])
        hit = None
        for j in cand:
            if j in used:
                continue
            r = ref[j]
            t2 = tol * (10 if c[0] in ('ARC', 'ELLIPSE') else 1)
            if not any(close(a, r[2] if c[0] != 'LINE' else r[2], t2) or (c[0] == 'LINE' and close(a, r[2][2:] + r[2][:2], t2)) for a in alts):
                continue
            if c[0] == 'TEXT':
                if c[3]['str'] != r[3]['str']:
                    continue
                dr = abs((c[3]['rot'] - r[3]['rot'] + 180) % 360 - 180)
                if dr > 0.01:
                    continue
            hit = j
            break
        if hit is None:
            stats[c[0] + '.unmatched_ours'] += 1
            problems.append(('ours-only', c))
        else:
            used.add(hit)
            stats[c[0] + '.matched'] += 1
            a, b = c[3]['_a'], ref[hit][3]['_a']
            pairs_lt[(a['lt'], b['lt'])] += 1
            pairs_c[((a['c'], a['tc']), (b['c'], b['tc']))] += 1
            pairs_layer[(a['layer'], b['layer'])] += 1
            if c[0] == 'TEXT':
                pairs_w[(round(a['w'], 3), round(b['w'], 3))] += 1
            if c[0] == 'TEXT' and c[3]['h'] > 0:
                height_ratios.append(round(ref[hit][3]['h'] / c[3]['h'], 4))
    # fills: compare total area and bbox per layer code
    ref_fill_bins = collections.defaultdict(list)
    for j, r in enumerate(ref):
        if r[0] == 'FILL':
            ref_fill_bins[r[1]].append(r)
            used.add(j)
    for code in set(our_fill_bins) | set(ref_fill_bins):
        a = sum(c[3]['area'] for c in our_fill_bins.get(code, []))
        b = sum(c[3]['area'] for c in ref_fill_bins.get(code, []))
        stats['FILL.ref'] += len(ref_fill_bins.get(code, []))
        stats['FILL.ours'] += len(our_fill_bins.get(code, []))
        if abs(a - b) <= 5e-3 * max(a, b, 1e-9) + tol * tol:
            stats['FILL.area_match'] += len(ref_fill_bins.get(code, []))
        else:
            stats['FILL.area_mismatch'] += len(ref_fill_bins.get(code, [])) or 1
            problems.append(('fill-area', (code, a, b)))
    for j, r in enumerate(ref):
        if r[0] == 'FILL':
            continue
        stats[r[0] + '.ref'] += 1
        if j not in used:
            stats[r[0] + '.unmatched_ref'] += 1
            problems.append(('ref-only', r))
    hr = collections.Counter(height_ratios)
    result = {'stats': dict(stats), 'text_height_ratio': hr.most_common(3), 'tol': tol,
              'lt': [[k[0], k[1], v] for k, v in pairs_lt.items()], 'color': [[list(k[0]), list(k[1]), v] for k, v in pairs_c.items()],
              'layer': [[k[0], k[1], v] for k, v in pairs_layer.items()], 'wfactor': [[k[0], k[1], v] for k, v in pairs_w.items()]}
    print(json.dumps(result, ensure_ascii=False))
    if verbose:
        for p in problems[:40]:
            print(p)
    bad = any(k.endswith('unmatched_ours') or k.endswith('unmatched_ref') or k.endswith('area_mismatch') for k in stats)
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
