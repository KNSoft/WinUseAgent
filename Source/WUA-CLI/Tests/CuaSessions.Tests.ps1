# Requires a disposable parent desktop with Child Sessions configured before logon.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Executable,
    [int]$SessionId = (Get-Process -Id $PID).SessionId,
    [Parameter(Mandatory)][string]$ArtifactDirectory,
    [Parameter(Mandatory)][switch]$DisposableChild,
    [switch]$ConfigureChildSessions
)
$ErrorActionPreference = 'Stop'
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
. (Join-Path $PSScriptRoot 'CuaTestClient.ps1')
if (-not $DisposableChild) { throw 'This test creates, faults, and destroys only its own disposable child.' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
New-Item -ItemType Directory -Path $ArtifactDirectory -Force | Out-Null
$ArtifactDirectory = (Resolve-Path -LiteralPath $ArtifactDirectory).Path
$script:sequence = 0
$script:passed = 0
$script:createProcesses = @()
$ownedSessionId = $null
if (-not ('WuaSessionClock' -as [type])) {
    Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class WuaSessionClock {
    [DllImport("kernel32.dll")] public static extern ulong GetTickCount64();
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern uint GetWindowLong(IntPtr window, int index);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr window, StringBuilder text, int size);
    delegate bool EnumProc(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr data);
    public static IntPtr Preview(uint owner) {
        IntPtr found=IntPtr.Zero;
        EnumWindows(delegate(IntPtr window, IntPtr data) {
            uint pid; GetWindowThreadProcessId(window,out pid);
            if(pid==owner) {
                var title=new StringBuilder(100); GetWindowText(window,title,title.Capacity);
                if(title.ToString()=="WUA CUA Child Session") { found=window; return false; }
            }
            return true;
        },IntPtr.Zero);
        return found;
    }
}
'@
}
function Assert-Session([bool]$Condition, [string]$Description) {
    if (-not $Condition) { throw $Description }
    $script:passed++
    Write-Host "PASS: $Description"
}
function Call-Cua([int]$TargetSession, [string]$Method, [hashtable]$Parameters = @{}, [switch]$AllowError) {
    $script:sequence++
    $path = Join-Path $ArtifactDirectory ('request-{0:D3}.json' -f $script:sequence)
    $image = Join-Path $ArtifactDirectory ('frame-{0:D3}.png' -f $script:sequence)
    if ($Method -eq 'inspect') { $Parameters = $Parameters.Clone(); $Parameters.outfile = $image }
    $request = @{method=$Method; params=$Parameters}
    $text = $request | ConvertTo-Json -Depth 15 -Compress
    [IO.File]::WriteAllText($path, $text, [Text.UTF8Encoding]::new($false))
    $reply = Invoke-CuaTestRpc $Executable $TargetSession $text | ConvertFrom-Json
    $reply | ConvertTo-Json -Depth 30 | Set-Content (Join-Path $ArtifactDirectory ('response-{0:D3}.json' -f $script:sequence)) -Encoding UTF8
    if (-not $AllowError -and -not $reply.ok) { throw "$Method failed: $text" }
    return $reply
}
function Wait-Child([Nullable[int]]$ChildId = $null, [string]$OldInstance = '') {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    do {
        $list = (Call-Cua $SessionId session.list).result
        $child = @($list | Where-Object { $_.owned -and ($null -eq $ChildId -or $_.id -eq $ChildId) })
        if ($child.Count -ne 1) { throw "Expected exactly one owned session, ID=$ChildId" }
        $child = $child[0]
        if ($null -ne $child.id) { $ChildId = [int]$child.id }
        if ($child.state -eq 'ready' -and $null -ne $child.id) {
            $capabilities = Call-Cua $child.id capabilities -AllowError
            if ($capabilities.ok -and $capabilities.result.desktop_ready -and
                (-not $OldInstance -or $capabilities.result.instance -ne $OldInstance)) { return $child }
        }
        if ($child.state -in @('failed', 'closed', 'destroy_failed')) { throw ($child | ConvertTo-Json -Compress) }
        Start-Sleep -Milliseconds 300
    } while ($watch.Elapsed.TotalSeconds -lt 360)
    throw "Child did not become ready: $($child | ConvertTo-Json -Compress)"
}
function Start-ChildCreation([bool]$Show) {
    $script:sequence++
    $output = Join-Path $ArtifactDirectory ('create-{0:D3}.json' -f $script:sequence)
    $errorFile = Join-Path $ArtifactDirectory ('create-{0:D3}.stderr.txt' -f $script:sequence)
    $process = Start-Process -FilePath $Executable -ArgumentList @('CUA','Session','CreateChild',
        "Session=$SessionId",'Width=1280','Height=720',"Show=$($Show.ToString().ToLowerInvariant())") `
        -RedirectStandardOutput $output -RedirectStandardError $errorFile -WindowStyle Hidden -PassThru
    $null = $process.Handle
    $script:createProcesses += $process
    return @{Process=$process; Output=$output}
}
function Complete-ChildCreation($Creation) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while (-not $Creation.Process.HasExited -and $watch.Elapsed.TotalSeconds -lt 360) {
        Start-Sleep -Milliseconds 300
    }
    if (-not $Creation.Process.HasExited) { throw 'CreateChild has not completed its credential/startup workflow.' }
    $Creation.Process.WaitForExit()
    $reply = [IO.File]::ReadAllText($Creation.Output) | ConvertFrom-Json
    Assert-Session ($Creation.Process.ExitCode -eq 0 -and $reply.ok -and
        ($reply.result.id -is [int] -or $reply.result.id -is [long]) -and
        $reply.result.id -gt 0 -and $reply.result.id -lt [uint32]::MaxValue -and
        @($reply.result.PSObject.Properties).Count -eq 1) 'CreateChild succeeds only with its usable numeric Windows Session ID'
    return $reply.result
}
function Read-Fixture {
    for ($n = 0; $n -lt 100; $n++) {
        if (Test-Path -LiteralPath $reportPath) {
            try { return [IO.File]::ReadAllText($reportPath) | ConvertFrom-Json } catch {}
        }
        Start-Sleep -Milliseconds 100
    }
    throw 'Child fixture did not create its report.'
}
function Observe-Fixture {
    return (Call-Cua $child.id inspect @{handle=$windowHandle; uia=$true}).result
}
function Wait-Fixture([scriptblock]$Condition, [string]$Description) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    do {
        if (& $Condition (Read-Fixture)) { Assert-Session $true $Description; return }
        Start-Sleep -Milliseconds 30
    } while ($watch.Elapsed.TotalSeconds -lt 5)
    throw "Fixture condition timed out: $Description"
}
function Element($Inspection, [string]$Name) {
    $items = @($Inspection.elements | Where-Object name -eq $Name)
    if ($items.Count -ne 1) { throw "Expected one fixture element: $Name" }
    return $items[0]
}

if ($ConfigureChildSessions) {
    $computer = Get-CimInstance Win32_ComputerSystem
    if ($computer.Manufacturer -ne 'Microsoft Corporation' -or $computer.Model -ne 'Virtual Machine') {
        throw 'ConfigureChildSessions is permitted only inside a disposable Hyper-V guest.'
    }
    $settings = & $Executable CUA Session EnableChildSession | ConvertFrom-Json
    $applied = @($settings.result.PSObject.Properties.Value | Where-Object { -not $_.applied -or $_.error -ne 0 })
    Assert-Session ($LASTEXITCODE -eq 0 -and $settings.ok -and $applied.Count -eq 0 -and
        @($settings.result.PSObject.Properties).Count -eq 4) 'EnableChildSession returns only the four applied settings'
}
$initialDesktop = & $Executable CUA Inspect "Session=$SessionId" | ConvertFrom-Json
if ($LASTEXITCODE -ne 0 -or -not $initialDesktop.ok) { throw 'Public desktop Inspect could not start the parent Worker.' }
$parentReply = Call-Cua $SessionId capabilities
$parentCaps = $parentReply.result
$parentIdentity = [CuaTestClient]::ProcessIdentity($parentCaps.pid)
$initial = (Call-Cua $SessionId session.list).result
if (@($initial | Where-Object owned).Count) { throw 'The parent already owns a child; refusing to reuse or destroy it.' }
try {
    $creation = Start-ChildCreation $true
    $watch = [Diagnostics.Stopwatch]::StartNew()
    do {
        $owned = @((Call-Cua $SessionId session.list).result | Where-Object owned)
        if ($owned.Count -eq 1) { break }
        Start-Sleep -Milliseconds 100
    } while ($watch.Elapsed.TotalSeconds -lt 10)
    Assert-Session ($owned.Count -eq 1) 'Creating child is registered without blocking parent List'
    if ($null -ne $owned[0].id) { $ownedSessionId = [int]$owned[0].id }
    if ($owned[0].state -ne 'ready') {
        Assert-Session (-not $creation.Process.HasExited) 'Pending login does not return a successful CreateChild'
        $parentObservation = & $Executable CUA Inspect "Session=$SessionId" | ConvertFrom-Json
        Assert-Session ($LASTEXITCODE -eq 0 -and $parentObservation.ok) 'Parent observation remains available while child creation waits'
    }
    $reuse = Start-ChildCreation $true
    $created = Complete-ChildCreation $creation
    $again = Complete-ChildCreation $reuse
    $ownedSessionId = [int]$created.id
    $child = Wait-Child $ownedSessionId
    $ownedChildren = @((Call-Cua $SessionId session.list).result | Where-Object owned)
    Assert-Session ($ownedChildren.Count -eq 1 -and $again.id -eq $ownedSessionId -and
        $child.width -eq 1280 -and $child.height -eq 720) 'Concurrent matching CreateChild calls reuse one authenticated child'
    Assert-Session ($ownedSessionId -ne $SessionId) 'Child is callable immediately after CreateChild succeeds'
    $childReply = Call-Cua $child.id capabilities
    $caps = $childReply.result
    $initialChildWorkerPid = $caps.pid
    Assert-Session $caps.desktop_ready 'Ready child has an available graphics desktop'
    $worker = Get-Process -Id $caps.pid
    Assert-Session ($worker.SessionId -eq $child.id -and $worker.Path -eq $Executable) 'Child uses the exact executable under test'
    $publicList = & $Executable CUA Session List "Session=$SessionId" | ConvertFrom-Json
    $publicChild = @($publicList.result | Where-Object id -eq $ownedSessionId)
    Assert-Session ($LASTEXITCODE -eq 0 -and $publicChild.Count -eq 1 -and $publicChild[0].state -eq 'ready' -and
        $publicChild[0].owned) 'Parent Session List identifies the owned child by Windows Session ID'
    $childList = & $Executable CUA Session List "Session=$ownedSessionId" | ConvertFrom-Json
    $childSelf = @($childList.result | Where-Object id -eq $child.id)
    Assert-Session ($LASTEXITCODE -eq 0 -and $childList.ok -and $childSelf.Count -eq 1 -and
        -not $childSelf[0].owned -and $childSelf[0].state -eq 'ready') 'Numeric Session List executes in the selected child Worker'
    $routed = & $Executable CUA Inspect "Session=$ownedSessionId" | ConvertFrom-Json
    Assert-Session ($LASTEXITCODE -eq 0 -and $routed.ok -and $routed.result.geometry.width -eq 1280 -and
        $routed.result.geometry.height -eq 720) 'Public Inspect selects the child desktop by its Windows Session ID'
    $childIdentity = [CuaTestClient]::ProcessIdentity($caps.pid)
    Assert-Session ($childIdentity.UserSid -eq $parentIdentity.UserSid) 'Child Worker preserves the parent user identity'
    $shell = @(Get-Process explorer | Where-Object SessionId -eq $child.id)
    Assert-Session ($shell.Count -eq 1) 'Child has an independently verified Explorer logon'
    $childExplorerPid = $shell[0].Id
    $preview = [WuaSessionClock]::Preview($parentCaps.pid)
    Assert-Session ($preview -ne [IntPtr]::Zero -and [WuaSessionClock]::IsWindowVisible($preview) -and $child.preview_visible) 'Show=true creates a visible preview owned by the parent Server'
    $hidden = & $Executable CUA Session Preview "Session=$ownedSessionId" Show=false | ConvertFrom-Json
    Assert-Session ($hidden.ok -and $null -eq $hidden.result -and -not [WuaSessionClock]::IsWindowVisible($preview)) 'CLI Preview Show=false hides the actual preview'
    $sameWorker = (Call-Cua $child.id capabilities).result
    Assert-Session ($sameWorker.pid -eq $caps.pid -and (Get-Process -Id $childExplorerPid).SessionId -eq $child.id) 'Hiding the preview retains both Worker and Explorer'
    $desktop = (Call-Cua $child.id inspect @{uia=$false}).result
    Assert-Session ($desktop.geometry.width -eq 1280 -and $desktop.geometry.height -eq 720) 'Child capture keeps its fixed resolution while the preview is hidden'
    $shown = Call-Cua $SessionId session.preview @{session="$ownedSessionId"; show=$true}
    Assert-Session ($shown.ok -and $null -eq $shown.result -and [WuaSessionClock]::IsWindowVisible($preview) -and
        (Call-Cua $child.id capabilities).result.pid -eq $caps.pid) 'Preview Show=true shows the existing preview without replacing the Worker'
    $denied = Call-Cua $SessionId session.preview @{session="$SessionId"; show=$false} -AllowError
    Assert-Session (-not $denied.ok -and [WuaSessionClock]::IsWindowVisible($preview)) 'Preview cannot change an unowned session'
    $missing = Call-Cua $SessionId session.preview @{session="$ownedSessionId"} -AllowError
    Assert-Session (-not $missing.ok -and $missing.hresult -lt 0 -and $missing.details -match 'Show') 'Preview requires an explicit visibility value'
    $null = Call-Cua $SessionId session.preview @{session="$ownedSessionId"; show=$false}
    $null = Call-Cua $child.id click @{x=1100; y=550}
    $desktop = (Call-Cua $child.id inspect @{uia=$false}).result
    $null = Call-Cua $child.id keys @{keys=@('WIN','R')}
    Start-Sleep -Milliseconds 500
    $desktop = (Call-Cua $child.id inspect @{uia=$true}).result
    $reportPath = Join-Path $ArtifactDirectory 'child-fixture.json'
    Remove-Item -LiteralPath $reportPath -ErrorAction SilentlyContinue
    $command = '"{0}" CUA TestHost Report="{1}"' -f $Executable, $reportPath
    $null = Call-Cua $child.id type @{text=$command}
    $desktop = (Call-Cua $child.id inspect @{uia=$false}).result
    $null = Call-Cua $child.id keys @{keys=@('ENTER')}
    $fixture = Read-Fixture
    Assert-Session ((Get-Process -Id $fixture.pid).SessionId -eq $child.id) 'CUA keyboard input launches the controlled application in the child'
    $desktop = (Call-Cua $child.id inspect @{uia=$false}).result
    $windows = @($desktop.windows | Where-Object { $_.pid -eq $fixture.pid -and $_.title -eq 'WUA CUA Test Fixture' })
    Assert-Session ($windows.Count -eq 1) 'Child desktop observation discovers its own application'
    $windowHandle = $windows[0].window_handle
    $inspection = Observe-Fixture
    $button = Element $inspection 'Increment counter'
    $null = Call-Cua $child.id click @{handle=$windowHandle; x=$button.point_image_px.x; y=$button.point_image_px.y}
    Wait-Fixture { param($state) $state.clicks -eq 1 } 'Child window click reaches the intended control'
    $inspection = Observe-Fixture
    $null = Call-Cua $child.id invoke @{handle=$windowHandle; runtime_id=(Element $inspection 'Increment counter').runtime_id}
    $inspection = Observe-Fixture
    $null = Call-Cua $child.id toggle @{handle=$windowHandle; runtime_id=(Element $inspection 'Test checkbox').runtime_id}
    Wait-Fixture { param($state) $state.clicks -eq 2 -and $state.checked } 'Child UIA Invoke and Toggle reach the intended controls'
    $inspection = Observe-Fixture
    $edit = @($inspection.elements | Where-Object { $_.automation_id -eq '101' })[0]
    $null = Call-Cua $child.id click @{handle=$windowHandle; x=$edit.point_image_px.x; y=$edit.point_image_px.y}
    $inspection = Observe-Fixture
    $null = Call-Cua $child.id keys @{handle=$windowHandle; keys=@('CTRL','A')}
    $inspection = Observe-Fixture
    $null = Call-Cua $child.id type @{handle=$windowHandle; text='CUA Child 中文 🙂'}
    Wait-Fixture { param($state) $state.text -eq 'CUA Child 中文 🙂' } 'Child Unicode input is verified from received window messages'
    $inspection = Observe-Fixture
    $scroll = Element $inspection 'Scroll area'
    $x = [int]$scroll.point_image_px.x
    $y = [int]$scroll.point_image_px.y
    $null = Call-Cua $child.id click @{handle=$windowHandle; x=$x; y=$y}
    $null = Call-Cua $child.id move @{handle=$windowHandle; x=$x; y=$y}
    $null = Call-Cua $child.id scroll @{handle=$windowHandle; x=$x; y=$y; delta=-120; axis='vertical'}
    $null = Call-Cua $child.id scroll @{handle=$windowHandle; x=$x; y=$y; delta=120; axis='horizontal'}
    $null = Call-Cua $child.id drag @{handle=$windowHandle; x=$x; y=$y; to_x=($x+25); to_y=($y+25); duration_ms=250}
    Wait-Fixture { param($state) $state.wheel_vertical -eq -120 -and $state.wheel_horizontal -eq 120 -and
        $state.scroll_clicks -eq 2 -and -not $state.mouse_down } 'Child Move, both wheel axes, and Drag reach the fixture and release input'
    $publicInspection = & $Executable CUA Inspect "Session=$ownedSessionId" "Handle=$windowHandle" | ConvertFrom-Json
    $publicEdit = @($publicInspection.result.elements | Where-Object automation_id -eq '101')[0]
    $publicInput = & $Executable CUA Mouse "Session=$ownedSessionId" Operation=Click "Handle=$windowHandle" "X=$($publicEdit.point_image_px.x)" "Y=$($publicEdit.point_image_px.y)" | ConvertFrom-Json
    $publicKeys = & $Executable CUA Keys "Session=$ownedSessionId" "Handle=$windowHandle" Keys=CTRL+A | ConvertFrom-Json
    $publicText = & $Executable CUA Text "Session=$ownedSessionId" "Handle=$windowHandle" 'Text=public child tools 中文' | ConvertFrom-Json
    Assert-Session ($LASTEXITCODE -eq 0 -and $publicInspection.ok -and $publicInput.ok -and $publicKeys.ok -and
        $publicText.ok) 'Public Inspect, Mouse, Keys and Text share numeric child-session routing'
    Wait-Fixture { param($state) $state.text -eq 'public child tools 中文' } 'Child application receives input routed by the numeric public Windows Session ID'
    $located = & $Executable Run Locate "Session=$ownedSessionId" "Path=$reportPath" | ConvertFrom-Json
    Assert-Session ($LASTEXITCODE -eq 0 -and $located.ok) 'Run Locate submits the shell request in the selected child session'
    $located | ConvertTo-Json -Depth 30 |
        Set-Content (Join-Path $ArtifactDirectory 'locate-response.json') -Encoding UTF8
    $locateWatch = [Diagnostics.Stopwatch]::StartNew()
    do {
        $locatedDesktop = & $Executable CUA Inspect "Session=$ownedSessionId" | ConvertFrom-Json
        if ($LASTEXITCODE -ne 0 -or -not $locatedDesktop.ok) { throw 'Inspect failed while waiting for Explorer.' }
        $locatedWindows = @($locatedDesktop.result.windows | Where-Object class -eq 'CabinetWClass')
        if ($locatedWindows.Count -gt 0) { break }
        Start-Sleep -Milliseconds 200
    } while ($locateWatch.Elapsed.TotalSeconds -lt 10)
    $locatedDesktop | ConvertTo-Json -Depth 30 |
        Set-Content (Join-Path $ArtifactDirectory 'locate-desktop.json') -Encoding UTF8
    Assert-Session ($locatedWindows.Count -gt 0) 'Run Locate opens Explorer in the selected child session'
    $locatedExplorer = Get-Process -Id $locatedWindows[0].pid
    Assert-Session ($locatedExplorer.ProcessName -eq 'explorer' -and $locatedExplorer.SessionId -eq $ownedSessionId) 'The located Explorer window belongs to the selected child session'
    Assert-Session (-not [WuaSessionClock]::IsWindowVisible($preview)) 'Child capture, UIA, keyboard, and mouse tests completed while the preview stayed hidden'
    $null = Call-Cua $SessionId session.preview @{session="$ownedSessionId"; show=$true}

    for ($crash = 1; $crash -le 2; $crash++) {
        $caps = (Call-Cua $child.id capabilities).result
        $worker = Get-Process -Id $caps.pid
        if ($worker.SessionId -ne $child.id -or $worker.Path -ne $Executable) { throw 'Fault target identity mismatch.' }
        Stop-Process -Id $worker.Id -Force
        $next = Wait-Child $ownedSessionId $caps.instance
        $restored = (Call-Cua $next.id capabilities).result
        Assert-Session ($next.id -eq $child.id -and $restored.pid -ne $caps.pid -and
            $restored.instance -ne $caps.instance -and
            (Get-Process -Id $childExplorerPid).SessionId -eq $child.id) "Worker failure $crash recovers automatically without replacing the authenticated logon"
        $recoveredObservation = & $Executable CUA Inspect "Session=$ownedSessionId" | ConvertFrom-Json
        Assert-Session ($LASTEXITCODE -eq 0 -and $recoveredObservation.ok) 'Recovered Worker accepts the same numeric session route'
        $child = $next
    }
    $caps = (Call-Cua $child.id capabilities).result
    $invalid = Call-Cua $SessionId session.destroy @{session="$SessionId"} -AllowError
    Assert-Session (-not $invalid.ok) 'Destroy cannot log off the attached parent session'
    $childId = $child.id
    $destroyed = & $Executable CUA Session Destroy "Session=$ownedSessionId" | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0 -or -not $destroyed.ok) { throw 'Public Destroy failed.' }
    $ownedSessionId = $null
    Assert-Session (-not (Get-Process -Id $childExplorerPid -ErrorAction SilentlyContinue)) 'Destroy logs off the owned child Explorer'
    Assert-Session (-not (Get-Process -Id $caps.pid -ErrorAction SilentlyContinue)) 'Destroy terminates the child Worker with its session'
    Assert-Session (@((Call-Cua $SessionId session.list).result | Where-Object owned).Count -eq 0) 'Destroyed child is removed from the owner registry'

    $new = Complete-ChildCreation (Start-ChildCreation $false)
    $ownedSessionId = [int]$new.id
    $second = Wait-Child $ownedSessionId
    $ownedSessionId = [int]$second.id
    Assert-Session ($second.owned -and $second.id -ne $SessionId -and $second.state -eq 'ready') 'A new owned child becomes ready after the original is destroyed'
    $preview = [WuaSessionClock]::Preview($parentCaps.pid)
    Assert-Session ($preview -ne [IntPtr]::Zero -and -not $second.preview_visible -and -not [WuaSessionClock]::IsWindowVisible($preview)) 'Show=false creates a hidden preview'
    $shown = & $Executable CUA Session Preview "Session=$ownedSessionId" Show=true | ConvertFrom-Json
    Assert-Session ($shown.ok -and $null -eq $shown.result -and [WuaSessionClock]::IsWindowVisible($preview) -and
        ([WuaSessionClock]::GetWindowLong($preview, -16) -band 0x00c40000) -eq 0x00c40000) 'An initially hidden preview can be shown with a caption and resize border'
    $destroyed = & $Executable CUA Session Destroy "Session=$ownedSessionId" | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0 -or -not $destroyed.ok) { throw 'Public Destroy failed.' }
    $ownedSessionId = $null
    @{ok=$true;assertions=$script:passed;parent_session=$SessionId;child_session=$childId;second_child_session=$second.id;
        parent_worker_pid=$parentCaps.pid;initial_child_worker_pid=$initialChildWorkerPid;
        parent_identity=$parentIdentity;child_identity=$childIdentity} |
        ConvertTo-Json -Depth 5 | Set-Content (Join-Path $ArtifactDirectory 'result.json') -Encoding UTF8
    Write-Host "Passed $script:passed ChildSession assertions. Artifacts: $ArtifactDirectory"
} catch {
    @{ok=$false;assertions=$script:passed;error=$_.Exception.Message;stack=$_.ScriptStackTrace;retained_session_id=$ownedSessionId} |
        ConvertTo-Json | Set-Content (Join-Path $ArtifactDirectory 'result.json') -Encoding UTF8
    # Leave this disposable child available for diagnosis; never destroy any other owner's session.
    throw
} finally {
    foreach ($process in $script:createProcesses) {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
        $process.Dispose()
    }
}
