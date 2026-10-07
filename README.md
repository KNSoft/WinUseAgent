# WinUseAgent

Windows tools and a CUA harness in one `WUA-CLI.exe`.

```powershell
WUA-CLI.exe CUA Inspect OutFile=desktop.png
WUA-CLI.exe CUA Inspect Handle=<hex> OutFile=window.png
WUA-CLI.exe CUA Mouse Operation=Click Handle=<hex> X=120 Y=80
WUA-CLI.exe CUA Text Handle=<hex> "Text=hello"
```

CUA provides desktop/window inspection, UIA, window operations, input and child sessions. Omit `Session` for the caller's Windows session, or specify a Windows session ID. Omit `Handle` or use `0` for the desktop; a hexadecimal handle selects a window. Coordinates belong to the screenshot, starting at its upper-left corner. Window screenshots include the nonclient area.

Read the [agent workflow](Source/WUA-CLI/AGENTS.md) and [tool reference](Source/WUA-CLI/Tools/CUA/README.md). `OutFile` requests a PNG at the specified path. Without it, inspection returns information without taking a screenshot.

## Build

Use Visual Studio's C++ workload, Windows SDK, Git and NuGet CLI. Until MLE publishes its
`PS_CreateProcess` flags update, build the pinned public MLE source and restore dependencies once:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Source/Build/Build-MLE.ps1
```

The script verifies the source and patch hashes, builds the four x64/x86 Debug/Release libraries,
and restores `1.0.12-beta.local.4` from `Source/OutDir/MleLocal4`. It does not use another MLE checkout.
Then build the whole solution:

```powershell
msbuild Source/WinUseAgent.slnx /m /p:Configuration=Release /p:Platform=x64
msbuild Source/WinUseAgent.slnx /m /p:Configuration=Release /p:Platform=x86
```

Outputs are in `Source/OutDir/<architecture>/<configuration>/`. See [integration tests](Source/WUA-CLI/Tests/README.md) and [runtime design](Source/WUA-CLI/Tools/CUA/Architecture.md) for development.
