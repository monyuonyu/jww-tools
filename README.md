**English** | [日本語](README.ja.md) | [简体中文](README.zh-CN.md)

# jww-tools

`jww2dxf` converts [Jw_cad](https://www.jwcad.net/) drawings (`.jww`, and registered
symbols `.jws`) to DXF, so that free CAD programs such as FreeCAD, LibreCAD or QCAD can
open them. Jw_cad is a popular Japanese freeware 2D CAD; many drawings exchanged by
Japanese companies are `.jww` files that other programs cannot read.

It is a small C++17 program without dependencies (one static executable for Windows and
Linux). The reader is written from the format description that Jw_cad's author publishes
(`Jw_cadのデータ形式`) and the MFC `CArchive` serialization rules, and was checked figure
by figure against Jw_cad's own DXF export.

It is bundled with [freecad-ja-unified](https://github.com/monyuonyu/freecad-ja-unified),
which opens `.jww` files directly.

## Download

Windows and Linux executables are on the [Releases](https://github.com/monyuonyu/jww-tools/releases)
page (one file each; the Windows build is not signed, SmartScreen may warn).

## Usage

```
jww2dxf drawing.jww                 # writes drawing.dxf (DXF R2000, Shift_JIS text)
jww2dxf drawing.jww out.dxf --utf8  # DXF R2007 with UTF-8 text
jww2dxf --json drawing.jww          # dump everything that was read, as JSON
```

| Option | |
|---|---|
| `--r12` / `--r2000` / `--utf8` | DXF R12 (AC1009) / R2000 (AC1015, default) / R2007 (AC1021, UTF-8) |
| `--explode` | expand blocks instead of writing `INSERT`s |
| `--hatch` | write solids (ソリッド) as `HATCH` instead of `SOLID` pieces |
| `--group-scale` | scale each layer group by its own scale (default: like Jw_cad, all groups at the scale of the current group) |
| `--center` | keep Jw_cad's origin (centre of the sheet); default: like Jw_cad, the lower-left corner of the sheet is (0,0) |
| `--paper` | paper millimetres (no scale) |
| `--text-factor F` | DXF text height = Jw_cad character height × F (default 1; Jw_cad's own export uses 0.83) |
| `--no-temp` | do not write temporary points (仮点) |
| `--no-images` | do not write `IMAGE` entities |
| `--check` (or `--warn-overlaps`) | report duplicate and overlapping figures on stderr (see below) |
| `--strict` | with `--check`: exit status 4 if duplicates or overlaps were found |
| `--tol MM` | tolerance for `--check` and `--merge-collinear`, in DXF millimetres (default: 1e-6 of the drawing's extent, at least 0.0001) |
| `--merge-collinear` | write collinear lines that overlap or touch as one line (see below) |
| `-q` | no warnings |

Exit status: 0 = converted, 1 = cannot read, 3 = the figure list could not be read to the end (the DXF has what was read),
4 = `--strict` and duplicates or overlaps were found (the DXF is written all the same).

### Duplicate and overlapping lines

Outlines in Jw_cad drawings are often split into pieces or drawn twice, which gets in the way when the
converted drawing is edited. `--check` lists them on stderr; the DXF is not changed:

```
$ jww2dxf --check plan.jww
jww2dxf: plan.jww: check: 10948 lines, 23 circles/arcs; tolerance 0.0142547 mm (coordinates as in the DXF)
jww2dxf: plan.jww: duplicate lines: 83 groups (47 on one layer, 36 across layers)
    #13 #34 [01_平面図_柱・壁][06_平面図_ハッチ] (2330, 2009.848481)-(2430, 2009.848481)
    ...
jww2dxf: plan.jww: overlapping collinear lines: 250 pairs (36 on one layer, 214 across layers)
    #35 #67 [06_平面図_ハッチ][01_平面図_柱・壁] overlap (3240, 2009.848481)-(4250, 2009.848481)
    ...
jww2dxf: plan.jww: collinear lines touching end to end (info: could be one line): 176 pairs (64 on one layer, 112 across layers)
jww2dxf: 平面図.jww: check total: duplicate-lines 83, overlapping-lines 250, duplicate-circles 0, duplicate-arcs 0, touching-lines 176
```

- **duplicate lines**: the same end points (within the tolerance), in either direction
- **overlapping collinear lines**: on one line and sharing a stretch longer than the tolerance (also one inside the other)
- **duplicate circles/ellipses** and **duplicate arcs**: same centre, radius, shape and (for arcs) the same end points
- **check total**: the last line gives the count of every kind, always with the same keys (`grep "check total"` to compare many drawings side by side)
- **touching** (information only): on one line and meeting end to end, so they could be one line.
  Consecutive pieces of a Jw_cad curve (曲線) are not reported.

Each kind is counted on one layer and across layers, and the first 10 are shown with their figure numbers
(`#n`, the index in `--json`'s `entities`), DXF layer names and DXF coordinates. `--json` always has a
`check` object (tolerance, extent) and an `issues` array with all of them
(`type`, `severity`, `same_layer`, `entities`, `layers`, `lines` / `overlap` / `at` or `center` / `r`).
Only top-level lines and circles/arcs are examined (not the insides of blocks or dimensions).
The tolerance is absolute, in the DXF's millimetres; the default is one millionth of the drawing's size, at least 0.0001 mm.

`--merge-collinear` writes each run of collinear lines that overlap or touch as one line, when they are on the
same layer with the same line type, colour and width (and not pieces of a curve). The merged line takes the place
of the first of them, with the outermost end points; the others are left out. A run that bends by more than the
tolerance is left as drawn. It is off by default: without it the DXF is the same, byte for byte, as with 0.1.0.

## What is converted

| Jw_cad | DXF |
|---|---|
| line (線) | `LINE` |
| circle / arc (円・円弧), including tilted and flattened ones | `CIRCLE`, `ARC`, `ELLIPSE` |
| point (点), temporary point (仮点) | `POINT` |
| text (文字): font, size, angle, italic, vertical text style, `^` | `TEXT` (Shift_JIS or UTF-8; characters missing in CP932 as `\U+XXXX`) |
| text padded with `・` (均等割付) | `TEXT` with "Fit" alignment, as Jw_cad does |
| dimension (寸法図形) | its line and text |
| solid (ソリッド): quadrilaterals, arc/sector/segment/ring solids, any colour | `SOLID` pieces, or `HATCH` with `--hatch`; RGB colours as true colour |
| block (ブロック), nested, scaled, rotated, mirrored | `BLOCK` + `INSERT` (mirrored blocks with text are expanded, because Jw_cad keeps text readable) |
| image (`^@BM…`), linked or embedded (Ver.7) | `IMAGE`; embedded images are extracted to `<output>_images/` |
| layer groups × layers (16 × 16), their names and hidden state | layers named `<group><layer>_<group name>_<layer name>`, e.g. `0A_1F_通り芯` |
| line types 1–9, random and double-length line types | `LTYPE`s with Jw_cad's patterns |
| pen colours 1–9 and SXF colours, line widths | ACI colours (1 cyan→4, 2 black→7, 3 green→3, 4 yellow→2, 5 magenta→6, 6 blue→5, 7→4, 8 red→1, 9 grey→8), true colours, line weights |

File versions: all data versions found in the wild — 220, 230, 300, 351, 420, 600 and 700
(Jw_cad for Windows 2.x up to the current 10.x, which still writes 700), with ANSI (Shift_JIS)
or Unicode strings. Older layouts that are not described (e.g. version 65) are read on a
best-effort basis. DOS Jw_cad `.jwc` files are not supported.

## How it was checked

Jw_cad 10.03.7 exports DXF through `JWW2DXFConv.dll`. That DLL was driven directly
(`ExecDxfExport(hwnd, jww, dxf)`) on Windows to produce reference DXFs, and every figure of our
output was matched against them (type, layer group/layer, coordinates within 1e-6 of the drawing
size, text strings and angles, filled areas) with the scripts in `tools/oracle/`.

307 drawings — public sample drawings, old Jw_cad distributions and the ezjww test drawings,
versions 220 (13), 230 (14), 300 (1), 351 (23), 420 (25), 600 (19) and 700 (212):

| | Jw_cad | jww2dxf | matched |
|---|---:|---:|---:|
| LINE | 152,625 | 152,625 | 100 % |
| ARC / CIRCLE / ELLIPSE | 10,001 | 10,001 | 100 % |
| POINT | 2,313 | 2,313 | 100 % |
| TEXT | 22,116 | 22,116 | 100 % |
| solid (area) | 1,085 | 1,085 | 100 % |
| image | 16 | 16 | 100 % |

Block-preserving output was also checked by expanding its `INSERT`s with ezdxf (20 drawings
with blocks, all equal), and every output passes `ezdxf`'s audit in all DXF versions. The
Windows and Linux executables write identical files.

Deliberate differences from Jw_cad's export: colours use the usual ACI numbers (Jw_cad writes
0 for black), text height is the Jw_cad character height (Jw_cad multiplies by 0.83, use
`--text-factor 0.83` to get the same), text width is kept as a width factor, layer names also
contain the layer group name, and images become `IMAGE` entities instead of `^@BM` text.

## Building

```
cmake -S . -B build && cmake --build build && (cd build && ctest)
```

Options: `-DJWW_STATIC=ON` links statically. Windows builds use MinGW-w64 (see
`.github/workflows/build.yml`).

## License

MIT (see `LICENSE`). The test drawings in `tests/data` come from
[ezjww](https://github.com/monozukuri-ai/ezjww) (MIT). No Jw_cad code is used; Jw_cad is
© Jiro Shimizu & Yoshifumi Tanaka.
