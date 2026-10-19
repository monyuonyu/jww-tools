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
| `-q` | no warnings |

Exit status: 0 = converted, 1 = cannot read, 3 = the figure list could not be read to the end (the DXF has what was read).

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
