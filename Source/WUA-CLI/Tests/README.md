# GUI integration tests

Run input tests in a disposable VM's interactive session, never on the physical host or a desktop in active human use. Tests create their own GUI and independently verify received messages, text, focus, coordinates, process identity and session lifecycle. An elevated runner is needed for cross-session fault injection.

```powershell
pwsh -File CuaHarness.Tests.ps1 -Executable <WUA-CLI.exe> -ArtifactDirectory <directory> -TestServerRestart -TestUiaTimeout
pwsh -File CuaTransport.Tests.ps1 -Exe <WUA-CLI.exe> -SessionId <id>
pwsh -File CuaSessions.Tests.ps1 -Executable <WUA-CLI.exe> -ArtifactDirectory <directory> -DisposableChild
pwsh -File CuaDpi.Tests.ps1 -Executable <WUA-CLI.exe> -ArtifactDirectory <directory> -RequireScaledDesktop
```

Harness tests use WGC by default; `-Backend gdi` exercises GDI. `-ReuseServer` preserves an existing Server and is incompatible with TestServerRestart and TestUiaTimeout. These are test-runner switches. Transport tests require a running Server and never stop it. Session tests refuse to reuse or destroy another test's child.

Public calls select an exact numeric Windows Session ID, or omit Session for the caller's session. Handle is a hexadecimal HWND; omit it or use zero for the desktop. Window coordinates and screenshots use the full window, including its non-client area. UIA actions use Handle and the native RuntimeId from Inspect. Tests cover activation, minimize/maximize, obstruction, UIA/focus/caret and standard listbox selection, mouse buttons and double-click messages, requested and failed OutFile, Message/Paste text, OLE clipboard restoration and WM_CHAR/WM_UNICHAR. Error tests verify common HRESULT output and Inspect guidance after uncertain delivery. Fixture counters detect unintended replay, including a cancelled RPC call waiting for the input gate. TestUiaTimeout stalls a fixture provider, verifies bounded waiting and native captures from the same Worker, then checks real mouse input received by another fixture; it does not claim to cancel the provider call.

Session tests run synchronous CreateChild in hidden caller processes so the runner can inspect pending login and the parent's responsiveness. They cover same-child reuse, numeric routing, actual preview visibility, bounded Worker recovery without replacing the authenticated logon and destruction. Add `-ConfigureChildSessions` only inside a disposable Hyper-V guest restored from a checkpoint afterward. This calls the optional EnableChildSession command and changes guest settings; it does not save passwords or guarantee removal of the Windows credential prompt. Without that switch configuration is left untouched.

`CuaTestClient.ps1` calls the private TestRpc entry with raw UTF-8 request files and supplies independent Windows oracles. Transport tests check JSON/size validation, rejected removed/unknown fields, Worker identity, unavailable sessions and caller authorization over local Windows RPC. TestRpc does not start a Server. RPC runtime framing is not a custom harness protocol. Private requests contain method and params only; all replies use the same success/error structure as other WUA tools.

DPI tests compare caret coordinates against an oracle in the target's UI thread and independently measure frame origins with Win32/DWM. They cover unaware, system-aware and PerMonitorV2 windows, including mirrored controls. `-RequireScaledDesktop` requires scaling above 100%; omit it for a baseline run. Preserve evidence outside the source tree, normally under Source/OutDir.
