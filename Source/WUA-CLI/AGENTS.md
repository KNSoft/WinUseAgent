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
- `Verb` (`String`, required): `Active`, `Minimize`, or `Maximize`.

Result: `null`

Example:

```powershell
.\WUA-CLI.exe Window Operation Handle=00123456 Verb=Active
```

### `Input Mouse`

Sends mouse input.

Parameters:

- `Handle` (`HexU32`, optional): when present, `X` and `Y` are client coordinates for that window, or screen coordinates if absent.
- `X` (`Int32`, required), `Y` (`Int32`, required): coordinates.
- `Button` (`String`, required): `Left`, `Right`, `Middle`, `X1`, or `X2`.
- `Operation` (`String`, required): `Click`, `DoubleClick`, `Down`, or `Up`.
- Without `Handle`, `X` and `Y` are virtual-screen coordinates.

Result: `null`

Examples:

```powershell
.\WUA-CLI.exe Input Mouse X=640 Y=360 Button=Left Operation=Click
.\WUA-CLI.exe Input Mouse Handle=00123456 X=120 Y=80 Button=Left Operation=DoubleClick
```

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
- `lf_only`: standalone LF line-ending count.
- `cr_only`: standalone CR line-ending count.
- `final_line_ending`: final line ending, or `null`.

Example:

```powershell
.\WUA-CLI.exe Text Inspect File=C:\Temp\readme.txt
```

### `File Recycle`

Moves a file or directory to the Windows Recycle Bin.

Parameters:

- `File` (`String`, required): file or directory path.

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

5. Use `Input Mouse X=<ScreenX> Y=<ScreenY> ...` for virtual-screen coordinates.
6. Use `Input Mouse Handle=<handle> X=<clientX> Y=<clientY> ...` only when coordinates are relative to that window client area.
7. Use `Window Operation Verb=Active` before text input or when the target window needs focus.
8. Before using `Input Text`, focus the intended control when possible; if focus cannot be confirmed, try input cautiously and verify the result.
9. Repeat snapshot, inspect, and action until the task is complete.
10. When GUI automation is inefficient or unnecessary, use PowerShell, .NET, or platform APIs directly. Store generated files only in writable locations.

Notes:

- `Window Snapshot` captures the full virtual desktop; `Window Inspect` targets one top-level window.
- `SendInput` acts on the current foreground context; use `Window Operation` or `Input Mouse Handle=...` when focus matters.
- UIA `clickable_point` and element bounds are already screen coordinates.
- UIA traversal is capped, so very large trees may be partial.
