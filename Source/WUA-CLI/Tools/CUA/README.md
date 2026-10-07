# CUA tools

Runtime support and validation target Windows 11. Older Windows releases are not verified.

The [agent guide](../../AGENTS.md) describes the workflow. See [command lines and JSON examples](Examples.md). Use `WUA-CLI.exe CUA <Command> [Name=value]`; a leading hyphen is optional. Command, parameter and Keys key names are case-insensitive. Quote an entire argument containing spaces.

## Parameters and results

| Parameter | Meaning |
| --- | --- |
| `Session` | Omit for the caller's Windows session, or use a numeric Windows session ID from List/CreateChild. |
| `Handle` | Hexadecimal window handle from Inspect's `window_handle`. Omit or use 0 for the desktop where supported. |
| `OutFile` | Request a PNG at this path. Relative paths resolve in the caller's directory. Without it, return information without taking a screenshot. |

Success returns `{ "ok": true, "result": ... }`, with null when the command has no result. Failure reuses the other tools' format: `ok=false`, `hresult`, `hresult_text` and `details`, with a nonzero exit code. `ok` reports API completion; check the application's resulting state in the returned inspection.

Input and window operations return fresh inspection information. If their main API succeeds but subsequent observation, screenshot or clipboard restoration fails, `ok` stays true and `result.observation_error`, `image_error` or `clipboard_error` describes the supplemental failure using the same error format. The operation is not undone. On input failure or a lost response, inspect before continuing; some input may already have occurred.

## Inspection and windows

| Command | Parameters |
| --- | --- |
| `CUA Inspect` | `[Handle=<hex>] [Session] [OutFile]` |
| `CUA Window` | `Action=Activate|Minimize|Maximize|Close Handle=<hex> [Session] [OutFile]` |

Desktop inspection observes the virtual desktop, lists top-level windows and inspects foreground UIA. Window inspection includes owned dialogs, UIA and processed input context. All window observations include the nonclient area. Minimize and Close return desktop context; Close may display the application's confirmation dialog.

All returned coordinates start at the screenshot's upper-left corner. Desktop coordinates start at the virtual desktop's upper-left corner, even when monitors have negative Windows coordinates. Window coordinates start at the full window's upper-left corner. UIA `bounds_image_px` and `point_image_px` already use that coordinate system. Copy the point directly into Mouse with the same Handle and Session; do not convert screen, client or DPI coordinates. Missing or inconsistent geometry produces no pointer point.

Covered window content can still be observed. Window input restores and raises the target, then verifies hit testing. A visible Click, DoubleClick or Down can activate it through normal Windows mouse handling even when Windows denies programmatic foreground activation. The application may consume that first click; inspect rather than automatically clicking again. An obstruction or modal blocker produces an error before input. Keys, Text and other mouse operations require the actual foreground target. Desktop input uses the current screen destination.

`interaction` interprets GetGUIThreadInfo into focus, capture, menu and move/size information. Its state is current for a consistent foreground sample, background for an observed background window, changed when sampling detects a transition, or unavailable when no usable context was obtained. Background retained focus is omitted. Native menu and move/size loops also omit focus and caret hints; finish or cancel the loop with Keys before Text. ESC may step back one menu level; inspect the new state before continuing. These checks do not guarantee focus remains unchanged until the next call. UIA failure/truncation is reported separately. `caret_hint` is a position hint, not an input target.

```powershell
WUA-CLI.exe CUA Inspect OutFile=desktop.png
WUA-CLI.exe CUA Inspect Handle=<hex> OutFile=window.png
WUA-CLI.exe CUA Window Action=Activate Handle=<hex>
WUA-CLI.exe CUA Window Action=Minimize Handle=<hex>
WUA-CLI.exe CUA Window Action=Maximize Handle=<hex>
WUA-CLI.exe CUA Window Action=Close Handle=<hex>
```

## Input

| Command | Parameters |
| --- | --- |
| `CUA Mouse` | `Operation=Click|DoubleClick|Move|Wheel|Drag|Down|Up X=<n> Y=<n> [Handle]` |
| `CUA Text` | `Text=<literal text> [Method=Message|Paste] [Handle]` |
| `CUA Keys` | `Keys=CTRL+A` (1-8 unique key names), `[Handle]` |
| `CUA Element` | `Operation=Invoke|SetValue|Toggle|Select Handle=<hex> RuntimeId=<value>`, Text for SetValue |

These commands accept Session and OutFile. Mouse defaults to Button=Left; Click, DoubleClick, Down, Up and Drag also accept Right, Middle, X1 or X2. Wheel uses `Delta=120 Axis=Vertical|Horizontal`; positive is up/right. Drag requires ToX/ToY in the same screenshot coordinate system and optionally DurationMs. Prefer Drag for a complete gesture; Down/Move/Up remain available for scripts.

Text and Keys use current focus. With Handle, the harness activates the window and verifies that the focused control belongs to it. Click the intended edit control first.

Message is Text's default: probe WM_UNICHAR support, otherwise use WM_CHAR for a Unicode control. It leaves the clipboard untouched. Paste retains the original OLE object, supplies temporary Unicode text and sends Ctrl+V. It restores the object on a best-effort basis if clipboard ownership is unchanged; restoration does not guarantee that every original format remains readable. A new clipboard owner is left intact.

For an Element action, copy `runtime_id` unchanged from Inspect: it is a comma-separated string of native signed integers. Use the same observed window's Handle. Choose an operation supported by the element's `patterns`. SetValue may bypass normal keyboard-driven edit behavior.

```powershell
WUA-CLI.exe CUA Mouse Operation=Click Handle=<hex> X=120 Y=80
WUA-CLI.exe CUA Mouse Operation=DoubleClick Handle=<hex> X=120 Y=80
WUA-CLI.exe CUA Mouse Operation=Move Handle=<hex> X=120 Y=80
WUA-CLI.exe CUA Mouse Operation=Wheel Handle=<hex> X=120 Y=80 Delta=-120
WUA-CLI.exe CUA Mouse Operation=Drag Handle=<hex> X=120 Y=80 ToX=400 ToY=200
WUA-CLI.exe CUA Mouse Operation=Down Handle=<hex> X=120 Y=80
WUA-CLI.exe CUA Mouse Operation=Up Handle=<hex> X=120 Y=80
WUA-CLI.exe CUA Text Handle=<hex> "Text=hello" OutFile=typed.png
WUA-CLI.exe CUA Text Handle=<hex> Method=Paste "Text=hello"
WUA-CLI.exe CUA Keys Handle=<hex> Keys=CTRL+A
WUA-CLI.exe CUA Element Operation=Invoke Handle=<hex> RuntimeId=<runtime-id>
WUA-CLI.exe CUA Element Operation=SetValue Handle=<hex> RuntimeId=<runtime-id> "Text=hello"
WUA-CLI.exe CUA Element Operation=Toggle Handle=<hex> RuntimeId=<runtime-id>
WUA-CLI.exe CUA Element Operation=Select Handle=<hex> RuntimeId=<runtime-id>
```

## Sessions

```powershell
WUA-CLI.exe CUA Session CreateChild Width=1600 Height=900 Show=false
WUA-CLI.exe CUA Session List
WUA-CLI.exe CUA Inspect Session=<id> OutFile=child.png
WUA-CLI.exe CUA Session Preview Session=<id> Show=true
WUA-CLI.exe CUA Session Preview Session=<id> Show=false
WUA-CLI.exe CUA Session Destroy Session=<id>
```

CreateChild defaults to 1920x1080 and a hidden preview. It waits for login, desktop and Worker readiness, then returns `result.id`. When Windows prompts for credentials, guide the user to enter their password in the system window. A retained child with matching resolution is reused; recoverable startup or Worker failures receive bounded internal retries without replacing its login. Show applies only to initial creation; Preview changes visibility without reconnecting or changing resolution. Preview and Destroy require an owned child's numeric ID and return null on success. Closing the preview or Destroy ends the child and its Worker. List reports diagnostic state; dimensions and preview visibility apply to owned children, and details/error fields appear only when present.

Workers start hidden under the same account. Standard accounts operate directly; administrator accounts try UAC elevation and UIAccess, retaining available privileges if either attempt fails. Other Windows session IDs can select an existing same-account Worker; this does not implement arbitrary-session logon or Worker startup.

`CUA Session EnableChildSession` optionally enables Windows child sessions, localhost credential delegation and password login. It reports each setting's applied/changed/error status and tries UAC if an administrator account needs write access. CreateChild never changes these settings automatically. Windows may still show a credential prompt: guide the user to enter their password in that system window. The CLI accepts no password and saves none. See [machine settings](../../../../notice.md).

## Other tools

| Command | Parameters |
| --- | --- |
| `Run Locate` | `Path=<file or directory> [Session]`; open Explorer and select the item |
| `Run Elevate` | `Program=<path> -- <arguments>`; ShellExecute runas |
| `Run Restrict` | `Program=<path> Level=AuthenticatedUsers|Users -- <arguments>` |
| `Text Inspect` | `File=<path>`; size, encoding, BOM and line endings |
| `File Recycle` | `File=<path>`; move to Recycle Bin |

Run Locate shares the CUA session route. Files are on the same machine in all sessions. See [runtime design](Architecture.md) and [tests](../../Tests/README.md) for implementation details.
