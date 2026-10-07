# Runs only against the GUI fixture created by this script. Screenshots and requests remain in ArtifactDirectory.
[CmdletBinding()]
param(
    [string]$Executable = (Join-Path $PSScriptRoot '../../OutDir/x64/Release/WUA-CLI.exe'),
    [string]$ArtifactDirectory = (Join-Path ([IO.Path]::GetTempPath()) ('WuaCuaTests-' + [guid]::NewGuid())),
    [ValidateSet('wgc', 'gdi')][string]$Backend = 'wgc',
    [switch]$TestUiaTimeout,
    [switch]$TestServerRestart,
    [switch]$ReuseServer
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$OutputEncoding = [Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = $OutputEncoding
. (Join-Path $PSScriptRoot 'CuaTestClient.ps1')
if ($ReuseServer -and ($TestServerRestart -or $TestUiaTimeout)) {
    throw 'A reused Server must not be restarted or left with a stalled UIA request by this test.'
}
$Executable = (Resolve-Path -LiteralPath $Executable).Path
New-Item -ItemType Directory -Path $ArtifactDirectory -Force | Out-Null
$ArtifactDirectory = (Resolve-Path -LiteralPath $ArtifactDirectory).Path
$sessionId = (Get-Process -Id $PID).SessionId
$reportPath = Join-Path $ArtifactDirectory 'fixture.json'
$script:sequence = 0
$script:passed = 0
$script:serverInstance = $null
$fixture = $null
$server = $null
$occluder = $null
$dragProcess = $null

if (-not ('WuaCuaFixtureNative' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class WuaCuaFixtureNative {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct GuiInfo {
        public uint Size, Flags;
        public IntPtr Active, Focus, Capture, MenuOwner, MoveSize, Caret;
        public Rect CaretRect;
    }
    [DllImport("user32.dll",SetLastError=true)] static extern bool OpenClipboard(IntPtr owner);
    [DllImport("user32.dll")] static extern bool CloseClipboard();
    [DllImport("user32.dll")] static extern bool EmptyClipboard();
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern uint RegisterClipboardFormat(string name);
    [DllImport("user32.dll",SetLastError=true)] static extern IntPtr SetClipboardData(uint format,IntPtr memory);
    [DllImport("user32.dll",SetLastError=true)] static extern IntPtr GetClipboardData(uint format);
    [DllImport("kernel32.dll")] static extern IntPtr GlobalAlloc(uint flags,UIntPtr size);
    [DllImport("kernel32.dll")] static extern IntPtr GlobalFree(IntPtr memory);
    [DllImport("kernel32.dll")] static extern IntPtr GlobalLock(IntPtr memory);
    [DllImport("kernel32.dll")] static extern bool GlobalUnlock(IntPtr memory);
    static readonly byte[] Marker = {0,1,2,0,255,254,64,128};
    public static void ClearClipboard() {
        if (!OpenClipboard(IntPtr.Zero)) throw new Exception("Cannot open fixture clipboard.");
        try { if (!EmptyClipboard()) throw new Exception("Cannot empty fixture clipboard."); }
        finally { CloseClipboard(); }
    }
    public static bool ClipboardMarker(bool write) {
        return ClipboardData(RegisterClipboardFormat("WUA.Test.Clipboard.Binary"),write);
    }
    static bool ClipboardData(uint format,bool write) {
        if(!OpenClipboard(IntPtr.Zero))throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        try {
            IntPtr memory=write ? GlobalAlloc(2,(UIntPtr)Marker.Length) : GetClipboardData(format);
            if(memory==IntPtr.Zero)return false;
            IntPtr data=GlobalLock(memory);
            if(data==IntPtr.Zero)throw new Exception("Clipboard marker could not be locked.");
            var bytes=new byte[Marker.Length];
            try {
                if(write)Marshal.Copy(Marker,0,data,Marker.Length); else Marshal.Copy(data,bytes,0,bytes.Length);
            } finally { GlobalUnlock(memory); }
            if(write) {
                if(SetClipboardData(format,memory)==IntPtr.Zero) {
                    GlobalFree(memory); throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
                }
                return true;
            }
            for(int i=0;i<bytes.Length;i++)if(bytes[i]!=Marker[i])return false;
            return true;
        } finally { CloseClipboard(); }
    }
    [DllImport("user32.dll",EntryPoint="SendMessageW")]
    static extern IntPtr SendMessage(IntPtr window,uint message,IntPtr wParam,IntPtr lParam);
    public static void ConfigureTextInput(IntPtr window,int option) {
        if(SendMessage(window,0x800B,new IntPtr(option),IntPtr.Zero)==IntPtr.Zero)throw new Exception("Fixture configuration failed.");
    }
    public static void ToggleUiaStall(IntPtr window) {
        SendMessage(window,0x0111,new IntPtr(106),IntPtr.Zero);
    }
    [DllImport("kernel32.dll")] public static extern ulong GetTickCount64();
    [DllImport("user32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int key);
    [DllImport("user32.dll")] public static extern uint GetDoubleClickTime();
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr window);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] static extern bool GetGUIThreadInfo(uint thread, ref GuiInfo info);
    [DllImport("user32.dll")] static extern int GetDlgCtrlID(IntPtr window);
    [DllImport("user32.dll")] static extern int MapWindowPoints(IntPtr from, IntPtr to, ref Rect rect, uint count);
    public static bool EditHasFocus(uint thread) {
        var info = new GuiInfo { Size = (uint)Marshal.SizeOf(typeof(GuiInfo)) };
        return GetGUIThreadInfo(thread, ref info) && GetDlgCtrlID(info.Focus) == 101;
    }
    public static GuiInfo GuiState(uint thread) {
        var info = new GuiInfo { Size = (uint)Marshal.SizeOf(typeof(GuiInfo)) };
        if (!GetGUIThreadInfo(thread, ref info)) throw new Exception("Cannot read fixture input state.");
        return info;
    }
    public static string FocusHandle(uint thread) {
        var info = new GuiInfo { Size = (uint)Marshal.SizeOf(typeof(GuiInfo)) };
        if (!GetGUIThreadInfo(thread, ref info)) throw new Exception("Cannot read fixture focus.");
        return info.Focus.ToInt64().ToString("X8");
    }
    public static Rect CaretScreen(uint thread) {
        var info = new GuiInfo { Size = (uint)Marshal.SizeOf(typeof(GuiInfo)) };
        if (!GetGUIThreadInfo(thread, ref info) || info.Caret == IntPtr.Zero) throw new Exception("No fixture caret");
        var previous = SetThreadDpiAwarenessContext(new IntPtr(-4));
        try {
            MapWindowPoints(info.Caret, IntPtr.Zero, ref info.CaretRect, 2);
            return info.CaretRect;
        } finally { SetThreadDpiAwarenessContext(previous); }
    }
}
'@
}

if (-not ('WuaOleClipboardFixture' -as [type])) {
    Add-Type -ReferencedAssemblies System.Windows.Forms -TypeDefinition @'
using System;
using System.IO;
using System.Threading;
using System.Windows.Forms;
public static class WuaOleClipboardFixture {
    static Thread thread;
    static Control owner;
    static readonly byte[] marker = {0,1,2,0,255,254,64,128};
    public static void Seed(string text) {
        Stop();
        var ready = new ManualResetEvent(false);
        Exception failure = null;
        thread = new Thread(delegate() {
            try {
                using (var control = new Control()) {
                    owner = control;
                    var handle = control.Handle;
                    var data = new DataObject();
                    data.SetData(DataFormats.UnicodeText, text);
                    data.SetData("WUA.Test.Ole.Binary", false, new MemoryStream(marker));
                    Clipboard.SetDataObject(data, false);
                    ready.Set();
                    Application.Run();
                    GC.KeepAlive(data);
                }
            } catch (Exception error) { failure = error; ready.Set(); }
        });
        thread.IsBackground = true;
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        if (!ready.WaitOne(5000)) throw new Exception("OLE fixture startup timed out.");
        ready.Dispose();
        if (failure != null) throw failure;
    }
    public static bool Verify(string text) {
        return (bool)owner.Invoke(new Func<bool>(delegate() {
            if (Clipboard.GetText() != text) return false;
            var stream = Clipboard.GetData("WUA.Test.Ole.Binary") as MemoryStream;
            if (stream == null) return false;
            byte[] actual = stream.ToArray();
            if (actual.Length != marker.Length) return false;
            for (int i = 0; i < actual.Length; ++i) if (actual[i] != marker[i]) return false;
            return true;
        }));
    }
    public static void Stop() {
        if (thread == null) return;
        if (thread.IsAlive && owner != null) owner.BeginInvoke(new Action(Application.ExitThread));
        if (!thread.Join(5000)) throw new Exception("OLE fixture did not stop.");
        thread = null;
        owner = null;
    }
}
'@
}

function Fixture-Handle($State) {
    return [IntPtr]::new([Convert]::ToInt64($State.hwnd, 16))
}

function Set-FixtureBounds($State, [int]$X, [int]$Y, [int]$Width, [int]$Height, [IntPtr]$After = [IntPtr]::Zero, [uint32]$Flags = 0x14) {
    $oldDpi = [WuaCuaFixtureNative]::SetThreadDpiAwarenessContext([IntPtr]::new(-4))
    try {
        if (-not [WuaCuaFixtureNative]::SetWindowPos((Fixture-Handle $State), $After, $X, $Y, $Width, $Height, $Flags)) {
            throw "Failed to position owned test fixture: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
        }
    } finally {
        if ($oldDpi -ne [IntPtr]::Zero) { $null = [WuaCuaFixtureNative]::SetThreadDpiAwarenessContext($oldDpi) }
    }
}

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:passed++
    Write-Host "PASS: $Message"
}

function Invoke-Cua([string]$Method, [hashtable]$Parameters, [switch]$AllowError) {
    $script:sequence++
    $parameters = $Parameters.Clone()
    if ($Method -eq 'inspect' -and -not $parameters.ContainsKey('outfile')) {
        $parameters.outfile = Join-Path $ArtifactDirectory ('capture-{0:D3}.png' -f $script:sequence)
    }
    $request = [ordered]@{ method = $Method; params = $parameters }
    $requestPath = Join-Path $ArtifactDirectory ('request-{0:D3}.json' -f $script:sequence)
    [IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json -Depth 20 -Compress), [Text.UTF8Encoding]::new($false))
    $response = Invoke-CuaTestRpc $Executable $sessionId ($request | ConvertTo-Json -Depth 20 -Compress) | ConvertFrom-Json
    $response | ConvertTo-Json -Depth 30 |
        Set-Content (Join-Path $ArtifactDirectory ('response-{0:D3}.json' -f $script:sequence)) -Encoding UTF8
    if (-not $AllowError -and -not $response.ok) {
        throw "$Method failed: $($response | ConvertTo-Json -Depth 10 -Compress)"
    }
    return $response
}

function Read-Fixture {
    for ($attempt = 0; $attempt -lt 30; $attempt++) {
        try {
            $state = Get-Content -LiteralPath $reportPath -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($null -eq $fixture -or $state.pid -eq $fixture.Id) { return $state }
        } catch {}
        Start-Sleep -Milliseconds 50
    }
    throw 'Fixture report was not available.'
}

function Wait-Fixture([scriptblock]$Condition, [string]$Description) {
    $deadline = [WuaCuaFixtureNative]::GetTickCount64() + 5000
    do {
        $state = Read-Fixture
        if (& $Condition $state) { Assert-Condition $true $Description; return $state }
        Start-Sleep -Milliseconds 30
    } while ([WuaCuaFixtureNative]::GetTickCount64() -lt $deadline)
    throw "Fixture condition timed out: $Description"
}

function Observe-Fixture([bool]$Uia = $true) {
    $parameters = @{ handle = $script:windowHandle; backend = $Backend }
    if (-not $Uia) { $parameters.uia = $false }
    return (Invoke-Cua inspect $parameters).result
}

function Find-Element($Observation, [string]$Name) {
    $matches = @($Observation.elements | Where-Object { $_.name -eq $Name })
    if ($matches.Count -ne 1) { throw "Expected one UIA element named '$Name', found $($matches.Count)." }
    return $matches[0]
}

function Assert-ImageCoordinates($Observation, [string]$Scope) {
    $invalid = 0
    $mapped = 0
    foreach ($element in $Observation.elements) {
        $properties = $element.PSObject.Properties.Name
        if ($properties -contains 'bounds_screen_px') { $invalid++ }
        if ($properties -contains 'bounds_image_px') {
            $mapped++
            $bounds = $element.bounds_image_px
            if ($bounds.left -lt 0 -or $bounds.top -lt 0 -or
                $bounds.right -gt $Observation.geometry.width -or $bounds.bottom -gt $Observation.geometry.height -or
                $bounds.right -le $bounds.left -or $bounds.bottom -le $bounds.top) { $invalid++ }
        }
        if ($properties -contains 'point_image_px') {
            $point = $element.point_image_px
            if (-not ($properties -contains 'bounds_image_px') -or -not $element.enabled -or $element.offscreen -or
                $point.x -ne [Math]::Floor($point.x) -or $point.y -ne [Math]::Floor($point.y) -or
                $point.x -lt $element.bounds_image_px.left -or $point.x -ge $element.bounds_image_px.right -or
                $point.y -lt $element.bounds_image_px.top -or $point.y -ge $element.bounds_image_px.bottom) { $invalid++ }
        }
        if ($properties -contains 'runtime_id' -and $element.runtime_id -notmatch '^-?\d+(,-?\d+)*$') { $invalid++ }
    }
    Assert-Condition ($mapped -gt 0 -and $invalid -eq 0) "$Scope UIA coordinates stay inside the screenshot without conversion"
}

try {
    if (-not $ReuseServer) {
        $existing = Invoke-CuaTestRpc $Executable $sessionId '{"method":"capabilities","params":{}}' -TimeoutMs 2000 | ConvertFrom-Json
        if ($existing.ok) { throw 'An existing Server is present. Use -ReuseServer or run in a dedicated test session.' }
        if ($existing.hresult -ne -2147023174) {
            throw 'Server absence is unconfirmed. Refusing to claim or stop a possibly existing Worker.'
        }
    }
    $fixture = Start-Process -FilePath $Executable -ArgumentList @('CUA', 'TestHost', ('Report="{0}"' -f $reportPath)) -WindowStyle Hidden -PassThru
    $null = Read-Fixture
    $initialDesktop = & $Executable CUA Inspect Handle=0 | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0 -or -not $initialDesktop.ok) { throw 'Public desktop Inspect could not start the Worker.' }
    $deadline = [WuaCuaFixtureNative]::GetTickCount64() + 10000
    do {
        try { $caps = Invoke-Cua capabilities @{}; break } catch {
            if ([WuaCuaFixtureNative]::GetTickCount64() -ge $deadline) { throw }
            Start-Sleep -Milliseconds 100
        }
    } while ($true)
    $script:serverInstance = $caps.result.instance
    if (-not $ReuseServer) { $server = Get-Process -Id $caps.result.pid }
    Assert-Condition ((Get-Process -Id $caps.result.pid).SessionId -eq $sessionId) 'Server is in the requested Windows session'
    Assert-Condition ([CuaTestClient]::ProcessIdentity($caps.result.pid).UserSid -eq
        [CuaTestClient]::ProcessIdentity($PID).UserSid) 'Worker and caller use the same account'

    $desktop = (Invoke-Cua inspect @{ handle = '0'; backend = $Backend; uia = $false }).result
    $window = @($desktop.windows | Where-Object { $_.title -eq 'WUA CUA Test Fixture' -and $_.pid -eq $fixture.Id })
    Assert-Condition ($window.Count -eq 1) 'Desktop observation identifies the owned fixture'
    Assert-Condition ($window[0].tid -gt 0 -and $window[0].process_path -eq $Executable -and
        $window[0].window_handle -match '^[0-9A-F]{8}$' -and $null -ne $window[0].maximized -and $null -ne $window[0].topmost) 'Window metadata identifies the process, thread, and placement state'
    $script:windowHandle = $window[0].window_handle
    $observed = Observe-Fixture
    Assert-Condition ($observed.geometry.width -gt 0 -and $observed.geometry.height -gt 0) 'Window capture returns an image'
    Assert-Condition ($observed.uia_state -eq 'ok' -and $observed.elements.Count -gt 0) 'Worker UIA observation returns controls'
    Assert-Condition ($observed.uia_state -eq 'ok' -and $observed.uia_target -eq $script:windowHandle -and
        $observed.interaction.state -in @('current','background','changed','unavailable') -and
        -not ($observed.interaction.PSObject.Properties.Name -contains 'scope') -and
        -not ($observed.interaction.PSObject.Properties.Name -contains 'queried_window')) 'Window observations include UIA and interpreted interaction without redundant scope'
    Assert-Condition (($observed | ConvertTo-Json -Depth 20 -Compress) -notmatch 'fixture-password-secret') 'Password value is absent from observation'
    Assert-ImageCoordinates $observed 'Full-window'

    $button = Find-Element $observed 'Increment counter'
    $clickParams = @{ handle = $script:windowHandle; x = $button.point_image_px.x; y = $button.point_image_px.y }
    $null = Invoke-Cua click $clickParams
    $null = Wait-Fixture { param($state) $state.clicks -eq 1 } 'Full-window UIA point clicks without coordinate arithmetic'

    $observed = Observe-Fixture
    $button = Find-Element $observed 'Increment counter'
    $null = Invoke-Cua invoke @{ handle = $script:windowHandle; runtime_id = $button.runtime_id }
    $null = Wait-Fixture { param($state) $state.clicks -eq 2 } 'UIA Invoke resolves the original runtime ID'
    $boundaryId = Invoke-Cua invoke @{handle=$script:windowHandle; runtime_id='-2147483648,2147483647'} -AllowError
    Assert-Condition (-not $boundaryId.ok -and $boundaryId.hresult -ne -2147024809 -and
        $boundaryId.details -match 'UIA action') 'Signed RuntimeId boundaries reach UIA lookup instead of parameter rejection'
    foreach ($invalidId in @('2147483648','-2147483649','','3,')) {
        $invalidRuntime = Invoke-Cua invoke @{handle=$script:windowHandle; runtime_id=$invalidId} -AllowError
        Assert-Condition (-not $invalidRuntime.ok -and $invalidRuntime.hresult -eq -2147024809 -and
            $invalidRuntime.details -match 'RuntimeId') 'Overflow, empty and trailing-comma RuntimeIds fail parameter validation'
    }
    Assert-Condition ((Read-Fixture).clicks -eq 2) 'Invalid and nonexistent RuntimeIds never invoke the fixture button'
    $observed = Observe-Fixture
    $checkbox = Find-Element $observed 'Test checkbox'
    $null = Invoke-Cua toggle @{ handle = $script:windowHandle; runtime_id = $checkbox.runtime_id }
    $null = Wait-Fixture { param($state) $state.checked } 'UIA Toggle changes the fixture checkbox'

    $observed = Observe-Fixture
    $edit = @($observed.elements | Where-Object { $_.automation_id -eq '101' })
    Assert-Condition ($edit.Count -eq 1) 'Edit control has a stable automation ID'
    $null = Invoke-Cua set_value @{ handle = $script:windowHandle; runtime_id = $edit[0].runtime_id; text = 'Semantic value' }
    $null = Wait-Fixture { param($state) $state.text -eq 'Semantic value' } 'UIA SetValue updates the edit control'
    $null = Invoke-Cua click @{ handle = $script:windowHandle; x = $edit[0].point_image_px.x; y = $edit[0].point_image_px.y }
    $observed = Observe-Fixture $false
    $null = Invoke-Cua keys @{ handle = $script:windowHandle; keys = @('CTRL', 'A') }
    [WuaOleClipboardFixture]::Seed('WUA original clipboard 中文')
    $typed = 'CUA 中文 ' + [char]::ConvertFromUtf32(0x1F642)
    $paste = Invoke-Cua type @{ handle = $script:windowHandle; method = 'paste'; text = $typed }
    $null = Wait-Fixture { param($state) $state.text -eq $typed } 'Pasting preserves Chinese text and a surrogate pair'
    Assert-Condition (-not ($paste.result.PSObject.Properties.Name -contains 'clipboard_error')) 'Live OLE restoration reports no failure'
    Assert-Condition ([WuaOleClipboardFixture]::Verify('WUA original clipboard 中文')) 'Retaining a live OLE source restores its Unicode text and binary format'
    [WuaOleClipboardFixture]::Stop()
    Assert-Condition ((Get-Clipboard -Raw) -eq 'WUA original clipboard 中文') 'Flushed text remains readable after the original OLE source exits'

    $textObservation = Observe-Fixture
    $focused = @($textObservation.elements | Where-Object focused)
    Assert-Condition ($textObservation.interaction.state -eq 'current' -and
        $textObservation.interaction.focus.window_handle -eq [WuaCuaFixtureNative]::FocusHandle($window[0].tid) -and
        $textObservation.interaction.focus.within_observed_window -and
        $textObservation.foreground -eq $script:windowHandle -and
        [WuaCuaFixtureNative]::GetForegroundWindow() -eq (Fixture-Handle (Read-Fixture)) -and
        -not ($textObservation.interaction.focus.PSObject.Properties.Name -contains 'in_foreground') -and
        $focused.Count -eq 1 -and $focused[0].automation_id -eq '101' -and
        [WuaCuaFixtureNative]::EditHasFocus($window[0].tid)) 'Processed focus agrees with the real edit control and its UIA element'
    $caret = [WuaCuaFixtureNative]::CaretScreen($window[0].tid)
    $frame = [CuaTestClient]::FrameBounds((Fixture-Handle (Read-Fixture)))
    $hint = $textObservation.interaction.caret_hint
    Assert-Condition ($hint.image_geometry_consistent -and ($hint.PSObject.Properties.Name -contains 'bounds_image_px') -and
        $hint.window_handle -eq $script:windowHandle -and
        $hint.bounds_image_px.left + $frame.Left -eq $caret.Left -and
        $hint.bounds_image_px.top + $frame.Top -eq $caret.Top -and
        $hint.bounds_image_px.right + $frame.Left -eq $caret.Right -and
        $hint.bounds_image_px.bottom + $frame.Top -eq $caret.Bottom) 'Caret hint is mapped from the real control into full-window image pixels'
    Assert-Condition ($textObservation.interaction.menu -eq 'none' -and -not $textObservation.interaction.move_size_active -and
        $textObservation.interaction.mouse_capture_window -eq '00000000') 'Interaction state reports no menu, move/size or capture'
    $desktopText = (Invoke-Cua inspect @{handle='0'; backend=$Backend}).result
    Assert-Condition ($desktopText.uia_state -eq 'ok' -and $desktopText.uia_target -eq $script:windowHandle -and
        $desktopText.foreground -eq $script:windowHandle -and $desktopText.interaction.state -eq 'current' -and
        -not ($desktopText.interaction.PSObject.Properties.Name -contains 'scope') -and $desktopText.elements.Count -gt 0 -and
        @($desktopText.windows | Where-Object window_handle -eq $script:windowHandle).Count -eq 1) 'Desktop screenshot includes top-level windows, foreground UIA, and current input context'
    $noUia = Observe-Fixture $false
    Assert-Condition ($noUia.uia_state -eq 'disabled' -and $noUia.elements.Count -eq 0 -and
        $noUia.interaction.state -eq 'current' -and $noUia.interaction.focus.window_handle -eq [WuaCuaFixtureNative]::FocusHandle($window[0].tid)) 'Disabling UIA still returns normalized native interaction state'

    $state = Read-Fixture
    $originalBounds = $state.window
    $width = $originalBounds.right - $originalBounds.left
    $height = $originalBounds.bottom - $originalBounds.top
    $observed = Observe-Fixture
    $button = Find-Element $observed 'Increment counter'
    $point = @{ handle = $script:windowHandle; x = $button.point_image_px.x; y = $button.point_image_px.y }
    $beforeClicks = $state.clicks
    Set-FixtureBounds $state ($originalBounds.left + 90) ($originalBounds.top + 60) $width $height
    $null = Invoke-Cua click $point
    $null = Wait-Fixture { param($value) $value.clicks -eq ($beforeClicks + 1) } 'Window-relative coordinates remain correct after translation'
    $beforeClicks++
    Set-FixtureBounds $state $originalBounds.left $originalBounds.top $width $height

    $secondReport = Join-Path $ArtifactDirectory 'occluder.json'
    $occluder = Start-Process -FilePath $Executable -ArgumentList @('CUA', 'TestHost', ('Report="{0}"' -f $secondReport)) -WindowStyle Hidden -PassThru
    $deadline = [WuaCuaFixtureNative]::GetTickCount64() + 5000
    do {
        try {
            $secondState = Get-Content -LiteralPath $secondReport -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($secondState.pid -ne $occluder.Id) { throw 'Waiting for the new occluder process.' }
            break
        } catch {
            if ([WuaCuaFixtureNative]::GetTickCount64() -ge $deadline) { throw }
            Start-Sleep -Milliseconds 50
        }
    } while ($true)
    $all = (Invoke-Cua inspect @{ handle = '0'; backend = $Backend; uia = $false }).result
    $secondWindow = @($all.windows | Where-Object { $_.pid -eq $occluder.Id -and $_.title -eq 'WUA CUA Test Fixture' })
    Assert-Condition ($secondWindow.Count -eq 1) 'Second owned fixture is independently identified'
    $null = Invoke-Cua activate @{ handle = $secondWindow[0].window_handle }
    $observed = Observe-Fixture
    Assert-Condition ($observed.interaction.state -eq 'background' -and $null -eq $observed.interaction.focus -and
        $observed.foreground -eq $secondWindow[0].window_handle -and $observed.uia_target -eq $script:windowHandle -and
        $observed.elements.Count -gt 0 -and -not ($observed.interaction.PSObject.Properties.Name -contains 'caret_hint')) 'Background window keeps its UIA but never exposes retained thread focus or caret as current input state'
    $button = Find-Element $observed 'Increment counter'
    $null = Invoke-Cua click @{ handle = $script:windowHandle; x = $button.point_image_px.x; y = $button.point_image_px.y }
    $beforeClicks++
    $null = Wait-Fixture { param($value) $value.clicks -eq $beforeClicks } 'Window input automatically activates the target over another fixture'
    Assert-Condition ([WuaCuaFixtureNative]::GetForegroundWindow() -eq (Fixture-Handle $state)) 'Window input leaves the intended fixture foreground'
    Set-FixtureBounds $secondState $originalBounds.left $originalBounds.top $width $height ([IntPtr]::Zero) 0x10
    $observed = Observe-Fixture
    $button = Find-Element $observed 'Increment counter'
    $null = Invoke-Cua click @{ handle = $script:windowHandle; x = $button.point_image_px.x; y = $button.point_image_px.y }
    $beforeClicks++
    $null = Wait-Fixture { param($value) $value.clicks -eq $beforeClicks } 'Window input raises an already-foreground target above a nonactivating occluder'
    $desktopObservation = (Invoke-Cua inspect @{ handle = '0'; backend = $Backend; uia = $true }).result
    Assert-ImageCoordinates $desktopObservation 'Desktop'
    $desktopButton = Find-Element $desktopObservation 'Increment counter'
    Set-FixtureBounds $secondState $originalBounds.left $originalBounds.top $width $height ([IntPtr]::new(-1)) 0x10
    $observed = Observe-Fixture
    $button = Find-Element $observed 'Increment counter'
    $blocked = Invoke-Cua click @{ handle = $script:windowHandle; x = $button.point_image_px.x; y = $button.point_image_px.y } -AllowError
    Assert-Condition (-not $blocked.ok -and $blocked.hresult -lt 0) 'A topmost occluder prevents clicking through a WGC image'
    Assert-Condition ((Read-Fixture).clicks -eq $beforeClicks) 'Obstructed action sends no click to the target'
    Assert-Condition ((Get-Content -LiteralPath $secondReport -Raw -Encoding UTF8 | ConvertFrom-Json).clicks -eq 0) 'Obstructed action sends no click to the occluder'
    $null = Invoke-Cua click @{ handle = '0'; x = $desktopButton.point_image_px.x; y = $desktopButton.point_image_px.y }
    Start-Sleep -Milliseconds 100
    Assert-Condition ((Get-Content -LiteralPath $secondReport -Raw -Encoding UTF8 | ConvertFrom-Json).clicks -eq 1) 'Desktop pixel input goes to the current screen destination without target activation'
    Stop-Process -Id $occluder.Id -Force
    $null = $occluder.WaitForExit(5000)
    $occluder = $null
    $closed = Invoke-Cua inspect @{ handle = $secondWindow[0].window_handle; backend = $Backend; uia = $false } -AllowError
    Assert-Condition (-not $closed.ok -and $closed.hresult -lt 0) 'Inspection rejects the HWND of an exited fixture'

    $leftMonitor = [CuaTestClient]::FrameBounds([IntPtr]::Zero)
    Set-FixtureBounds $state ($leftMonitor.left - 180) ($leftMonitor.top + 80) $width $height
    $gdiPath = Join-Path $ArtifactDirectory 'gdi-offscreen.png'
    $gdi = (Invoke-Cua inspect @{ handle = $script:windowHandle; backend = 'gdi'; uia = $true; outfile = $gdiPath }).result
    $gdiFrame = [CuaTestClient]::FrameBounds((Fixture-Handle $state))
    Assert-Condition ($gdi.geometry.width -eq ($gdiFrame.Right - $gdiFrame.Left) -and
        $gdi.geometry.height -eq ($gdiFrame.Bottom - $gdiFrame.Top)) 'GDI preserves the full-window size when part is offscreen'
    Assert-ImageCoordinates $gdi 'Full-window GDI'
    $button = Find-Element $gdi 'Increment counter'
    Add-Type -AssemblyName System.Drawing
    $bitmap = [Drawing.Bitmap]::new($gdiPath)
    try {
        $pixel = $bitmap.GetPixel(0, 0)
        Assert-Condition ($bitmap.Width -eq $gdi.geometry.width -and $bitmap.Height -eq $gdi.geometry.height -and
            $pixel.A -eq 255 -and $pixel.R -eq 0 -and $pixel.G -eq 0 -and $pixel.B -eq 0) 'GDI pads the offscreen area with opaque black'
    } finally { $bitmap.Dispose() }
    $null = Invoke-Cua click @{ handle = $script:windowHandle; x = $button.point_image_px.x; y = $button.point_image_px.y }
    $beforeClicks++
    $null = Wait-Fixture { param($value) $value.clicks -eq $beforeClicks } 'A full-window UIA point reaches the visible part of a partially offscreen window'
    Set-FixtureBounds $state $originalBounds.left $originalBounds.top $width $height

    $observed = Observe-Fixture
    $scroll = Find-Element $observed 'Scroll area'
    $x = [int](($scroll.bounds_image_px.left + $scroll.bounds_image_px.right) / 2)
    $y = [int](($scroll.bounds_image_px.top + $scroll.bounds_image_px.bottom) / 2)
    $null = Invoke-Cua click @{ handle = $script:windowHandle; x = $x; y = $y }
    $null = Invoke-Cua move @{ handle = $script:windowHandle; x = ($x + 5); y = ($y + 5) }
    $null = Invoke-Cua scroll @{ handle = $script:windowHandle; x = $x; y = $y; delta = -120; axis = 'vertical' }
    $null = Wait-Fixture { param($state) $state.wheel_vertical -eq -120 } 'Vertical scrolling preserves signed delta'
    $null = Invoke-Cua scroll @{ handle = $script:windowHandle; x = $x; y = $y; delta = 120; axis = 'horizontal' }
    $null = Wait-Fixture { param($state) $state.wheel_horizontal -eq 120 } 'Horizontal scrolling uses the horizontal wheel event'
    $invalid = Invoke-Cua scroll @{ handle = $script:windowHandle; x = $x; y = $y; delta = 0 } -AllowError
    Assert-Condition (-not $invalid.ok) 'Zero scroll delta is rejected'
    & {
        $beforeInvalid = Read-Fixture
        $badType = Invoke-Cua click @{ handle = $script:windowHandle; x = 'invalid'; y = $y } -AllowError
        Assert-Condition (-not $badType.ok -and $badType.hresult -lt 0) 'Invalid coordinate type identifies a parameter type error'
        $badType = Invoke-Cua click @{ handle = $script:windowHandle; x = $x; y = $y; button = 7 } -AllowError
        Assert-Condition (-not $badType.ok -and $badType.hresult -lt 0) 'Invalid button type is rejected before input'
        $badType = Invoke-Cua keys @{ handle = $script:windowHandle; keys = @('CTRL', 7) } -AllowError
        Assert-Condition (-not $badType.ok -and $badType.hresult -lt 0) 'A non-string key name produces a typed parameter error'
        $afterInvalid = Read-Fixture
        Assert-Condition ($afterInvalid.clicks -eq $beforeInvalid.clicks -and $afterInvalid.scroll_clicks -eq $beforeInvalid.scroll_clicks -and $afterInvalid.text -eq $beforeInvalid.text) 'Invalid typed requests leave the fixture unchanged'
    }
    $small = (Invoke-Cua inspect @{ handle = $script:windowHandle; backend = $Backend; uia = $true; max_nodes = 2 }).result
    Assert-Condition ($small.elements.Count -le 2 -and $small.uia_truncated -and $small.uia_state -eq 'truncated') 'UIA node budget is global and reports truncation'

    $observed = Observe-Fixture
    $scroll = Find-Element $observed 'Scroll area'
    $x = [int](($scroll.bounds_image_px.left + $scroll.bounds_image_px.right) / 2)
    $y = [int](($scroll.bounds_image_px.top + $scroll.bounds_image_px.bottom) / 2)
    $dragOutput = Join-Path $ArtifactDirectory 'drag-response.json'
    $dragProcess = Start-Process -FilePath $Executable -ArgumentList @('CUA','Mouse','Operation=Drag',
        "Handle=$script:windowHandle","X=$x","Y=$y","ToX=$($x + 30)","ToY=$($y + 20)",'DurationMs=3000',
        "Session=$sessionId") -RedirectStandardOutput $dragOutput -WindowStyle Hidden -PassThru
    $null = Wait-Fixture { param($state) $state.mouse_down } 'Drag begins inside the owned scroll area'
    $pending = Invoke-Cua capabilities @{}
    Assert-Condition $pending.ok 'Capabilities remain responsive during a drag'
    $queued = Invoke-Cua move @{handle = $script:windowHandle;x=$x;y=$y} -AllowError
    Assert-Condition (-not $queued.ok -and $queued.hresult -lt 0) 'Queued actions stop waiting at the bounded admission deadline'
    if (-not $dragProcess.WaitForExit(10000)) { throw 'Bounded drag did not finish.' }
    $dragResult = Get-Content -LiteralPath $dragOutput -Raw -Encoding UTF8 | ConvertFrom-Json
    Assert-Condition ($dragResult.ok) 'Bounded drag completes once'
    $null = Wait-Fixture { param($state) -not $state.mouse_down } 'Completed drag releases the owned mouse button'
    Assert-Condition ([WuaCuaFixtureNative]::GetAsyncKeyState(1) -ge 0) 'No synthetic left button remains held'

    $beforeCancel = (Read-Fixture).scroll_clicks
    $cancelDragOutput = Join-Path $ArtifactDirectory 'cancel-gate-drag.json'
    $dragProcess = Start-Process -FilePath $Executable -ArgumentList @('CUA','Mouse','Operation=Drag',
        "Handle=$script:windowHandle","X=$x","Y=$y","ToX=$($x + 10)","ToY=$($y + 10)",'DurationMs=1000',
        "Session=$sessionId") -RedirectStandardOutput $cancelDragOutput -WindowStyle Hidden -PassThru
    $null = Wait-Fixture { param($value) $value.mouse_down } 'The cancellation fixture holds the input gate with a drag'
    $cancelRequest = @{method='click'; params=@{handle=$script:windowHandle; x=$x; y=$y}} |
        ConvertTo-Json -Depth 5 -Compress
    [IO.File]::WriteAllText((Join-Path $ArtifactDirectory 'cancel-gate-request.json'), $cancelRequest,
        [Text.UTF8Encoding]::new($false))
    $cancelled = Invoke-CuaTestRpc $Executable $sessionId $cancelRequest -TimeoutMs 100 | ConvertFrom-Json
    $cancelled | ConvertTo-Json -Depth 10 |
        Set-Content (Join-Path $ArtifactDirectory 'cancel-gate-response.json') -Encoding UTF8
    Assert-Condition (-not $cancelled.ok -and $cancelled.hresult -eq -2147023436) 'A queued input call reaches its RPC deadline and cancels'
    if (-not $dragProcess.WaitForExit(10000)) { throw 'The cancellation fixture drag did not finish.' }
    $cancelDrag = Get-Content -LiteralPath $cancelDragOutput -Raw -Encoding UTF8 | ConvertFrom-Json
    Assert-Condition ($cancelDrag.ok) 'The gate owner completes independently of the cancelled caller'
    $null = Wait-Fixture { param($value) -not $value.mouse_down -and
        $value.scroll_clicks -eq ($beforeCancel + 1) } 'A cancelled queued click sends no input after the gate becomes available'
    $null = Invoke-Cua capabilities @{}
    Start-Sleep -Milliseconds 250
    Assert-Condition ((Read-Fixture).scroll_clicks -eq ($beforeCancel + 1) -and
        [WuaCuaFixtureNative]::GetAsyncKeyState(1) -ge 0) 'Cancelled input leaves no delayed click or held mouse button'

    $observed = Observe-Fixture
    $scroll = Find-Element $observed 'Scroll area'
    $x = $scroll.point_image_px.x; $y = $scroll.point_image_px.y
    $beforeInterrupted = (Read-Fixture).scroll_clicks
    $dragProcess = Start-Process -FilePath $Executable -ArgumentList @('CUA','Mouse','Operation=Drag',
        "Handle=$script:windowHandle","X=$x","Y=$y","ToX=$($x + 20)","ToY=$($y + 10)",'DurationMs=1500',
        "Session=$sessionId") -WindowStyle Hidden -PassThru
    $null = Wait-Fixture { param($state) $state.mouse_down } 'The interrupt test has submitted its drag'
    Stop-Process -Id $dragProcess.Id -Force
    $null = $dragProcess.WaitForExit(5000)
    $null = Wait-Fixture { param($state) -not $state.mouse_down -and
        $state.scroll_clicks -eq ($beforeInterrupted + 1) } 'A disconnected client leaves one completed drag and no replay'
    $afterInterrupted = & $Executable CUA Inspect "Handle=$script:windowHandle" | ConvertFrom-Json
    Assert-Condition ($LASTEXITCODE -eq 0 -and $afterInterrupted.ok -and $afterInterrupted.result.elements.Count -gt 0) 'Fresh public Inspect works after client interruption'
    Start-Sleep -Milliseconds 200
    Assert-Condition ((Read-Fixture).scroll_clicks -eq ($beforeInterrupted + 1) -and
        [WuaCuaFixtureNative]::GetAsyncKeyState(1) -ge 0) 'No delayed repeat or synthetic held button follows interruption'

    $observed = Observe-Fixture
    $modalButton = Find-Element $observed 'Open modal dialog'
    $null = Invoke-Cua click @{ handle = $script:windowHandle; x = $modalButton.point_image_px.x; y = $modalButton.point_image_px.y }
    Start-Sleep -Milliseconds 100
    $modalDesktop = (Invoke-Cua inspect @{ handle = '0'; backend = $Backend; uia = $false }).result
    $modal = @($modalDesktop.windows | Where-Object { $_.pid -eq $fixture.Id -and $_.title -eq 'WUA CUA Test Modal' })
    Assert-Condition ($modal.Count -eq 1) 'Desktop observation identifies the fixture-owned modal dialog'
    $blocked = Invoke-Cua click @{ handle = $script:windowHandle; x = 100; y = 100 } -AllowError
    Assert-Condition (-not $blocked.ok -and $blocked.hresult -lt 0) 'A modal dialog blocks physical input to its disabled owner'
    Assert-Condition ((Read-Fixture).clicks -eq $beforeClicks) 'Modal rejection does not click the owner'
    $null = Invoke-Cua close_window @{ handle = $modal[0].window_handle }
    Start-Sleep -Milliseconds 100
    $observed = Observe-Fixture
    Assert-Condition $observed.windows[0].enabled 'Explicitly closing the owned modal restores its owner'

    # Exercise the commands a model or an existing script actually calls.
    function Invoke-Tool([string[]]$Arguments, [switch]$AllowError) {
        $script:sequence++
        $text = & $Executable @Arguments
        $code = $LASTEXITCODE
        $reply = ($text -join "`n") | ConvertFrom-Json
        [IO.File]::WriteAllText((Join-Path $ArtifactDirectory ('cli-{0:D3}.json' -f $script:sequence)),
            ($reply | ConvertTo-Json -Depth 30), [Text.UTF8Encoding]::new($false))
        if (-not $AllowError -and ($code -ne 0 -or -not $reply.ok)) { throw "CLI failed: $text" }
        if ($AllowError -and ($reply.ok -or $code -eq 0)) { throw "Expected a nonzero failure: $text" }
        return $reply
    }

    $sessions = Invoke-Tool @('CUA','Session','List')
    Assert-Condition (@($sessions.result | Where-Object { $_.id -eq $sessionId -and $_.state -eq 'ready' }).Count -eq 1) 'Session List returns its array result and identifies the current ready session'
    foreach ($management in @('Preview','Destroy')) {
        $arguments = @('CUA','Session',$management,"Session=$sessionId")
        if ($management -eq 'Preview') { $arguments += 'Show=false' }
        $invalidSession = Invoke-Tool $arguments -AllowError
        Assert-Condition ($invalidSession.hresult -lt 0) "$management rejects a non-child session"
    }
    $sameServer = Invoke-Cua capabilities @{}
    Assert-Condition ($sameServer.result.pid -eq $caps.result.pid -and
        $sameServer.result.instance -eq $script:serverInstance) 'Invalid child management targets leave the existing Worker unchanged'
    $pngCount = @(Get-ChildItem -LiteralPath $ArtifactDirectory -Filter '*.png').Count
    $publicDesktop = (Invoke-Tool @('CUA','Inspect','Handle=0')).result
    $defaultDesktop = (Invoke-Tool @('CUA','Inspect')).result
    $numericDesktop = (Invoke-Tool @('CUA','Inspect','Handle=0',"Session=$sessionId")).result
    foreach ($observation in @($publicDesktop,$defaultDesktop,$numericDesktop)) {
        Assert-Condition (@($observation.windows | Where-Object window_handle -eq $script:windowHandle).Count -eq 1) 'Default, zero Handle and numeric Session identify the same desktop fixture'
    }
    $state = Read-Fixture
    $public = (Invoke-Tool @('CUA','Inspect',"Handle=$($state.hwnd)","Session=$sessionId")).result
    Assert-Condition ($public.uia_target -eq $script:windowHandle -and $public.elements.Count -gt 0 -and $public.interaction) 'CUA Inspect returns window UIA and processed native input context'
    Assert-Condition (@(Get-ChildItem -LiteralPath $ArtifactDirectory -Filter '*.png').Count -eq $pngCount) 'Inspect without OutFile returns usable geometry and UIA without creating an image'
    Assert-ImageCoordinates $public 'Public window'
    Assert-Condition (($public | ConvertTo-Json -Depth 30 -Compress) -notmatch '"(backend|screen_bounds_px|instance|uia_status|pattern_bits)"') 'Public results omit transport, backend, raw status, and physical coordinate fields'

    $button = Find-Element $public 'Increment counter'
    $count = (Read-Fixture).clicks
    $public = (Invoke-Tool @('CUA','Mouse','Operation=Click',"Handle=$script:windowHandle","X=$($button.point_image_px.x)","Y=$($button.point_image_px.y)")).result
    $null = Wait-Fixture { param($value) $value.clicks -eq ($count + 1) } 'CUA Mouse accepts the copied point from CUA Inspect'
    Assert-Condition ($public.elements.Count -gt 0 -and $public.interaction) 'Input returns fresh context without another model command'
    $button = Find-Element $public 'Increment counter'
    $public = (Invoke-Tool @('CUA','Mouse','Operation=Click',"Handle=$script:windowHandle",
        "X=$($button.point_image_px.x)","Y=$($button.point_image_px.y)")).result
    $count += 2
    $null = Wait-Fixture { param($value) $value.clicks -eq $count } 'UIA point_image_px is directly usable with the same window Handle'

    $output = Join-Path $ArtifactDirectory 'public window 中文.png'
    $saved = (Invoke-Tool @('CUA','Inspect',"Handle=$script:windowHandle","OutFile=$output")).result
    $bytes = [IO.File]::ReadAllBytes($output)
    Assert-Condition ($bytes.Length -gt 100 -and
        ([BitConverter]::ToString($bytes,0,8) -replace '-','') -eq '89504E470D0A1A0A') 'Explicit OutFile writes a PNG at a Unicode path'
    $button = Find-Element $saved 'Increment counter'
    $failedOutput = Join-Path $ArtifactDirectory 'missing-directory/after.png'
    $failed = Invoke-Tool @('CUA','Mouse','Operation=Click',"Handle=$script:windowHandle","X=$($button.point_image_px.x)","Y=$($button.point_image_px.y)","OutFile=$failedOutput")
    $count++
    $null = Wait-Fixture { param($value) $value.clicks -eq $count } 'A screenshot write failure after input never repeats the click'
    Assert-Condition ($failed.ok -and $failed.result.image_error.hresult -lt 0 -and
        $failed.result.elements.Count -gt 0) 'Post-action OutFile failure preserves successful input and fresh context'
    $inspectionFailure = Invoke-Tool @('CUA','Inspect',"Handle=$script:windowHandle","OutFile=$failedOutput") -AllowError
    Assert-Condition ($inspectionFailure.hresult -lt 0) 'An explicit Inspect screenshot failure is a main-operation failure'
    $null = Invoke-Tool @('CUA','Inspect',"Handle=$($state.hwnd)",'OutFile=') -AllowError
    $null = Invoke-Tool @('CUA','Inspect','Handle=0','Backend=gdi') -AllowError
    Assert-Condition ((Read-Fixture).clicks -eq $count) 'Invalid OutFile and private options fail without input'

    $state = Read-Fixture
    $desktopFrame = [CuaTestClient]::FrameBounds([IntPtr]::Zero)
    $screenX = [int](($state.controls.button.left + $state.controls.button.right) / 2) - $desktopFrame.Left
    $screenY = [int](($state.controls.button.top + $state.controls.button.bottom) / 2) - $desktopFrame.Top
    $null = Invoke-Tool @('CUA','Mouse','Operation=Click',"X=$screenX","Y=$screenY")
    $count++
    $null = Wait-Fixture { param($value) $value.clicks -eq $count } 'Desktop Mouse uses virtual-desktop screenshot coordinates'
    $fullWindow = (Invoke-Tool @('CUA','Inspect',"Handle=$($state.hwnd)")).result
    $button = Find-Element $fullWindow 'Increment counter'
    $null = Invoke-Tool @('CUA','Mouse','Operation=Click',"Handle=$($state.hwnd)",
        "X=$($button.point_image_px.x)","Y=$($button.point_image_px.y)")
    $count++
    $null = Wait-Fixture { param($value) $value.clicks -eq $count } 'Window Mouse uses full-window image coordinates'

    $public = (Invoke-Tool @('CUA','Inspect',"Handle=$script:windowHandle")).result
    $scroll = Find-Element $public 'Scroll area'
    $pointX = $scroll.point_image_px.x; $pointY = $scroll.point_image_px.y
    $down = (Invoke-Tool @('CUA','Mouse','Operation=Down',"Handle=$script:windowHandle","X=$pointX","Y=$pointY")).result
    $null = Wait-Fixture { param($value) $value.mouse_down } 'The retained Down operation reaches the control'
    $move = (Invoke-Tool @('CUA','Mouse','Operation=Move',"Handle=$script:windowHandle","X=$($pointX + 3)","Y=$pointY")).result
    $up = (Invoke-Tool @('CUA','Mouse','Operation=Up',"Handle=$script:windowHandle","X=$pointX","Y=$pointY")).result
    $null = Wait-Fixture { param($value) -not $value.mouse_down } 'Move and Up continue and finish a direct mouse sequence'
    Assert-Condition ([WuaCuaFixtureNative]::GetAsyncKeyState(1) -ge 0) 'Down/Move/Up leaves no held mouse button'
    foreach ($buttonName in @('Right','Middle')) {
        $null = Invoke-Tool @('CUA','Mouse','Operation=Click',"Button=$buttonName","Handle=$script:windowHandle",
            "X=$pointX","Y=$pointY")
    }
    $null = Wait-Fixture { param($value) $value.right_clicks -eq 1 -and $value.middle_clicks -eq 1 } 'Right and Middle deliver their distinct button messages'
    Start-Sleep -Milliseconds ([WuaCuaFixtureNative]::GetDoubleClickTime() + 100)
    $beforeDouble = Read-Fixture
    $null = Invoke-Tool @('CUA','Mouse','Operation=DoubleClick',"Handle=$script:windowHandle",
        "X=$pointX","Y=$pointY")
    $null = Wait-Fixture { param($value) $value.double_clicks -eq ($beforeDouble.double_clicks + 1) -and
        $value.scroll_clicks -eq ($beforeDouble.scroll_clicks + 2) -and -not $value.mouse_down } 'DoubleClick delivers a double-click message, two releases and no held button'
    foreach ($buttonName in @('X1','X2')) {
        $up = (Invoke-Tool @('CUA','Mouse','Operation=Click',"Button=$buttonName","Handle=$script:windowHandle","X=$pointX","Y=$pointY")).result
    }
    $null = Wait-Fixture { param($value) $value.x1_clicks -eq 1 -and $value.x2_clicks -eq 1 } 'The original X1 and X2 buttons deliver distinct events'
    $wheelBefore = (Read-Fixture).wheel_vertical
    $null = Invoke-Tool @('CUA','Mouse','Operation=Wheel',"Handle=$script:windowHandle","X=$pointX","Y=$pointY")
    $null = Wait-Fixture { param($value) $value.wheel_vertical -eq ($wheelBefore + 120) } 'Direct Wheel retains the original positive 120 default'

    $public = (Invoke-Tool @('CUA','Inspect',"Handle=$script:windowHandle")).result
    $edit = @($public.elements | Where-Object automation_id -eq '101')[0]
    $public = (Invoke-Tool @('CUA','Mouse','Operation=Click',"Handle=$script:windowHandle","X=$($edit.point_image_px.x)","Y=$($edit.point_image_px.y)")).result
    $public = (Invoke-Tool @('CUA','Keys','Keys=ctrl+a',"Handle=$script:windowHandle")).result
    Set-Clipboard -Value 'WUA public clipboard'
    $nativePaste = Invoke-Tool @('CUA','Text','Method=Paste',"Text=$typed","Handle=$script:windowHandle")
    $null = Wait-Fixture { param($value) $value.text -eq $typed } 'Public Paste and Keys preserve Unicode'
    [pscustomobject]@{ source='native'; expected='WUA public clipboard'; actual=(Get-Clipboard -Raw); response=$nativePaste } |
        ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $ArtifactDirectory 'native-clipboard-restoration.json') -Encoding UTF8
    $null = Invoke-Tool @('CUA','Keys','Keys=CTRL+A',"Handle=$script:windowHandle")
    Set-Clipboard -Value 'WUA text-method clipboard'
    if (-not [WuaCuaFixtureNative]::ClipboardMarker($true)) { throw 'Cannot seed the clipboard marker.' }
    $textAfter = Join-Path $ArtifactDirectory 'text-Message.png'
    $charBefore = (Read-Fixture).char_messages
    $public = (Invoke-Tool @('CUA','Text',"Text=$typed","Handle=$script:windowHandle","OutFile=$textAfter")).result
    $null = Wait-Fixture { param($value) $value.text -eq $typed -and
        $value.char_messages -gt $charBefore } 'Default Text uses WM_CHAR when the control declines WM_UNICHAR'
    Assert-Condition ($public.interaction.state -eq 'current' -and
        $public.interaction.focus.within_observed_window -and
        (Test-Path -LiteralPath $textAfter)) 'Message returns native focus and its post-input screenshot'
    Assert-Condition ((Get-Clipboard -Raw) -eq 'WUA text-method clipboard' -and
        [WuaCuaFixtureNative]::ClipboardMarker($false)) 'Message leaves Unicode and binary clipboard data untouched'
    $beforeMenu = Read-Fixture
    $null = Invoke-Tool @('CUA','Keys','Keys=ALT+SPACE',"Handle=$script:windowHandle")
    $menu = (Invoke-Tool @('CUA','Inspect',"Handle=$script:windowHandle")).result
    Assert-Condition ($menu.interaction.state -eq 'current' -and $menu.interaction.menu -eq 'system' -and
        $menu.interaction.menu_owner_window -eq $script:windowHandle) 'ALT+SPACE opens the real target system menu'
    Assert-Condition ($null -eq $menu.interaction.focus -and $null -eq $menu.interaction.caret_hint) `
        'The system menu does not expose retained edit focus or caret as a text destination'
    foreach ($method in @('Message','Paste')) {
        $blockedText = Invoke-Tool @('CUA','Text',"Method=$method",'Text=must not arrive',
            "Handle=$script:windowHandle") -AllowError
        Assert-Condition ($blockedText.hresult -lt 0) "$method rejects text while the system menu is active"
        $afterMenu = Read-Fixture
        Assert-Condition ($afterMenu.text -eq $beforeMenu.text -and
            $afterMenu.char_messages -eq $beforeMenu.char_messages -and
            $afterMenu.unichar_messages -eq $beforeMenu.unichar_messages -and
            (Get-Clipboard -Raw) -eq 'WUA text-method clipboard' -and
            [WuaCuaFixtureNative]::ClipboardMarker($false)) "$method leaves edit messages and clipboard unchanged"
    }
    $null = Invoke-Tool @('CUA','Keys','Keys=ESC',"Handle=$script:windowHandle")
    $menuCloseWatch = [Diagnostics.Stopwatch]::StartNew()
    do {
        $afterMenu = (Invoke-Tool @('CUA','Inspect',"Handle=$script:windowHandle")).result
        $nativeMenu = [WuaCuaFixtureNative]::GuiState($window[0].tid)
        $nativeMenuFlags = $nativeMenu.Flags
        if ($afterMenu.interaction.menu -eq 'none' -and ($nativeMenuFlags -band 0x1c) -eq 0) { break }
        Start-Sleep -Milliseconds 50
    } while ($menuCloseWatch.Elapsed.TotalSeconds -lt 1)
    [IO.File]::WriteAllText((Join-Path $ArtifactDirectory 'system-menu-first-esc.json'),
        (@{native_gui_flags=$nativeMenuFlags;native_menu_owner=$nativeMenu.MenuOwner.ToInt64().ToString('X8');
            observation=$afterMenu} | ConvertTo-Json -Depth 30), [Text.UTF8Encoding]::new($false))
    if (($nativeMenuFlags -band 0x1c) -ne 0) {
        if (-not ($afterMenu.interaction.state -eq 'current' -and
            $afterMenu.foreground -eq $script:windowHandle -and
            $afterMenu.interaction.menu_owner_window -eq $script:windowHandle -and
            $nativeMenu.MenuOwner -eq (Fixture-Handle (Read-Fixture)) -and
            [WuaCuaFixtureNative]::GetForegroundWindow() -eq $nativeMenu.MenuOwner)) {
            throw 'The remaining menu layer does not belong to the same foreground fixture.'
        }
        $null = Invoke-Tool @('CUA','Keys','Keys=ESC',"Handle=$script:windowHandle")
        $menuCloseWatch.Restart()
        do {
            $afterMenu = (Invoke-Tool @('CUA','Inspect',"Handle=$script:windowHandle")).result
            $nativeMenu = [WuaCuaFixtureNative]::GuiState($window[0].tid)
            $nativeMenuFlags = $nativeMenu.Flags
            if ($afterMenu.interaction.menu -eq 'none' -and ($nativeMenuFlags -band 0x1c) -eq 0) { break }
            Start-Sleep -Milliseconds 50
        } while ($menuCloseWatch.Elapsed.TotalSeconds -lt 1)
    }
    [IO.File]::WriteAllText((Join-Path $ArtifactDirectory 'system-menu-close.json'),
        (@{native_gui_flags=$nativeMenuFlags;native_menu_owner=$nativeMenu.MenuOwner.ToInt64().ToString('X8');
            observation=$afterMenu} | ConvertTo-Json -Depth 30),
        [Text.UTF8Encoding]::new($false))
    Assert-Condition ($afterMenu.interaction.state -eq 'current' -and $afterMenu.interaction.menu -eq 'none' -and
        ($nativeMenuFlags -band 0x1c) -eq 0 -and
        $null -ne $afterMenu.interaction.focus -and $afterMenu.interaction.focus.within_observed_window -and
        [WuaCuaFixtureNative]::EditHasFocus($window[0].tid)) `
        'Observed ESC steps close the system menu and restore real edit focus'
    [WuaCuaFixtureNative]::ConfigureTextInput((Fixture-Handle (Read-Fixture)),1)
    $public = (Invoke-Tool @('CUA','Keys','Keys=ctrl+a',"Handle=$script:windowHandle")).result
    $unicharBefore = (Read-Fixture).unichar_messages
    $public = (Invoke-Tool @('CUA','Text','Method=Message',"Text=$typed","Handle=$script:windowHandle")).result
    $null = Wait-Fixture { param($value) $value.text -eq $typed -and $value.unichar_messages -gt $unicharBefore } 'A WM_UNICHAR-capable control receives real UTF-32 characters and preserves emoji'
    $null = Invoke-Tool @('CUA','Text','Method=Unknown','Text=must not arrive',"Handle=$script:windowHandle") -AllowError
    Assert-Condition ((Read-Fixture).text -eq $typed) 'An invalid text method fails before input'
    $public = (Invoke-Tool @('CUA','Keys','Keys=ctrl+a',"Handle=$script:windowHandle")).result
    [WuaCuaFixtureNative]::ConfigureTextInput((Fixture-Handle (Read-Fixture)),2)
    $timeoutImage = Join-Path $ArtifactDirectory 'text-timeout.png'
    $timeout = Invoke-Tool @('CUA','Text','Method=Message','Text=ZZZ',"Handle=$script:windowHandle","OutFile=$timeoutImage") -AllowError
    Assert-Condition ($timeout.hresult -lt 0 -and $timeout.details -match '(?i)inspect') 'Timed-out delivery requires inspection before retry'
    $null = Wait-Fixture { param($value) $value.text -eq 'Z' } 'Only one in-flight character arrives after a message timeout'
    Assert-Condition (Test-Path -LiteralPath $timeoutImage) 'Failed text input still supplies the requested recovery screenshot'
    Start-Sleep -Milliseconds 200
    Assert-Condition ((Read-Fixture).text -eq 'Z') 'No automatic resend follows a character timeout'
    [WuaCuaFixtureNative]::ConfigureTextInput((Fixture-Handle (Read-Fixture)),0)
    $null = Invoke-Tool @('CUA','Keys','Keys=CTRL+A')
    [WuaCuaFixtureNative]::ConfigureTextInput((Fixture-Handle (Read-Fixture)),4)
    $paste = Invoke-Tool @('CUA','Text','Method=Paste','Text=concurrent-paste')
    $null = Wait-Fixture { param($value) $value.text -eq 'concurrent-paste' -and $value.clipboard_changed } 'The receiving application replaces the clipboard after consuming Paste'
    Assert-Condition ((Get-Clipboard -Raw) -eq 'WUA newer clipboard content') 'Paste preserves a newer clipboard owner instead of restoring over it'
    [WuaCuaFixtureNative]::ConfigureTextInput((Fixture-Handle (Read-Fixture)),0)
    $null = Invoke-Tool @('CUA','Keys','Keys=CTRL+A')
    [WuaCuaFixtureNative]::ClearClipboard()
    $paste = Invoke-Tool @('CUA','Text','Method=Paste','Text=empty clipboard paste')
    $null = Wait-Fixture { param($value) $value.text -eq 'empty clipboard paste' } 'Paste also works with an initially empty clipboard'
    Assert-Condition ([string]::IsNullOrEmpty((Get-Clipboard -Raw))) 'An empty clipboard is restored without publishing its lazy wrapper'
    $null = Invoke-Tool @('CUA','Keys','Keys=CTRL+A')
    $null = Invoke-Tool @('CUA','Text','Text=direct input')
    $null = Wait-Fixture { param($value) $value.text -eq 'direct input' } 'Text and Keys also preserve direct calls using current focus'
    $public = (Invoke-Tool @('CUA','Inspect',"Handle=$script:windowHandle")).result
    $edit = @($public.elements | Where-Object automation_id -eq '101')[0]
    $null = Invoke-Tool @('CUA','Element','Operation=SetValue',"Handle=$script:windowHandle","RuntimeId=$($edit.runtime_id)",'Text=semantic tool')
    $null = Wait-Fixture { param($value) $value.text -eq 'semantic tool' } 'Public Element resolves the copied native runtime ID'
    $public = (Invoke-Tool @('CUA','Inspect',"Handle=$script:windowHandle")).result
    $choice = Find-Element $public 'Choice two'
    Assert-Condition $choice.patterns.select 'The standard list item exposes the native UIA SelectionItem pattern'
    $null = Invoke-Tool @('CUA','Element','Operation=Select',"Handle=$script:windowHandle",
        "RuntimeId=$($choice.runtime_id)")
    $null = Wait-Fixture { param($value) $value.selection -eq 1 } 'UIA Select changes the standard listbox selection'
    $null = Invoke-Tool @('CUA','Window','Action=Activate',"Handle=$($state.hwnd)")
    Assert-Condition ([WuaCuaFixtureNative]::GetForegroundWindow() -eq (Fixture-Handle $state)) 'CUA Window remains the shared activation command'
    $placement = Read-Fixture
    $minimized = Invoke-Tool @('CUA','Window','Action=Minimize',"Handle=$script:windowHandle")
    $null = Wait-Fixture { param($value) [WuaCuaFixtureNative]::IsIconic((Fixture-Handle $value)) } 'Minimize changes the actual fixture window state'
    Assert-Condition (@($minimized.result.windows | Where-Object window_handle -eq $script:windowHandle).Count -eq 1) 'Minimize returns desktop context containing the fixture'
    $null = Invoke-Tool @('CUA','Window','Action=Activate',"Handle=$script:windowHandle")
    $null = Wait-Fixture { param($value) -not [WuaCuaFixtureNative]::IsIconic((Fixture-Handle $value)) -and
        [WuaCuaFixtureNative]::GetForegroundWindow() -eq (Fixture-Handle $value) } 'Activate restores the minimized target to the foreground'
    $null = Invoke-Tool @('CUA','Window','Action=Maximize',"Handle=$script:windowHandle")
    $null = Wait-Fixture { param($value) [WuaCuaFixtureNative]::IsZoomed((Fixture-Handle $value)) } 'Maximize changes the actual fixture window state'
    $null = [WuaCuaFixtureNative]::ShowWindow((Fixture-Handle $placement),9)
    Set-FixtureBounds $placement $placement.window.left $placement.window.top `
        ($placement.window.right - $placement.window.left) ($placement.window.bottom - $placement.window.top)


    if ($TestServerRestart) {
        if ($server.HasExited -or $caps.result.pid -ne $server.Id) {
            throw 'Server restart test requires the server instance owned by this test run.'
        }
        $observed = Observe-Fixture
        $oldInstance = $script:serverInstance
        Stop-Process -Id $server.Id -Force
        $null = $server.WaitForExit(5000)
        $script:serverInstance = $null
        $restarted = & $Executable CUA Inspect Handle=0 | ConvertFrom-Json
        if ($LASTEXITCODE -ne 0 -or -not $restarted.ok) { throw 'Public desktop Inspect could not restart the Worker.' }
        $caps = Invoke-Cua capabilities @{}
        $server = Get-Process -Id $caps.result.pid
        $script:serverInstance = $caps.result.instance
        Assert-Condition ($script:serverInstance -ne $oldInstance) 'A restarted server has a new instance identity'
        $desktop = (Invoke-Cua inspect @{ handle = '0'; backend = $Backend; uia = $false }).result
        $script:windowHandle = @($desktop.windows | Where-Object { $_.pid -eq $fixture.Id -and $_.title -eq 'WUA CUA Test Fixture' })[0].window_handle
    }

    if ($TestUiaTimeout) {
        # Configure the fixture directly so an action's automatic observation cannot consume this first UIA request.
        [WuaCuaFixtureNative]::ToggleUiaStall((Fixture-Handle (Read-Fixture)))
        $null = Wait-Fixture { param($state) $state.uia_hang } 'Fixture has enabled its deliberate UIA stall'
        $timer = [Diagnostics.Stopwatch]::StartNew()
        $hung = (Invoke-Cua inspect @{ handle = $script:windowHandle; backend = $Backend; uia = $true; timeout_ms = 1000 }).result
        $timer.Stop()
        Assert-Condition ($hung.uia_state -eq 'timeout' -and
            $null -ne $hung.interaction -and $timer.ElapsedMilliseconds -lt 5000) 'A stalled UIA request stops waiting within its budget while native context remains available'
        $timer.Restart()
        $healthy = Invoke-Cua capabilities @{}
        $native = (Invoke-Cua inspect @{ handle = '0'; backend = $Backend; uia = $false }).result
        Assert-Condition ($healthy.result.pid -eq $caps.result.pid -and
            $healthy.result.instance -eq $script:serverInstance -and $native.windows.Count -gt 0 -and
            $null -ne $native.interaction -and $timer.ElapsedMilliseconds -lt 5000) 'The same Worker remains responsive and captures native context after the UIA timeout'
        $secondReport = Join-Path $ArtifactDirectory 'after-uia-timeout.json'
        $occluder = Start-Process -FilePath $Executable -ArgumentList @('CUA', 'TestHost', ('Report="{0}"' -f $secondReport)) -WindowStyle Hidden -PassThru
        $deadline = [WuaCuaFixtureNative]::GetTickCount64() + 5000
        do {
            try {
                $secondState = Get-Content -LiteralPath $secondReport -Raw -Encoding UTF8 | ConvertFrom-Json
                if ($secondState.pid -ne $occluder.Id) { throw 'Waiting for the healthy fixture process.' }
                break
            } catch {
                if ([WuaCuaFixtureNative]::GetTickCount64() -ge $deadline) { throw }
                Start-Sleep -Milliseconds 50
            }
        } while ($true)
        $frame = [CuaTestClient]::FrameBounds((Fixture-Handle $secondState))
        $x = [int](($secondState.controls.button.left + $secondState.controls.button.right) / 2) - $frame.Left
        $y = [int](($secondState.controls.button.top + $secondState.controls.button.bottom) / 2) - $frame.Top
        $timer.Restart()
        $delivered = Invoke-Cua click @{ handle = $secondState.hwnd; x = $x; y = $y }
        Assert-Condition ($delivered.ok -and $timer.ElapsedMilliseconds -lt 5000) 'Physical input remains available after the UIA timeout'
        $deadline = [WuaCuaFixtureNative]::GetTickCount64() + 5000
        do {
            $received = Get-Content -LiteralPath $secondReport -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($received.clicks -eq 1) { break }
            Start-Sleep -Milliseconds 30
        } while ([WuaCuaFixtureNative]::GetTickCount64() -lt $deadline)
        Assert-Condition ($received.clicks -eq 1) 'The healthy application receives exactly one click after the UIA timeout'
    }
    Write-Host "Passed $script:passed CUA harness assertions. Artifacts: $ArtifactDirectory"
} finally {
    [WuaOleClipboardFixture]::Stop()
    if ($null -ne $dragProcess -and -not $dragProcess.HasExited) { Stop-Process -Id $dragProcess.Id -Force -ErrorAction SilentlyContinue }
    if ($null -ne $occluder -and -not $occluder.HasExited) { Stop-Process -Id $occluder.Id -Force -ErrorAction SilentlyContinue }
    if ($null -ne $fixture -and -not $fixture.HasExited) { Stop-Process -Id $fixture.Id -Force -ErrorAction SilentlyContinue }
    if ($null -ne $server -and -not $server.HasExited) { Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue }
}
