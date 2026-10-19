param([string]$srcdir,[string]$dstdir)
$sig = @'
using System; using System.Runtime.InteropServices;
public static class J {
  [DllImport("C:\\jwwork\\jw10\\JWW2DXFConv.dll", CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)]
  public static extern int ExecDxfExport(IntPtr hwnd, string src, string dst);
}
'@
Add-Type -TypeDefinition $sig
Set-Location C:\jwwork\jw10
New-Item -ItemType Directory -Force $dstdir | Out-Null
Get-ChildItem $srcdir -Recurse -Include *.jww | % {
  $rel = $_.FullName.Substring($srcdir.Length).TrimStart('\')
  $out = Join-Path $dstdir ($rel -replace '\.jww$','.dxf')
  New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null
  if (Test-Path $out) { Remove-Item $out }
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $r = [J]::ExecDxfExport([IntPtr]::Zero, $_.FullName, $out)
  "{0}`t{1}`t{2}" -f $r, $sw.ElapsedMilliseconds, $rel
}
