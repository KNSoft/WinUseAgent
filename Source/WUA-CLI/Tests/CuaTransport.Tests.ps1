# Requires a running CUA Server in the selected session; never stops that server.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Exe, [int]$SessionId = (Get-Process -Id $PID).SessionId)
$ErrorActionPreference = 'Stop'
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$Exe = (Resolve-Path -LiteralPath $Exe).Path
. (Join-Path $PSScriptRoot 'CuaTestClient.ps1')
$results = [Collections.Generic.List[string]]::new()
function Assert-Rpc([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "FAIL: $Name" }
    $results.Add($Name)
    Write-Output "PASS: $Name"
}
function Call-Rpc([string]$Method, [object]$Params = @{}) {
    $request = @{method=$Method; params=$Params}
    return Invoke-CuaTestRpc $Exe $SessionId ($request | ConvertTo-Json -Depth 10 -Compress) | ConvertFrom-Json
}
$cap = Call-Rpc capabilities
Assert-Rpc ($cap.ok -and $cap.result.instance -and
    (Get-Process -Id $cap.result.pid).SessionId -eq $SessionId) 'RPC capabilities belongs to the requested Windows session'
Assert-Rpc (-not ($cap.PSObject.Properties.Name -contains 'id') -and
    -not ($cap.PSObject.Properties.Name -contains 'server')) 'RPC replies omit redundant correlation and Server fields'
$unknown = Call-Rpc __rpc_test_unknown__
Assert-Rpc (-not $unknown.ok -and $unknown.hresult -lt 0 -and $unknown.details) 'an unknown method returns the common error result'
foreach ($field in @('id','expected_server','unexpected')) {
    $request = @{method='capabilities'; params=@{}}
    $request[$field] = 'removed-or-unknown'
    $rejected = Invoke-CuaTestRpc $Exe $SessionId ($request | ConvertTo-Json -Compress) | ConvertFrom-Json
    Assert-Rpc (-not $rejected.ok -and $rejected.hresult -lt 0 -and
        $rejected.details -match 'Unknown request field') "removed/unknown request field $field is rejected"
}
$invalid = Invoke-CuaTestRpc $Exe $SessionId '{' | ConvertFrom-Json
Assert-Rpc (-not $invalid.ok -and $invalid.hresult -lt 0 -and $invalid.details -and
    $invalid.PSObject.Properties.Name -contains 'hresult_text') 'malformed JSON uses the common HRESULT error result'
foreach ($entry in @(
    @{Name='empty RPC request is rejected'; Request=''},
    @{Name='oversized RPC request is rejected'; Request=('a' * 1048577)}
)) {
    $rejected = Invoke-CuaTestRpc $Exe $SessionId $entry.Request | ConvertFrom-Json
    Assert-Rpc (-not $rejected.ok -and $rejected.hresult -lt 0 -and $rejected.details) $entry.Name
}
$duplicate = Start-Process -FilePath $Exe -ArgumentList @('CUA','Server') -WindowStyle Hidden -PassThru
$null = $duplicate.Handle
try {
    $exited = $duplicate.WaitForExit(15000)
    Assert-Rpc ($exited -and $null -ne $duplicate.ExitCode -and
        $duplicate.ExitCode -ne 0) 'an additional Server is refused'
} finally {
    if (-not $duplicate.HasExited) { Stop-Process -Id $duplicate.Id -Force }
    $duplicate.Dispose()
}
$absentSession = $SessionId + 50000
$absent = & $Exe CUA Inspect "Session=$absentSession" | ConvertFrom-Json
Assert-Rpc ($LASTEXITCODE -ne 0 -and -not $absent.ok -and
    $absent.hresult -lt 0) 'an unavailable Windows session does not fall back to the current Worker'
$probe = @{method='capabilities'; params=@{}} | ConvertTo-Json -Compress
$low = Invoke-CuaTestRpc $Exe $SessionId $probe -Identity LowIntegrity | ConvertFrom-Json
Assert-Rpc (-not $low.ok -and $low.hresult -eq -2147024891 -and
    $low.details -match '^The RPC') 'RPC authorization rejects a lower-integrity caller'
$filtered = Invoke-CuaTestRpc $Exe $SessionId $probe -Identity FilteredAdmin | ConvertFrom-Json
Assert-Rpc $filtered.ok 'a same-user caller with its administrator group disabled is accepted'
$healthy = Call-Rpc capabilities
Assert-Rpc ($healthy.ok -and $healthy.result.pid -eq $cap.result.pid -and
    $healthy.result.instance -eq $cap.result.instance) 'the same Server remains healthy after negative RPC tests'
Write-Output "Completed $($results.Count) transport assertions."
