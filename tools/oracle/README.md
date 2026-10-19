# Checking jww2dxf against Jw_cad

1. `stage.py DIR...` copies every `.jww` under DIR to `stage/<sha256[:16]>.jww` (and `index.json`).
2. On Windows with Jw_cad 10 installed, copy `stage/` to `C:\jwwork\corpus` and run
   `convall.ps1 C:\jwwork\corpus C:\jwwork\ref` with a **32-bit** PowerShell whose executable sits in the
   Jw_cad folder (copy `C:\Windows\SysWOW64\WindowsPowerShell\v1.0\powershell.exe` there: the DLL reads
   `DXF_HDR_TJW.txt` next to the host program). It calls `JWW2DXFConv.dll`'s
   `ExecDxfExport(hwnd, jww, dxf)` — the exporter Jw_cad itself uses for 「DXF形式で保存」.
3. Copy the DXFs back to `ref/`, then `JWW2DXF=build/jww2dxf python3 runall.py` (needs `ezdxf` only for
   `insertcheck.py` and `render.py`). `ATTRS=1` also prints colour/line type/layer pairings.

Never commit the drawings themselves.
