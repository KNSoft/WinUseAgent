# Private RPC test entry and independent Windows oracles for integration tests only.
if (-not ('CuaTestClient' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class CuaTestClient {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr window, out Rect bounds);
    [DllImport("user32.dll")] static extern int GetSystemMetrics(int index);
    [DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(IntPtr window, int kind, out Rect bounds, int size);
    public static Rect FrameBounds(IntPtr window) {
        IntPtr previous = SetThreadDpiAwarenessContext(new IntPtr(-4));
        try {
            var desktop = new Rect { Left = GetSystemMetrics(76), Top = GetSystemMetrics(77) };
            desktop.Right = desktop.Left + GetSystemMetrics(78); desktop.Bottom = desktop.Top + GetSystemMetrics(79);
            if (window == IntPtr.Zero) return desktop;
            Rect bounds;
            if (DwmGetWindowAttribute(window, 9, out bounds, Marshal.SizeOf<Rect>()) < 0 &&
                !GetWindowRect(window, out bounds)) throw new Exception("Cannot measure fixture frame.");
            if (bounds.Left > bounds.Right) { int swap = bounds.Left; bounds.Left = bounds.Right; bounds.Right = swap; }
            return bounds;
        } finally { if (previous != IntPtr.Zero) SetThreadDpiAwarenessContext(previous); }
    }
    public sealed class Identity { public string UserSid; public bool Administrator, UiAccess; }
    [StructLayout(LayoutKind.Sequential)] struct SidAttributes { public IntPtr Sid; public uint Attributes; }
    [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr value);
    [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr value);
    [DllImport("advapi32.dll", SetLastError=true)] static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
    [DllImport("advapi32.dll", SetLastError=true)] static extern bool GetTokenInformation(IntPtr token, int kind, IntPtr data, int size, out int needed);
    [DllImport("advapi32.dll", CharSet=CharSet.Unicode)] static extern bool ConvertSidToStringSid(IntPtr sid, out IntPtr text);
    static string SidText(IntPtr sid) {
        IntPtr text;
        if (!ConvertSidToStringSid(sid, out text)) throw new Exception("Cannot read token SID.");
        try { return Marshal.PtrToStringUni(text); } finally { LocalFree(text); }
    }
    static IntPtr TokenData(IntPtr token, int kind) {
        int needed;
        GetTokenInformation(token, kind, IntPtr.Zero, 0, out needed);
        if (needed <= 0) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        IntPtr data = Marshal.AllocHGlobal(needed);
        if (!GetTokenInformation(token, kind, data, needed, out needed)) {
            Marshal.FreeHGlobal(data); throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        }
        return data;
    }
    public static Identity ProcessIdentity(int pid) {
        IntPtr process = OpenProcess(0x1000, false, pid), token = IntPtr.Zero, data = IntPtr.Zero;
        if (process == IntPtr.Zero) throw new Exception("Cannot inspect Worker process.");
        try {
            if (!OpenProcessToken(process, 8, out token)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            var result = new Identity();
            data = TokenData(token, 1); result.UserSid = SidText(Marshal.ReadIntPtr(data)); Marshal.FreeHGlobal(data); data = IntPtr.Zero;
            data = TokenData(token, 2);
            int count = Marshal.ReadInt32(data), size = Marshal.SizeOf<SidAttributes>();
            for (int index = 0; index < count; ++index) {
                var group = Marshal.PtrToStructure<SidAttributes>(IntPtr.Add(data, IntPtr.Size + index * size));
                if (SidText(group.Sid) == "S-1-5-32-544" && (group.Attributes & 4) != 0 &&
                    (group.Attributes & 16) == 0) result.Administrator = true;
            }
            Marshal.FreeHGlobal(data); data = IntPtr.Zero;
            data = TokenData(token, 26); result.UiAccess = Marshal.ReadInt32(data) != 0;
            return result;
        } finally {
            if (data != IntPtr.Zero) Marshal.FreeHGlobal(data);
            if (token != IntPtr.Zero) CloseHandle(token);
            CloseHandle(process);
        }
    }
}
'@
}

function Invoke-CuaTestRpc {
    param(
        [Parameter(Mandatory)][string]$Executable,
        [Parameter(Mandatory)][uint32]$SessionId,
        [Parameter(Mandatory)][AllowEmptyString()][string]$Request,
        [uint32]$TimeoutMs = 10000,
        [ValidateSet('Normal','LowIntegrity','FilteredAdmin')][string]$Identity = 'Normal'
    )
    $requestFile = [IO.Path]::GetTempFileName()
    try {
        [IO.File]::WriteAllText($requestFile, $Request, [Text.UTF8Encoding]::new($false))
        $text = & $Executable CUA TestRpc "RequestFile=$requestFile" "Session=$SessionId" "TimeoutMs=$TimeoutMs" "Identity=$Identity"
        if (-not $text) { throw "TestRpc returned no JSON (exit $LASTEXITCODE)." }
        return ($text -join "`n")
    } finally {
        Remove-Item -LiteralPath $requestFile -ErrorAction SilentlyContinue
    }
}
