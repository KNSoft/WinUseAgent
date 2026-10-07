param([string]$NuGet = 'nuget.exe', [string]$MSBuild = '')

$ErrorActionPreference = 'Stop'
$SourceCommit = '93f8aac25264421956a131fbb22eb5686f38847f'
$ArchiveHash = '82DBE95A4B655EAABA3D65AE2AF8C656A10308B607A36137B28B272A919D4783'
$PatchHash = '36F04F40AB21120A86B4C339F96852B3B5FB81F836A643020A905599B144E4F9'
$SourceRoot = Split-Path $PSScriptRoot -Parent
$OutputRoot = Join-Path $SourceRoot 'OutDir/MleLocal4'
$Patch = Join-Path $PSScriptRoot 'MLE-ProcessFlags.patch'
$WorkRoot = Join-Path ([IO.Path]::GetTempPath()) ('Wua-MLE-' + [Guid]::NewGuid())
$Feed = 'https://api.nuget.org/v3/index.json'

if ($MSBuild -eq '') {
    $VSWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $MSBuild = & $VSWhere -latest -products '*' -requires Microsoft.Component.MSBuild -find MSBuild/**/Bin/MSBuild.exe |
        Select-Object -First 1
}
if (-not $MSBuild -or -not (Test-Path -LiteralPath $MSBuild)) { throw 'Visual Studio MSBuild is required.' }
if ((Get-FileHash -LiteralPath $Patch -Algorithm SHA256).Hash -ne $PatchHash) { throw 'MLE patch hash mismatch.' }

New-Item -ItemType Directory -Path $OutputRoot, $WorkRoot -Force | Out-Null
try {
    $Archive = Join-Path $OutputRoot 'source.zip'
    Invoke-WebRequest "https://codeload.github.com/KNSoft/KNSoft.MakeLifeEasier/zip/$SourceCommit" `
        -UseBasicParsing -OutFile $Archive
    if ((Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash -ne $ArchiveHash) {
        throw 'MLE source archive hash mismatch.'
    }
    Expand-Archive -LiteralPath $Archive -DestinationPath $WorkRoot
    $MleRoot = Join-Path $WorkRoot "KNSoft.MakeLifeEasier-$SourceCommit"
    & git -C $MleRoot apply --check $Patch
    if ($LASTEXITCODE -ne 0) { throw 'MLE patch validation failed.' }
    & git -C $MleRoot apply --whitespace=nowarn $Patch
    if ($LASTEXITCODE -ne 0) { throw 'MLE patch failed.' }
    & $NuGet restore "$MleRoot/Source/KNSoft.MakeLifeEasier/packages.config" `
        -PackagesDirectory "$MleRoot/Source/packages" -Source $Feed -NonInteractive
    if ($LASTEXITCODE -ne 0) { throw 'MLE dependency restore failed.' }
    foreach ($Arch in 'x64', 'x86') {
        foreach ($Configuration in 'Debug', 'Release') {
            & $MSBuild "$MleRoot/Source/KNSoft.MakeLifeEasier.slnx" /m /nr:false /nologo /v:minimal `
                "/p:Platform=$Arch" "/p:Configuration=$Configuration" `
                "/flp:logfile=$OutputRoot/build-$Arch-$Configuration.log;verbosity=minimal"
            if ($LASTEXITCODE -ne 0) { throw "MLE $Arch $Configuration build failed." }
        }
    }
    & $NuGet pack "$PSScriptRoot/KNSoft.MakeLifeEasier.nuspec" `
        -Properties "MleRoot=$MleRoot;Patch=$Patch" -OutputDirectory $OutputRoot -NonInteractive
    if ($LASTEXITCODE -ne 0) { throw 'MLE package creation failed.' }
    & $NuGet restore "$SourceRoot/WUA-CLI/packages.config" `
        -PackagesDirectory "$SourceRoot/packages" -Source "$OutputRoot;$Feed" -NonInteractive
    if ($LASTEXITCODE -ne 0) { throw 'WUA dependency restore failed.' }
} finally {
    $ResolvedWorkRoot = [IO.Path]::GetFullPath($WorkRoot)
    $TempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
    if ((Split-Path $ResolvedWorkRoot -Parent) -ne $TempRoot -or
        (Split-Path $ResolvedWorkRoot -Leaf) -notlike 'Wua-MLE-*') {
        throw 'Unexpected MLE temporary directory.'
    }
    Remove-Item -LiteralPath $ResolvedWorkRoot -Recurse -Force -ErrorAction SilentlyContinue
}
