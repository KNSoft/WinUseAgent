#pragma once

#define WUA_MAX_MONITORS 64

typedef enum _CAPTURE_BACKEND
{
    CaptureBackendWgc,
    CaptureBackendScreenGdi
} CAPTURE_BACKEND;

typedef struct _CAPTURED_FRAME
{
    RECT ScreenBounds;
    RECT WindowBounds;
    RECT ClientBounds;
    UINT WindowDpi;
    UINT Width;
    UINT Height;
    PBYTE Png;
    ULONG PngLength;
    RECT Monitors[WUA_MAX_MONITORS];
    ULONG MonitorCount;
} CAPTURED_FRAME;

EXTERN_C_START

LOGICAL DesktopCaptureReady(VOID);

HRESULT
MeasureFrame(
    _In_opt_ HWND Window,
    _Out_ CAPTURED_FRAME* Frame);

// PNG pixels map 1:1 to ScreenBounds. Free Frame->Png with Mem_Free.
// ERROR_RETRY means capture geometry changed; no partial image is returned.
HRESULT
CaptureWindow(
    _In_ HWND Window,
    _In_ CAPTURE_BACKEND Backend,
    _In_ DWORD TimeoutMs,
    _Out_ CAPTURED_FRAME* Frame);

HRESULT
CaptureDesktop(
    _In_ CAPTURE_BACKEND Backend,
    _In_ DWORD TimeoutMs,
    _Out_ CAPTURED_FRAME* Frame);

HRESULT
GetWindowCaptureBounds(
    _In_ HWND Window,
    _Out_ RECT* Bounds);

EXTERN_C_END
