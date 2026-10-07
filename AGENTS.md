# AGENTS.md

## Scope
- Applies to the entire repository.

## Quick Start (Read First)
- `README.md`
- Other `AGENTS.md` files in subdirectories

## Hard Rules
- CRITICAL: Follow `.editorconfig`, including SAL annotations conventions.
- CRITICAL: Preserve original file encoding (usually UTF8 or UTF8-BOM) and line-ending style (usually CRLF) when editing files.
- CRITICAL: Keep diffs minimal when you touch Visual Studio project files (*.sln, *.slnx, *.vcxproj, *.props, *.targets, ...).
- Keep diffs minimal; do not refactor unrelated code.
- Use concise, technical comments only when needed.

## Rules
- Some files are auto-generated and usually end with `.g.*` (for example, `I18N.xml.g.c` and `I18N.xml.g.h`); do not modify them manually.
- The output directory is usually named `OutDir` and is located next to the solution file; the exact path depends on `.props` files and project settings.

## Tool
- You can use Visual Studio and the Windows SDK when needed.

## Build

- Use `msbuild` to build the entire solution (`*.sln`, `*.slnx`).

## CUA implementation
- Keep the public harness minimal: every command and parameter must have a concrete workflow need. Keep transport and capture implementation details private.
- Read existing commands and the installed MLE/NDK implementations before adding code. Reuse Main's parameter parser and MLE JSON directly; add business helpers only when they remove meaningful duplication.
- Prefer MLE functions, then NDK NT/Rtl APIs. Use Win32/COM APIs when required by their UI interfaces. If an MLE wrapper cannot meet the requirement, ask the user whether to extend MLE before replacing it.
- Write C in .c files. Use C++ only where SDK ABI/imports require it; avoid STL and C++/WinRT.
- Match the original C/MLE style: SAL annotations, separate function declaration lines, braces for control flow, one statement per line, and explicit comparisons for numeric/string-comparison results and pointers.
- Borrow the current image path from the PEB. Do not add fixed-size path allocations or wrappers around existing error/IO helpers.
- Before handing back changes, review public API necessity, existing-code reuse, low-level API choice, lifetimes/error paths, and source style separately from compiler validation.
