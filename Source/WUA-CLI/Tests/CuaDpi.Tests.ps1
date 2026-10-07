[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory)][string]$ArtifactDirectory,
    [switch]$RequireScaledDesktop
)

$ErrorActionPreference = 'Stop'
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
. (Join-Path $PSScriptRoot 'CuaTestClient.ps1')
New-Item -ItemType Directory -Path $ArtifactDirectory -Force | Out-Null
$script:sequence = 0
Add-Type -ReferencedAssemblies System.Windows.Forms,System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Threading;
using System.Runtime.InteropServices;
using System.Windows.Forms;
public static class WuaDpiFixture {
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] struct Gui {
        public uint Size, Flags;
        public IntPtr Active, Focus, Capture, Menu, Move, Caret;
        public Rect Bounds;
    }
    [DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] static extern bool GetGUIThreadInfo(uint id, ref Gui gui);
    [DllImport("user32.dll")] static extern int MapWindowPoints(IntPtr from, IntPtr to, ref Rect rect, uint count);
    [DllImport("user32.dll")] static extern bool LogicalToPhysicalPointForPerMonitorDPI(IntPtr window, ref Point point);
    [DllImport("user32.dll", EntryPoint="GetWindowLongPtrW")] static extern IntPtr GetStyle(IntPtr window, int index);
    [DllImport("user32.dll", EntryPoint="SetWindowLongPtrW")] static extern IntPtr SetStyle(IntPtr window, int index, IntPtr style);
    [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr window);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    static Form form;
    static TextBox edit;
    static Thread thread;
    public static long Handle;
    public static uint Dpi;
    public static void Start(int awareness, bool mirrored) {
        var ready = new ManualResetEvent(false);
        Exception failure = null;
        thread = new Thread(delegate() {
            try {
                if (SetThreadDpiAwarenessContext(new IntPtr(awareness)) == IntPtr.Zero)
                    throw new Exception("Cannot set fixture DPI awareness.");
                using (var window = new Form()) {
                    form = window;
                    form.Text = "WUA DPI fixture";
                    form.StartPosition = FormStartPosition.Manual;
                    form.Location = new System.Drawing.Point(120, 120);
                    form.Size = new Size(500, 240);
                    form.AutoScaleMode = AutoScaleMode.None;
                    edit = new TextBox { AccessibleName = "DPI edit", Location = new System.Drawing.Point(30, 50),
                        Size = new Size(350, 40), Text = "" };
                    form.Controls.Add(edit);
                    form.Shown += delegate {
                        Handle = form.Handle.ToInt64();
                        Dpi = GetDpiForWindow(form.Handle);
                        if (mirrored) SetStyle(edit.Handle, -20, new IntPtr(GetStyle(edit.Handle, -20).ToInt64() | 0x00400000));
                        edit.Focus();
                        ready.Set();
                    };
                    Application.Run(form);
                }
            } catch (Exception error) { failure = error; ready.Set(); }
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.IsBackground = true;
        thread.Start();
        if (!ready.WaitOne(5000)) throw new Exception("DPI fixture startup timed out.");
        ready.Dispose();
        if (failure != null) throw failure;
    }
    public static string Text() { return (string)form.Invoke(new Func<string>(() => edit.Text)); }
    public static Rect Caret() {
        return (Rect)form.Invoke(new Func<Rect>(delegate() {
            var gui = new Gui { Size = (uint)Marshal.SizeOf(typeof(Gui)) };
            if (!GetGUIThreadInfo(GetCurrentThreadId(), ref gui) || gui.Caret != edit.Handle)
                throw new Exception("The fixture edit does not own the caret.");
            // Oracle runs in the target's own DPI context, unlike the external PMv2 Worker.
            MapWindowPoints(gui.Caret, IntPtr.Zero, ref gui.Bounds, 2);
            var first = new Point { X = gui.Bounds.Left, Y = gui.Bounds.Top };
            var last = new Point { X = gui.Bounds.Right, Y = gui.Bounds.Bottom };
            if (!LogicalToPhysicalPointForPerMonitorDPI(gui.Caret, ref first) ||
                !LogicalToPhysicalPointForPerMonitorDPI(gui.Caret, ref last)) throw new Exception("Cannot map caret.");
            return new Rect { Left = Math.Min(first.X,last.X), Top = Math.Min(first.Y,last.Y),
                Right = Math.Max(first.X,last.X), Bottom = Math.Max(first.Y,last.Y) };
        }));
    }
    public static void Stop() {
        if (thread == null) return;
        if (thread.IsAlive) form.BeginInvoke(new Action(form.Close));
        if (!thread.Join(5000)) throw new Exception("DPI fixture did not stop.");
        thread = null;
    }
}
'@

function Invoke-DpiTool([string[]]$Arguments) {
    $text = & $Executable @Arguments
    $exitCode = $LASTEXITCODE
    $result = $text | ConvertFrom-Json
    $script:sequence++
    $result | ConvertTo-Json -Depth 25 | Set-Content (Join-Path $ArtifactDirectory ("tool-{0:D3}.json" -f $script:sequence)) -Encoding UTF8
    if ($exitCode -ne 0 -or -not $result.ok) { throw "DPI tool failed: $text" }
    return $result.result
}

$results = @()
foreach ($awareness in @(-1,-2,-4)) {
    foreach ($mirrored in @($false,$true)) {
        try {
            [WuaDpiFixture]::Start($awareness,$mirrored)
            $handleText = '{0:X8}' -f [WuaDpiFixture]::Handle
            $observation = Invoke-DpiTool @('CUA','Inspect',"Handle=$handleText")
            $element = @($observation.elements | Where-Object name -eq 'DPI edit')[0]
            $observation = Invoke-DpiTool @('CUA','Mouse','Operation=Click',"Handle=$handleText",
                "X=$($element.point_image_px.x)","Y=$($element.point_image_px.y)")
            $path = Join-Path $ArtifactDirectory "dpi-$awareness-$mirrored.png"
            $observation = Invoke-DpiTool @('CUA','Text','Text=DPI mapped input',"Handle=$handleText","OutFile=$path")
            $deadline = [DateTime]::UtcNow.AddSeconds(3)
            do {
                $actualText = [WuaDpiFixture]::Text()
                if ($actualText -eq 'DPI mapped input') { break }
                Start-Sleep -Milliseconds 30
            } while ([DateTime]::UtcNow -lt $deadline)
            if ($actualText -ne 'DPI mapped input') { throw "DPI fixture received '$actualText' instead of the expected text." }
            $observation = Invoke-DpiTool @('CUA','Inspect',"Handle=$handleText","OutFile=$path")
            $actual = [WuaDpiFixture]::Caret()
            $frame = [CuaTestClient]::FrameBounds([IntPtr]::new([WuaDpiFixture]::Handle))
            @{awareness=$awareness;mirrored=$mirrored;dpi=[WuaDpiFixture]::Dpi;oracle=$actual;frame=$frame;response=$observation} |
                ConvertTo-Json -Depth 25 | Set-Content (Join-Path $ArtifactDirectory "caret-$awareness-$mirrored.json") -Encoding UTF8
            $hint = $observation.interaction.caret_hint
            if (-not $hint.image_geometry_consistent -or -not ($hint.PSObject.Properties.Name -contains 'bounds_image_px')) { throw 'Missing mapped caret hint.' }
            foreach ($side in @('Left','Top','Right','Bottom')) {
                $origin = if ($side -in @('Left','Right')) { $frame.Left } else { $frame.Top }
                if ([Math]::Abs($hint.bounds_image_px.$side + $origin - $actual.$side) -gt 1) {
                    throw "Incorrect caret mapping: awareness=$awareness mirrored=$mirrored side=$side"
                }
            }
            if ($RequireScaledDesktop -and $awareness -eq -4 -and [WuaDpiFixture]::Dpi -le 96) { throw 'The desktop is not scaled.' }
            $results += @{awareness=$awareness;mirrored=$mirrored;dpi=[WuaDpiFixture]::Dpi;caret=$actual;ok=$true}
            Write-Output "PASS: DPI awareness $awareness, mirrored=$mirrored, DPI=$([WuaDpiFixture]::Dpi)"
        } finally { [WuaDpiFixture]::Stop() }
    }
}
$results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $ArtifactDirectory 'result.json') -Encoding UTF8
