[English](README.md) | [日本語](README.ja.md) | **简体中文**

> 本文译自 [README.md](README.md)（英文版为原文）。

# jww-tools

`jww2dxf` 把 [Jw_cad](https://www.jwcad.net/) 图纸（`.jww`，以及图形登录文件 `.jws`）转换为 DXF，
让 FreeCAD、LibreCAD、QCAD 等免费 CAD 能够打开。Jw_cad 是日本广泛使用的免费 2D CAD，
日本企业之间交换的很多图纸都是 `.jww`，而其他软件无法读取。

这是一个没有外部依赖的小型 C++17 程序（Windows 和 Linux 各一个静态可执行文件）。
读取部分依据 Jw_cad 作者公开的格式说明（「Jw_cadのデータ形式」）和 MFC `CArchive` 的序列化规则编写，
并与 Jw_cad 自身的 DXF 导出逐个图形进行了比对。

它随 [freecad-ja-unified](https://github.com/monyuonyu/freecad-ja-unified) 一起发布，可以直接打开 `.jww`。

## 下载

Windows 和 Linux 的可执行文件见 [Releases](https://github.com/monyuonyu/jww-tools/releases)
（各一个文件；Windows 版未签名，SmartScreen 可能会提示警告）。

## 用法

```
jww2dxf drawing.jww                 # 生成 drawing.dxf（DXF R2000，文字为 Shift_JIS）
jww2dxf drawing.jww out.dxf --utf8  # DXF R2007，文字为 UTF-8
jww2dxf --json drawing.jww          # 以 JSON 输出读取到的全部内容
```

| 选项 | |
|---|---|
| `--r12` / `--r2000` / `--utf8` | DXF R12（AC1009）/ R2000（AC1015，默认）/ R2007（AC1021，UTF-8） |
| `--explode` | 分解图块，不写 `INSERT` |
| `--hatch` | 实心填充（ソリッド）写成 `HATCH`，而不是 `SOLID` 小块 |
| `--group-scale` | 每个图层组按各自的比例换算（默认与 Jw_cad 相同：全部按当前写入图层组的比例） |
| `--center` | 保留 Jw_cad 的原点（图纸中心）；默认与 Jw_cad 相同，图纸左下角为 (0,0) |
| `--paper` | 保持图纸上的毫米（不乘比例） |
| `--text-factor F` | DXF 文字高度 = Jw_cad 文字高度 × F（默认 1；Jw_cad 自身导出为 0.83） |
| `--no-temp` | 不输出临时点（仮点） |
| `--no-images` | 不输出 `IMAGE` |
| `--check`（也可写作 `--warn-overlaps`） | 在标准错误输出中报告重复和重叠的图形（见下文） |
| `--strict` | 与 `--check` 一起使用：发现重复或重叠时退出码为 4 |
| `--tol MM` | `--check` 和 `--merge-collinear` 的容差，单位为 DXF 中的毫米（默认：图面尺寸的百万分之一，最小 0.0001） |
| `--merge-collinear` | 将共线且重叠或相接的直线合并为一条写出（见下文） |
| `-q` | 不显示警告 |

退出码：0 = 转换成功，1 = 无法读取，3 = 图形列表未能读到最后（DXF 中包含已读取的部分），
4 = 使用 `--strict` 且发现了重复或重叠（DXF 照常写出）。

### 重复和重叠的直线

Jw_cad 图面中的轮廓线经常被分成多段或画了两遍，编辑转换后的图面时会造成麻烦。`--check` 会把它们列在标准错误输出中（DXF 不变）：

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

- **duplicate lines（重复直线）**：两端点（在容差内）相同，方向相反也算
- **overlapping collinear lines（重叠直线）**：位于同一直线上，且重叠部分长于容差（包括一条包含另一条的情况）
- **duplicate circles/ellipses、duplicate arcs（重复的圆、圆弧）**：圆心、半径、形状以及（圆弧的）两端点相同
- **check total**：最后一行总是以相同的顺序列出各类的数量（用 `grep "check total"` 可以并排比较多张图纸）
- **touching（相接，仅供参考）**：位于同一直线上且首尾相接，可以合并为一条。Jw_cad 曲线（曲線属性）的各段之间不报告

每一类分别统计同一图层内和跨图层的数量，并显示前 10 个，附图形编号（`#n`，即 `--json` 中 `entities` 的序号）、DXF 图层名和 DXF 坐标。
`--json` 总是包含 `check`（容差、图面尺寸）和列出全部问题的 `issues`
（`type`、`severity`、`same_layer`、`entities`、`layers`、`lines` / `overlap` / `at` 或 `center` / `r`）。
只检查图面中直接存在的直线和圆、圆弧（不检查图块和尺寸图形的内部）。
容差是 DXF 中以毫米为单位的绝对值，默认为图面尺寸的百万分之一，最小 0.0001 mm。

`--merge-collinear` 将共线且重叠或相接的一组直线作为一条写入 DXF，仅合并位于同一图层、线型、颜色和线宽都相同的直线（不合并曲线的各段）。
合并后的直线取代其中最前面的一条，取最外侧的端点，其余的不再写出。弯曲超过容差的一组保持原样。
默认不启用；不使用时，DXF 与 0.1.0 逐字节相同。

## 转换内容

| Jw_cad | DXF |
|---|---|
| 线 | `LINE` |
| 圆、圆弧（包括倾斜和扁平） | `CIRCLE`、`ARC`、`ELLIPSE` |
| 点、临时点 | `POINT` |
| 文字（字体、大小、角度、斜体、竖排、`^`） | `TEXT`（Shift_JIS 或 UTF-8；CP932 中没有的字符写成 `\U+XXXX`） |
| 末尾用「・」填充的文字（均等分布） | 与 Jw_cad 相同，写成 Fit 对齐的 `TEXT` |
| 尺寸标注 | 线和文字 |
| 实心填充（四边形，圆、扇形、弓形、圆环填充，任意颜色） | `SOLID` 小块，或 `--hatch` 时的 `HATCH`；任意颜色写成真彩色 |
| 图块（嵌套、缩放、旋转、镜像） | `BLOCK` 和 `INSERT`（含文字的镜像图块会被分解，因为 Jw_cad 不镜像文字） |
| 图像（`^@BM…`），链接或内嵌（Ver.7） | `IMAGE`；内嵌图像提取到 `<输出名>_images/` |
| 图层组 × 图层（16 × 16）、名称、隐藏状态 | 图层名 `<组><层>_<组名>_<层名>`，例如 `0A_1F_通り芯` |
| 线型 1–9、随机线、倍长线型 | 采用 Jw_cad 图案的 `LTYPE` |
| 线色 1–9 和 SXF 颜色、线宽 | ACI 颜色（1 青→4，2 黑→7，3 绿→3，4 黄→2，5 品红→6，6 蓝→5，7→4，8 红→1，9 灰→8）、真彩色、线宽 |

文件版本：支持实际流通的所有数据版本 —— 220、230、300、351、420、600、700
（从 Jw_cad for Windows 2.x 到现在仍以 700 保存的 10.x），字符串为 ANSI（Shift_JIS）或 Unicode。
没有格式说明的更早版本（例如 65）尽力读取。不支持 DOS 版 Jw_cad 的 `.jwc`。

## 验证方法

Jw_cad 10.03.7 通过 `JWW2DXFConv.dll` 导出 DXF。我们在 Windows 上直接调用该 DLL（`ExecDxfExport(hwnd, jww, dxf)`）
生成参考 DXF，并把本程序输出的每个图形与之比对（类型、图层组和图层、坐标（图纸尺寸的 1e-6 以内）、文字内容和角度、填充面积）。
比对脚本位于 `tools/oracle/`。

307 张图纸（公开的示例图纸、旧版 Jw_cad 附带的图纸和 ezjww 的测试图纸），版本 220（13）、230（14）、300（1）、
351（23）、420（25）、600（19）、700（212）：

| | Jw_cad | jww2dxf | 一致 |
|---|---:|---:|---:|
| LINE | 152,625 | 152,625 | 100 % |
| ARC / CIRCLE / ELLIPSE | 10,001 | 10,001 | 100 % |
| POINT | 2,313 | 2,313 | 100 % |
| TEXT | 22,116 | 22,116 | 100 % |
| 实心填充（面积） | 1,085 | 1,085 | 100 % |
| 图像 | 16 | 16 | 100 % |

保留图块的输出也用 ezdxf 分解 `INSERT` 后进行了比对（20 张含图块的图纸，全部一致）；
所有输出在各个 DXF 版本下都通过 ezdxf 的检查。Windows 版与 Linux 版输出完全相同。

与 Jw_cad 导出有意不同之处：颜色使用常规 ACI 编号（Jw_cad 把黑色写成 0）；文字高度保持 Jw_cad 的文字高度
（Jw_cad 乘以 0.83，可用 `--text-factor 0.83` 得到相同结果）；文字宽度保留为宽度系数；图层名中也包含图层组名；
图像写成 `IMAGE`，而不是 `^@BM` 文字。

## 构建

```
cmake -S . -B build && cmake --build build && (cd build && ctest)
```

`-DJWW_STATIC=ON` 为静态链接。Windows 版用 MinGW-w64 构建（见 `.github/workflows/build.yml`）。

## 许可证

MIT（见 `LICENSE`）。`tests/data` 中的测试图纸来自 [ezjww](https://github.com/monozukuri-ai/ezjww)（MIT）。
未使用 Jw_cad 的任何代码；Jw_cad 的著作权属于清水治郎和田中善文。
