[English](README.md) | **日本語** | [简体中文](README.zh-CN.md)

> [README.md](README.md)（英語版が原文）の翻訳です。

# jww-tools

`jww2dxf` は [Jw_cad](https://www.jwcad.net/) の図面（`.jww`、図形登録の `.jws`）を DXF に変換します。
FreeCAD・LibreCAD・QCAD などの無料の CAD で Jw_cad の図面を開けるようにするためのものです。
Jw_cad は日本で広く使われている無料の 2D CAD で、会社の図面の多くが `.jww` ですが、ほかのソフトでは開けません。

依存ライブラリの無い小さな C++17 のプログラムです（Windows・Linux とも実行ファイル 1 つ）。
読み取り部分は、Jw_cad の作者が公開している「Jw_cadのデータ形式」と MFC の `CArchive` の保存形式から書きました。
Jw_cad 自身の DXF 出力と、図形ごとに突き合わせて確かめています。

[freecad-ja-unified](https://github.com/monyuonyu/freecad-ja-unified)（日本語化した FreeCAD）に同梱していて、`.jww` をそのまま開けます。

## ダウンロード

Windows 用と Linux 用の実行ファイルは [Releases](https://github.com/monyuonyu/jww-tools/releases) にあります
（どちらもファイル 1 つ。Windows 版は署名していないので、SmartScreen の警告が出ることがあります）。

## 使い方

```
jww2dxf 図面.jww                 # 図面.dxf を書く（DXF R2000、文字は Shift_JIS）
jww2dxf 図面.jww 出力.dxf --utf8  # DXF R2007、文字は UTF-8
jww2dxf --json 図面.jww          # 読んだ内容を全部 JSON で出す
```

| オプション | |
|---|---|
| `--r12` / `--r2000` / `--utf8` | DXF R12（AC1009）／R2000（AC1015、既定）／R2007（AC1021、UTF-8） |
| `--explode` | ブロックを分解して書く（`INSERT` を使わない） |
| `--hatch` | ソリッドを `SOLID` の小片でなく `HATCH` で書く |
| `--group-scale` | レイヤグループごとに、それぞれの縮尺で実寸にする（既定は Jw_cad と同じく、全部を書込レイヤグループの縮尺で） |
| `--center` | Jw_cad の原点（用紙の中心）のままにする（既定は Jw_cad と同じく、用紙の左下が (0,0)） |
| `--paper` | 用紙上の mm のまま（縮尺をかけない） |
| `--text-factor F` | DXF の文字の高さ = Jw_cad の文字の高さ × F（既定 1。Jw_cad 自身の出力は 0.83） |
| `--no-temp` | 仮点を書かない |
| `--no-images` | 画像（`IMAGE`）を書かない |
| `--check`（`--warn-overlaps` でも可） | 重なった線・二重の線などを標準エラーに出す（下を参照） |
| `--strict` | `--check` と一緒に使い、見つかったら終了コード 4 |
| `--tol MM` | `--check` と `--merge-collinear` の許容差。DXF 上の mm（既定は図面の大きさの 100 万分の 1、最小 0.0001） |
| `--merge-collinear` | 一直線上で重なる・つながる線を 1 本にして書く（下を参照） |
| `-q` | 警告を出さない |

終了コード：0 = 変換できた、1 = 読めない、3 = 図形の並びを最後まで読めなかった（読めたところまでは DXF にある）、
4 = `--strict` で、二重の線や重なりが見つかった（DXF は普通に書く）。

### 二重の線・重なった線

Jw_cad の図面では、外形線が細切れになっていたり、二重に引かれていたりすることがよくあり、変換した図面を編集するときに邪魔になります。
`--check` を付けると、それを標準エラーに出します（DXF は変わりません）：

```
$ jww2dxf --check 平面図.jww
jww2dxf: 平面図.jww: check: 10948 lines, 23 circles/arcs; tolerance 0.0142547 mm (coordinates as in the DXF)
jww2dxf: 平面図.jww: duplicate lines: 83 groups (47 on one layer, 36 across layers)
    #13 #34 [01_平面図_柱・壁][06_平面図_ハッチ] (2330, 2009.848481)-(2430, 2009.848481)
    ...
jww2dxf: 平面図.jww: overlapping collinear lines: 250 pairs (36 on one layer, 214 across layers)
    #35 #67 [06_平面図_ハッチ][01_平面図_柱・壁] overlap (3240, 2009.848481)-(4250, 2009.848481)
    ...
jww2dxf: 平面図.jww: collinear lines touching end to end (info: could be one line): 176 pairs (64 on one layer, 112 across layers)
jww2dxf: 平面図.jww: check total: duplicate-lines 83, overlapping-lines 250, duplicate-circles 0, duplicate-arcs 0, touching-lines 176
```

- **duplicate lines（二重の線）**：両端が（許容差の中で）同じ。向きが逆でも
- **overlapping collinear lines（重なった線）**：一直線上にあって、許容差より長く重なっている（片方がもう片方に含まれる場合も）
- **duplicate circles/ellipses・duplicate arcs（二重の円・円弧）**：中心・半径・形と、円弧なら両端も同じ
- **check total**：最後の 1 行に、種類ごとの数をいつも同じ並びで出す（`grep "check total"` で複数の図面を並べて比べられる）
- **touching（つながった線、参考）**：一直線上で端と端がつながっていて、1 本にできる。Jw_cad の曲線（曲線属性）のつなぎ目は出しません

種類ごとに、同じレイヤの中のものとレイヤをまたぐものを数え、はじめの 10 件を図形の番号（`#n`、`--json` の `entities` の順番）、
DXF の画層名、DXF 上の座標で示します。`--json` には常に `check`（許容差・図面の大きさ）と、全件の `issues` が入ります
（`type`・`severity`・`same_layer`・`entities`・`layers`・`lines` / `overlap` / `at` か `center` / `r`）。
調べるのは図面に直接ある線と円・円弧だけです（ブロックや寸法図形の中は見ません）。
許容差は DXF 上の mm での絶対値で、既定は図面の大きさの 100 万分の 1（最小 0.0001 mm）です。

`--merge-collinear` は、一直線上で重なる・つながる線を 1 本にして DXF に書きます。まとめるのは、同じレイヤで線種・線色・線幅が同じもの
だけです（曲線の部品はまとめません）。いちばん先の線を両端いっぱいに伸ばし、残りは書きません。許容差より曲がっている並びはそのままにします。
既定では使いません。付けなければ、DXF は 0.1.0 とバイト単位で同じです。

## 変換するもの

| Jw_cad | DXF |
|---|---|
| 線 | `LINE` |
| 円・円弧（傾き・扁平も） | `CIRCLE`・`ARC`・`ELLIPSE` |
| 点・仮点 | `POINT` |
| 文字（フォント・大きさ・角度・斜体・縦字・`^`） | `TEXT`（Shift_JIS か UTF-8。CP932 に無い文字は `\U+XXXX`） |
| 末尾を「・」で埋めた文字（均等割付） | Jw_cad と同じく、両端揃え（Fit）の `TEXT` |
| 寸法図形 | 線と文字 |
| ソリッド（四角形、円・扇形・弓形・円環のソリッド、任意色） | `SOLID` の小片、`--hatch` なら `HATCH`。任意色は RGB で |
| ブロック（入れ子・倍率・回転・反転） | `BLOCK` と `INSERT`（文字を含む反転ブロックは分解する。Jw_cad は文字を裏返さないため） |
| 画像（`^@BM…`）、リンクも同梱（Ver.7）も | `IMAGE`。同梱の画像は `<出力名>_images/` に取り出す |
| レイヤグループ×レイヤ（16×16）、名前、非表示 | 画層名 `<グループ><レイヤ>_<グループ名>_<レイヤ名>`（例：`0A_1F_通り芯`） |
| 線種 1〜9、ランダム線、倍長線種 | Jw_cad の模様の `LTYPE` |
| 線色 1〜9 と SXF の色、線の太さ | ACI の色（1 水色→4、2 黒→7、3 緑→3、4 黄→2、5 ピンク→6、6 青→5、7→4、8 赤→1、9 灰→8）、RGB、線の太さ |

ファイルの版：実際に出回っているデータの版をすべて読みます — 220、230、300、351、420、600、700
（Jw_cad for Windows 2.x から、今も 700 で保存する現在の 10.x まで）。文字列は ANSI（Shift_JIS）と Unicode の両方。
形式の説明が無い古い版（たとえば 65）はできる範囲で読みます。DOS 版 Jw_cad の `.jwc` には対応していません。

## 確かめたこと

Jw_cad 10.03.7 は `JWW2DXFConv.dll` で DXF を書き出します。この DLL を Windows で直接呼んで（`ExecDxfExport(hwnd, jww, dxf)`）
基準の DXF を作り、こちらの出力の図形を一つずつ突き合わせました（種類、レイヤグループとレイヤ、座標（図面の大きさの 1e-6 以内）、
文字列と角度、塗りの面積）。照合の道具は `tools/oracle/` にあります。

307 図面（公開されている見本図面、昔の Jw_cad の配布物、ezjww の試験図面）。版は 220（13）、230（14）、300（1）、351（23）、
420（25）、600（19）、700（212）：

| | Jw_cad | jww2dxf | 一致 |
|---|---:|---:|---:|
| LINE | 152,625 | 152,625 | 100 % |
| ARC / CIRCLE / ELLIPSE | 10,001 | 10,001 | 100 % |
| POINT | 2,313 | 2,313 | 100 % |
| TEXT | 22,116 | 22,116 | 100 % |
| ソリッド（面積） | 1,085 | 1,085 | 100 % |
| 画像 | 16 | 16 | 100 % |

ブロックを残す出力も、`INSERT` を ezdxf で分解して比べました（ブロックのある 20 図面、すべて一致）。
出力はすべての DXF の版で ezdxf の検査を通ります。Windows 版と Linux 版の出力は同じです。

Jw_cad の出力と、わざと変えているところ：色は普通の ACI の番号（Jw_cad は黒を 0 で書く）、
文字の高さは Jw_cad の文字の高さのまま（Jw_cad は 0.83 倍する。同じにするなら `--text-factor 0.83`）、
文字の幅は幅の係数で残す、画層名にレイヤグループ名も入れる、画像は `^@BM` の文字でなく `IMAGE` にする。

## ビルド

```
cmake -S . -B build && cmake --build build && (cd build && ctest)
```

`-DJWW_STATIC=ON` で静的にリンクします。Windows 版は MinGW-w64 で作ります（`.github/workflows/build.yml`）。

## ライセンス

MIT（`LICENSE`）。`tests/data` の試験図面は [ezjww](https://github.com/monozukuri-ai/ezjww)（MIT）のものです。
Jw_cad のコードは使っていません。Jw_cad の著作権は清水治郎・田中善文の両氏にあります。
