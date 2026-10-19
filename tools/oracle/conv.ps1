param([string]$src,[string]$dst)
$sig = @'
using System; using System.Runtime.InteropServices;
public static class J {
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern bool SetDllDirectory(string p);
  [DllImport("C:\\jwwork\\jw10\\JWW2DXFConv.dll", CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)]
  public static extern int ExecDxfExport(IntPtr hwnd, string src, string dst);
}
'@
Add-Type -TypeDefinition $sig
Set-Location C:\jwwork\jw10
[J]::SetDllDirectory("C:\jwwork\jw10") | Out-Null
$r = [J]::ExecDxfExport([IntPtr]::Zero, $src, $dst)
"ret=$r"
