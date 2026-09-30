# WUA-CLI Agent Guide

## Run command

```powershell
.\WUA-CLI.exe <Tool> <Command> [Name=value]
```
Use `pwsh.exe` when available; it uses UTF-8 by default.

Tool and command names are case-insensitive.
Use `Name=value` for value parameters, `Name` for `Bool` switches, and `-- <text>` for `RawCommandLine` arguments when a command supports them.
Parameter types: 
- `String`: raw string after `=`.
- `HexU32`: unsigned 32-bit hex integer, used for handles. Pass handles without `0x`.
- `Int32`: signed decimal integer.
- `Bool`: present-only switch.
- `RawCommandLine`: raw text after `--`; all remaining text is passed through unchanged.

Exit code `0` means command execution completed and stdout contains UTF-8 JSON. Always parse it and check `ok`.

If `ok` is `true`, the command succeeded and `result` is command-specific and may be `null`:

```json
{"ok":true,"result":null}
```

If `ok` is `false`, the command failed; `hresult` is the Windows `HRESULT`, `hresult_text` is its text, and `details` gives command-specific context:

```json
{
  "ok": false,
  "hresult": -2147024809,
  "hresult_text": "The parameter is incorrect.",
  "details": "Parameter \"OutFile\" is required."
}
```

If the exit code is nonzero, the CLI failed before producing reliable output.

## Commands

### `Window Snapshot`

Captures the full virtual desktop when `OutFile` is set, and returns desktop state.

Parameters:

- `OutFile` (`String`, optional): PNG output path.

Result:

- `virtual_screen`: full virtual desktop rectangle in Windows screen coordinates: `left`, `top`, `right`, `bottom`.
- `active_window`: foreground top-level window state, or `null` if unavaliable.
- `active_window.handle`: foreground top-level window handle.
- `active_window.focus_handle`: focused window handle.
- `active_window.caret_handle`: caret window handle.
- `active_window.caret_rectangle`: caret rectangle in screen coordinates when available.
- `active_window.uia_tree`: UIA tree for the foreground top-level window.
- `windows[]`: visible top-level windows available for interaction.
- `windows[]` fields: `handle`, `title`, `class`, `minimized`, `pid`, and when available `process_path`, `process_product`.

Example:

```powershell
.\WUA-CLI.exe Window Snapshot OutFile=C:\Temp\screen.png
```

### `Window Inspect`

Inspects one top-level window and optionally captures its client area.

Parameters:

- `Handle` (`HexU32`, required by behavior): top-level window handle.
- `OutFile` (`String`, optional): PNG output path.

Result:

- `active_window`: inspected window state when that window is foreground, or `null`.
- `active_window` fields: `handle`, `focus_handle`, `caret_handle`, and when available `caret_rectangle` in inspected-window coordinates.
- `uia_tree`: UIA tree for the inspected window.
- UIA element fields may include `window_handle`, `name`, `role`, `text`, bounds (`left`, `top`, `right`, `bottom`), `clickable_point`, `enabled`, `offscreen`, `focused`, and recursive `children`.

Example:

```powershell
.\WUA-CLI.exe Window Inspect Handle=00123456 OutFile=C:\Temp\window.png
```

### `Window Operation`

Applies an operation to a window.

Parameters:

- `Handle` (`HexU32`, required): window handle.
- `Action` (`String`, required): `Activate`, `Minimize`, or `Maximize`.

Result: `null`

Example:

```powershell
.\WUA-CLI.exe Window Operation Handle=00123456 Action=Activate
```

### `Input Mouse`

Sends mouse input. **Two coordinate modes** — choose based on your coordinate source:

#### Mode 1: Virtual-Screen Coordinates (no Handle)

Use this when clicking from a **full desktop screenshot** or when you have **UIA `clickable_point` / bounds** (they are already in screen/virtual-screen coordinates).

```powershell
.\WUA-CLI.exe Input Mouse X=<ScreenX> Y=<ScreenY> Button=Left Operation=Click
```

- `X`, `Y`: **Virtual-screen coordinates** (absolute, same coordinate space as `virtual_screen` from `Window Snapshot`).
- No `Handle` parameter.
- CLI sends input as-is; no coordinate transformation.

**Conversion from screenshot image coordinates:**

```text
ScreenX = ImageX + virtual_screen.left
ScreenY = ImageY + virtual_screen.top
```

**UIA `clickable_point` is already in this coordinate space — pass directly, no conversion needed.**

Example — click a UIA element's clickable point on the desktop:

```powershell
# UIA returned clickable_point = { x: 1092, y: 1196 }
# This is already a screen coordinate -> use Mode 1 (no Handle)
.\WUA-CLI.exe Input Mouse X=1092 Y=1196 Button=Left Operation=Click
```

#### Mode 2: Client-Area Relative Coordinates (with Handle)

Use this when your coordinates are relative to the target window's client-area origin (0,0). Both coordinate modes can operate applications without UIA; choose the mode that matches the source of your coordinates.

```powershell
.\WUA-CLI.exe Input Mouse Handle=<hex> X=<ClientX> Y=<ClientY> Button=Left Operation=Click
```

- `Handle` (`HexU32`, required): target window handle.
- `X`, `Y`: **Client-area relative coordinates** — (0,0) is the top-left corner of the window's **client area** (inside the title bar/border). CLI internally calls `ClientToScreen()` to convert to screen coordinates before sending input.
- CLI also activates/foregrounds the handle's window before sending input.

**Do not subtract UIA window bounds to obtain client coordinates.** The UIA root rectangle can include the title bar and borders, so its origin is not necessarily the client-area origin. The window list does not expose a client-area origin.

Use `ScreenToClient()` for an actual screen-to-client conversion, or subtract the screen position returned by `ClientToScreen(hwnd, {0,0})`. If you only have a screen point and need to activate the target, use `Window Operation Action=Activate`, obtain a fresh observation, and send the resulting screen point without `Handle`.

Example using coordinates measured from the client-area screenshot returned by `Window Inspect`:

```powershell
.\WUA-CLI.exe Input Mouse Handle=F0844 X=120 Y=80 Button=Left Operation=Click
```

#### Shared Parameters

- `Button` (`String`): required for `Click`, `DoubleClick`, `Down`, and `Up`; one of `Left`, `Right`, `Middle`, `X1`, or `X2`. Omit for `Move` and `Wheel`.
- `Operation` (`String`, required): `Click`, `DoubleClick`, `Down`, `Up`, `Move`, or `Wheel`.

- `Delta` (`Int32`, optional for `Wheel`): signed wheel delta, default `120`; must not be zero. `120` is one wheel detent. Positive values scroll up for vertical input and right for horizontal input.
- `Axis` (`String`, optional for `Wheel`): `Vertical` (default) or `Horizontal`.

```powershell
.\WUA-CLI.exe Input Mouse X=640 Y=360 Operation=Move
.\WUA-CLI.exe Input Mouse X=640 Y=360 Operation=Wheel Delta=-120
.\WUA-CLI.exe Input Mouse X=640 Y=360 Operation=Wheel Axis=Horizontal Delta=120
```

Result: `null` for all operations.

#### Which mode should I use?

| Scenario | Mode | Why |
|----------|------|-----|
| Clicking from full desktop screenshot | **Mode 1 (no Handle)** | Screenshot pixels map directly to screen coords |
| Using UIA `clickable_point` / `bounds` | **Mode 1 (no Handle)** | UIA already returns screen coords |
| Operating a non-UIA window by pixel offset within it | **Mode 2 (with Handle)** | You measure positions relative to the window itself |
| Target window needs activation and the point is a screen coordinate | Activate, observe again, then **Mode 1** | Preserves the screen-coordinate contract |

**Common mistake:** Passing UIA `clickable_point` (screen coordinates) together with `Handle`. This causes a double-offset because CLI applies `ClientToScreen()` to an already-absolute coordinate, sending the mouse to the wrong location (often the taskbar). **If you have a screen coordinate, use Mode 1. Activate separately and observe again when needed.**

### `Input Text`

Pastes text into the focused target by temporarily replacing the clipboard and sending `Ctrl+V`.

Parameters:

- `Text` (`String`, required): text to paste.

Result: `null`

Example:

```powershell
.\WUA-CLI.exe Input Text Text="hello world"
```

### `Run Locate`

Opens File Explorer and selects a file or directory.

Parameters:

- `Path` (`String`, required): item path.

Result: `null`

Example:

```powershell
.\WUA-CLI.exe Run Locate Path=C:\Temp\file.txt
```

### `Run Elevate`

Starts a program through ShellExecute `runas`.

Parameters:

- `Program` (`String`, required): program path.
- `Arguments` (`RawCommandLine`, optional): passed to `Program`.

Result: `null`

Example:

```powershell
.\WUA-CLI.exe Run Elevate Program=C:\Windows\System32\cmd.exe -- /c whoami /groups
```

### `Run Restrict`

Starts a program with a filtered token.

Parameters:

- `Program` (`String`, required): program path.
- `Level` (`String`, required): `AuthenticatedUsers` or `Users`.
- `Arguments` (`RawCommandLine`, optional): passed to `Program`.

Result: `null`

Example:

```powershell
.\WUA-CLI.exe Run Restrict Program=C:\Windows\System32\cmd.exe Level=Users -- /c whoami /groups
```

### `Text Inspect`

Reports file size, BOM, line-ending counts, and final line ending.

Parameters:

- `File` (`String`, required): file path.

Result:

- `size`: file size in bytes.
- `bom`: detected BOM name when present.
- `crlf`: CRLF line-ending count.
- `lf_only`: standalone LF line count.
- `cr_only`: standalone CR line count.
- `final_line_ending`: final line ending, or `null`.

Example:

```powershell
.\WUA-CLI.exe Text Inspect File=C:\Temp\readme.txt
```

### `File Recycle`

Moves a file or directory to the Windows Recycle Bin.

Parameters:

- `File` (`String`, required): file or directory.

Result: `null`

Example:

```powershell
.\WUA-CLI.exe File Recycle File=C:\Temp\old.txt
```

## CUA Workflow

Use this CLI as a low-level Windows control surface.

1. Find or open the target program from normal user entry points such as existing windows, the taskbar, desktop, or Start menu. Do not install/download software or choose substitute products for the user.
2. Use `Window Snapshot OutFile=<path>` to capture the desktop and inspect `active_window`, `windows`, and `virtual_screen`.
3. Prefer UIA data from `active_window.uia_tree` or `Window Inspect` over image guessing.
4. To click a point chosen from a full desktop screenshot, convert image coordinates to screen coordinates:

```text
ScreenX = ImageX + virtual_screen.left
ScreenY = ImageY + virtual_screen.top
```

5. Use `Input Mouse` **Mode 1** (no Handle) with virtual-screen coordinates when clicking UIA `clickable_point` or screenshot-derived positions.
6. Use `Input Mouse` **Mode 2** (with Handle) only for client-area coordinates, such as points from a `Window Inspect` client-area screenshot. Do not convert UIA screen coordinates by subtracting the UIA root bounds.
7. Use `Window Operation Action=Activate` before text input or when the target window needs focus.
8. Before using `Input Text`, focus the intended control when possible; if focus cannot be confirmed, try input cautiously and verify the result.
9. Repeat snapshot, inspect, and action until the task is complete.
10. When GUI automation is inefficient or unnecessary, use PowerShell, .NET, or platform APIs directly. Store generated files only in writable locations.

Notes:

- `Window Snapshot` captures the full virtual desktop; `Window Inspect` targets one top-level window.
- `SendInput` acts on the current foreground context; use `Window Operation` or `Input Mouse Handle=...` when focus matters.
- UIA `clickable_point` and element bounds are already screen coordinates — use them with **Mode 1 (no Handle)**.
- UIA traversal is capped, so very large trees may be partial.
