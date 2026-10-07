#include "pch.h"
#include "Capture.h"
#include <d3d11.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "windowscodecs.lib")

// The SDK exposes DXGI interop only to C++; use its native ABI, without C++/WinRT.
using namespace ABI::Windows::Graphics;
using namespace ABI::Windows::Graphics::Capture;
using namespace ABI::Windows::Graphics::DirectX;
using namespace ABI::Windows::Graphics::DirectX::Direct3D11;
using ABI::Windows::Foundation::IClosable;
using Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess;

#define MAX_CAPTURE_DIMENSION 16384
#define MAX_CAPTURE_PIXELS (32ull * 1024 * 1024)

typedef struct _CAPTURE_PIXELS
{
    UINT Width, Height;
    PBYTE Bytes;
} CAPTURE_PIXELS;

typedef struct _CAPTURE_MONITOR
{
    HMONITOR Handle;
    RECT Bounds;
} CAPTURE_MONITOR;

typedef struct _CAPTURE_MONITORS
{
    CAPTURE_MONITOR Items[WUA_MAX_MONITORS];
    ULONG Count;
    HRESULT Status;
    RECT Bounds;
} CAPTURE_MONITORS;

typedef struct _CAPTURE_DEVICE
{
    ID3D11Device* Device;
    ID3D11DeviceContext* Context;
    IDirect3DDevice* RuntimeDevice;
} CAPTURE_DEVICE;

static
HRESULT
CheckDeadline(
    _In_ ULONGLONG Deadline)
{
    return _Inline_GetTickCount64() < Deadline ? S_OK : HRESULT_FROM_WIN32(ERROR_TIMEOUT);
}

static
HRESULT
AllocatePixels(
    _In_ UINT Width,
    _In_ UINT Height,
    _Out_ CAPTURE_PIXELS* Pixels)
{
    RtlZeroMemory(Pixels, sizeof(*Pixels));
    if (Width == 0 || Height == 0 || Width > MAX_CAPTURE_DIMENSION || Height > MAX_CAPTURE_DIMENSION ||
        (ULONGLONG)Width * Height > MAX_CAPTURE_PIXELS)
    {
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    }
    Pixels->Bytes = (PBYTE)Mem_Alloc((SIZE_T)Width * Height * 4);
    if (Pixels->Bytes == NULL)
    {
        return E_OUTOFMEMORY;
    }
    RtlZeroMemory(Pixels->Bytes, (SIZE_T)Width * Height * 4);
    Pixels->Width = Width;
    Pixels->Height = Height;
    return S_OK;
}

static
HRESULT
Dimensions(
    _In_ const RECT* Bounds,
    _Out_ UINT* Width,
    _Out_ UINT* Height)
{
    LONGLONG W = (LONGLONG)Bounds->right - Bounds->left;
    LONGLONG H = (LONGLONG)Bounds->bottom - Bounds->top;
    *Width = *Height = 0;
    if (W <= 0 || H <= 0 || W > MAXLONG || H > MAXLONG)
    {
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    }
    *Width = (UINT)W;
    *Height = (UINT)H;
    return S_OK;
}

static
BOOL
CALLBACK
EnumMonitor(
    _In_ HMONITOR Handle,
    _In_ HDC Dc,
    _In_ LPRECT Bounds,
    _In_ LPARAM Parameter)
{
    CAPTURE_MONITORS* Monitors = (CAPTURE_MONITORS*)Parameter;
    UNREFERENCED_PARAMETER(Dc);
    if (Monitors->Count == WUA_MAX_MONITORS)
    {
        Monitors->Status = HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }
    ULONG Index = Monitors->Count++;
    while (Index != 0 && (Monitors->Items[Index - 1].Bounds.left > Bounds->left ||
                     (Monitors->Items[Index - 1].Bounds.left == Bounds->left &&
                      Monitors->Items[Index - 1].Bounds.top > Bounds->top)))
    {
        Monitors->Items[Index] = Monitors->Items[Index - 1];
        --Index;
    }
    Monitors->Items[Index].Handle = Handle;
    Monitors->Items[Index].Bounds = *Bounds;
    UnionRect(&Monitors->Bounds, &Monitors->Bounds, Bounds);
    return TRUE;
}

static
HRESULT
GetMonitors(
    _Out_ CAPTURE_MONITORS* Monitors)
{
    RtlZeroMemory(Monitors, sizeof(*Monitors));
    if (!EnumDisplayMonitors(NULL, NULL, EnumMonitor, (LPARAM)Monitors))
    {
        return FAILED(Monitors->Status) ? Monitors->Status : HRESULT_FROM_WIN32(Err_GetLastError());
    }
    return Monitors->Count != 0 ? S_OK : HRESULT_FROM_WIN32(ERROR_NOT_READY);
}

static
HRESULT
VerifyMonitors(
    _In_ const CAPTURE_MONITORS* Before)
{
    CAPTURE_MONITORS After;
    HRESULT Hr = GetMonitors(&After);
    if (FAILED(Hr))
    {
        return Hr;
    }
    if (Before->Count != After.Count)
    {
        return HRESULT_FROM_WIN32(ERROR_RETRY);
    }
    for (ULONG Index = 0; Index < Before->Count; ++Index)
    {
        if (Before->Items[Index].Handle != After.Items[Index].Handle ||
            !EqualRect(&Before->Items[Index].Bounds, &After.Items[Index].Bounds))
        {
            return HRESULT_FROM_WIN32(ERROR_RETRY);
        }
    }
    return S_OK;
}

static
HRESULT
WindowBounds(
    _In_ HWND Window,
    _In_ LOGICAL ClientOnly,
    _In_ LOGICAL AllowEmpty,
    _Out_ RECT* Bounds)
{
    RtlZeroMemory(Bounds, sizeof(*Bounds));
    if (!IsWindow(Window))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    }
    if (IsIconic(Window))
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    }
    if (ClientOnly != FALSE)
    {
        if (!GetClientRect(Window, Bounds))
        {
            return HRESULT_FROM_WIN32(Err_GetLastError());
        }
        POINT First = { Bounds->left, Bounds->top }, Last = { Bounds->right, Bounds->bottom };
        if (!ClientToScreen(Window, &First) || !ClientToScreen(Window, &Last))
        {
            return HRESULT_FROM_WIN32(Err_GetLastError());
        }
        SetRect(Bounds, min(First.x, Last.x), min(First.y, Last.y), max(First.x, Last.x), max(First.y, Last.y));
    } else
    {
        HRESULT Hr = DwmGetWindowAttribute(Window, DWMWA_EXTENDED_FRAME_BOUNDS, Bounds, sizeof(*Bounds));
        if (FAILED(Hr))
        {
            return Hr;
        }
    }
    return AllowEmpty == FALSE && IsRectEmpty(Bounds) != FALSE ? HRESULT_FROM_WIN32(ERROR_NOT_READY) : S_OK;
}

static
VOID
CloseRuntimeObject(
    _In_opt_ IUnknown* Object)
{
    IClosable* Closable = NULL;
    if (Object != NULL && SUCCEEDED(Object->QueryInterface(IID_PPV_ARGS(&Closable))))
    {
        Closable->Close();
        Closable->Release();
    }
}

static
HRESULT
GetFactory(
    _In_ PCWSTR Class,
    _In_ REFIID InterfaceId,
    _Outptr_ PVOID* Factory)
{
    HSTRING_HEADER Header;
    HSTRING Name;
    *Factory = NULL;
    HRESULT Hr = _Inline_WindowsCreateStringReference(Class, (UINT32)wcslen(Class), &Header, &Name);
    if (FAILED(Hr))
    {
        return Hr;
    }
    return Name != NULL ? RoGetActivationFactory(Name, InterfaceId, Factory) : E_INVALIDARG;
}

static
HRESULT
CreateDevice(
    _Out_ CAPTURE_DEVICE* Device)
{
    IDXGIDevice* Dxgi = NULL;
    IInspectable* Inspectable = NULL;
    RtlZeroMemory(Device, sizeof(*Device));
    HRESULT Hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                                   D3D11_SDK_VERSION, &Device->Device, NULL, &Device->Context);
    if (FAILED(Hr))
    {
        if (Device->Context != NULL)
        {
            Device->Context->Release();
        }
        if (Device->Device != NULL)
        {
            Device->Device->Release();
        }
        Device->Context = NULL;
        Device->Device = NULL;
        Hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                               D3D11_SDK_VERSION, &Device->Device, NULL, &Device->Context);
    }
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Device->Device->QueryInterface(IID_PPV_ARGS(&Dxgi));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = CreateDirect3D11DeviceFromDXGIDevice(Dxgi, &Inspectable);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Inspectable->QueryInterface(IID_PPV_ARGS(&Device->RuntimeDevice));
Exit:
    if (Inspectable != NULL)
    {
        Inspectable->Release();
    }
    if (Dxgi != NULL)
    {
        Dxgi->Release();
    }
    return Hr;
}

static
HRESULT
CaptureItem(
    _In_ const CAPTURE_DEVICE* Device,
    _In_ IGraphicsCaptureItem* Item,
    _In_ ULONGLONG Deadline,
    _Out_ CAPTURE_PIXELS* Pixels)
{
    IDirect3D11CaptureFramePoolStatics2* Factory = NULL;
    IDirect3D11CaptureFramePool* Pool = NULL;
    IGraphicsCaptureSession* Session = NULL;
    IGraphicsCaptureSession2* Session2 = NULL;
    IGraphicsCaptureSession6* Session6 = NULL;
    IDirect3D11CaptureFrame* Frame = NULL;
    IDirect3DSurface* Surface = NULL;
    IDirect3DDxgiInterfaceAccess* Access = NULL;
    ID3D11Texture2D* Source = NULL;
    ID3D11Texture2D* Staging = NULL;
    SizeInt32 Size, ContentSize;
    D3D11_TEXTURE2D_DESC Description;
    D3D11_BOX Box;
    D3D11_MAPPED_SUBRESOURCE Mapped;
    HRESULT Hr;

    RtlZeroMemory(Pixels, sizeof(*Pixels));

    Hr = CheckDeadline(Deadline);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Item->get_Size(&Size);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = AllocatePixels(Size.Width, Size.Height, Pixels);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = GetFactory(RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool, IID_PPV_ARGS(&Factory));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Factory->CreateFreeThreaded(Device->RuntimeDevice, DirectXPixelFormat_B8G8R8A8UIntNormalized, 1, Size, &Pool);
    if (FAILED(Hr))
    {
        goto Exit;
    }

    Hr = Pool->CreateCaptureSession(Item, &Session);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (SUCCEEDED(Session->QueryInterface(IID_PPV_ARGS(&Session2))))
    {
        Hr = Session2->put_IsCursorCaptureEnabled(FALSE);
        if (FAILED(Hr))
        {
            goto Exit;
        }
    }
    if (SUCCEEDED(Session->QueryInterface(IID_PPV_ARGS(&Session6))))
    {
        Hr = Session6->put_IncludeSecondaryWindows(FALSE);
        if (FAILED(Hr))
        {
            goto Exit;
        }
    }

    Hr = Session->StartCapture();
    if (FAILED(Hr))
    {
        goto Exit;
    }

    for (;;)
    {
        Hr = CheckDeadline(Deadline);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = Pool->TryGetNextFrame(&Frame);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        if (Frame != NULL)
        {
            break;
        }
        PS_DelayExec(1);
    }
    Hr = Frame->get_ContentSize(&ContentSize);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (ContentSize.Width != Size.Width || ContentSize.Height != Size.Height)
    {
        Hr = HRESULT_FROM_WIN32(ERROR_RETRY);
        goto Exit;
    }
    Hr = Frame->get_Surface(&Surface);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Surface->QueryInterface(IID_PPV_ARGS(&Access));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Access->GetInterface(IID_PPV_ARGS(&Source));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Source->GetDesc(&Description);
    if (Description.Width < Pixels->Width || Description.Height < Pixels->Height ||
        Description.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
    {
        Hr = HRESULT_FROM_WIN32(ERROR_RETRY);
        goto Exit;
    }
    Description.Width = Pixels->Width;
    Description.Height = Pixels->Height;
    Description.MipLevels = Description.ArraySize = Description.SampleDesc.Count = 1;
    Description.SampleDesc.Quality = 0;
    Description.Usage = D3D11_USAGE_STAGING;
    Description.BindFlags = Description.MiscFlags = 0;
    Description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Hr = Device->Device->CreateTexture2D(&Description, NULL, &Staging);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Box = { 0, 0, 0, Pixels->Width, Pixels->Height, 1 };
    Device->Context->CopySubresourceRegion(Staging, 0, 0, 0, 0, Source, 0, &Box);
    Device->Context->Flush();
    for (;;)
    {
        Hr = CheckDeadline(Deadline);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = Device->Context->Map(Staging, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &Mapped);
        if (Hr != DXGI_ERROR_WAS_STILL_DRAWING)
        {
            break;
        }
        PS_DelayExec(1);
    }
    if (FAILED(Hr))
    {
        goto Exit;
    }
    for (UINT Row = 0; Row < Pixels->Height; ++Row)
    {
        RtlCopyMemory(Pixels->Bytes + (SIZE_T)Row * Pixels->Width * 4,
                      (PBYTE)Mapped.pData + (SIZE_T)Row * Mapped.RowPitch, (SIZE_T)Pixels->Width * 4);
    }
    Device->Context->Unmap(Staging, 0);
Exit:
    if (Staging != NULL)
    {
        Staging->Release();
    }
    if (Source != NULL)
    {
        Source->Release();
    }
    if (Access != NULL)
    {
        Access->Release();
    }
    if (Surface != NULL)
    {
        Surface->Release();
    }
    CloseRuntimeObject(Frame);
    if (Frame != NULL)
    {
        Frame->Release();
    }
    CloseRuntimeObject(Session);
    if (Session6 != NULL)
    {
        Session6->Release();
    }
    if (Session2 != NULL)
    {
        Session2->Release();
    }
    if (Session != NULL)
    {
        Session->Release();
    }
    CloseRuntimeObject(Pool);
    if (Pool != NULL)
    {
        Pool->Release();
    }
    if (Factory != NULL)
    {
        Factory->Release();
    }
    if (FAILED(Hr))
    {
        Mem_Free(Pixels->Bytes);
        RtlZeroMemory(Pixels, sizeof(*Pixels));
    }
    return Hr;
}

static
HRESULT
ScreenPixels(
    _In_ const RECT* Bounds,
    _Out_ CAPTURE_PIXELS* Pixels)
{
    UINT Width, Height;
    HDC Screen = NULL, Memory = NULL;
    HBITMAP Bitmap = NULL;
    HGDIOBJ Previous = NULL;
    PVOID Bits = NULL;
    POINT Origin;
    SIZE Size;
    RECT Desktop, Visible;
    HRESULT Hr;
    RtlZeroMemory(Pixels, sizeof(*Pixels));
    Hr = Dimensions(Bounds, &Width, &Height);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = AllocatePixels(Width, Height, Pixels);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Screen = GetDC(NULL);
    if (Screen == NULL)
    {
        ULONG Error = Err_GetLastError();
        Hr = Error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(Error) : E_FAIL;
        goto Exit;
    }
    Memory = CreateCompatibleDC(Screen);
    if (Memory == NULL)
    {
        ULONG Error = Err_GetLastError();
        Hr = Error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(Error) : E_FAIL;
        goto Exit;
    }
    Bitmap = UI_CreateBitmap((LONG)Width, -(LONG)Height, 32, &Bits);
    if (Bitmap == NULL || Bits == NULL)
    {
        ULONG Error = Err_GetLastError();
        Hr = Error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(Error) : E_FAIL;
        goto Exit;
    }
    Previous = SelectObject(Memory, Bitmap);
    UI_GetScreenPos(&Origin, &Size);
    Desktop = { Origin.x, Origin.y, Origin.x + Size.cx, Origin.y + Size.cy };
    RtlZeroMemory(Bits, (SIZE_T)Width * Height * 4);
    if (Previous == NULL || Previous == HGDI_ERROR ||
        (IntersectRect(&Visible, Bounds, &Desktop) != FALSE &&
         BitBlt(Memory, Visible.left - Bounds->left, Visible.top - Bounds->top,
             Visible.right - Visible.left, Visible.bottom - Visible.top,
             Screen, Visible.left, Visible.top, SRCCOPY | CAPTUREBLT) == FALSE))
    {
        ULONG Error = Err_GetLastError();
        Hr = Error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(Error) : E_FAIL;
        goto Exit;
    }
    GdiFlush();
    RtlCopyMemory(Pixels->Bytes, Bits, (SIZE_T)Width * Height * 4);
    UI_SetBitmapBitsAlpha((RGBQUAD*)Pixels->Bytes, Width * Height, 255);
Exit:
    if (Previous != NULL && Previous != HGDI_ERROR)
    {
        SelectObject(Memory, Previous);
    }
    if (Bitmap != NULL)
    {
        DeleteObject(Bitmap);
    }
    if (Memory != NULL)
    {
        DeleteDC(Memory);
    }
    if (Screen != NULL)
    {
        ReleaseDC(NULL, Screen);
    }
    if (FAILED(Hr))
    {
        Mem_Free(Pixels->Bytes);
        RtlZeroMemory(Pixels, sizeof(*Pixels));
    }
    return Hr;
}

static
HRESULT
EncodePng(
    _In_ const CAPTURE_PIXELS* Pixels,
    _Outptr_ PBYTE* Png,
    _Out_ PULONG Length)
{
    IWICImagingFactory* Factory = NULL;
    IStream* Stream = NULL;
    IWICBitmapEncoder* Encoder = NULL;
    IWICBitmapFrameEncode* Frame = NULL;
    WICPixelFormatGUID Format = GUID_WICPixelFormat32bppBGRA;
    STATSTG Stat;
    LARGE_INTEGER Start = { 0 };
    ULONG Read;
    HRESULT Hr;
    *Png = NULL;
    *Length = 0;
    Hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Factory));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = CreateStreamOnHGlobal(NULL, TRUE, &Stream);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Factory->CreateEncoder(GUID_ContainerFormatPng, NULL, &Encoder);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Encoder->Initialize(Stream, WICBitmapEncoderNoCache);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Encoder->CreateNewFrame(&Frame, NULL);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Frame->Initialize(NULL);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Frame->SetSize(Pixels->Width, Pixels->Height);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Frame->SetPixelFormat(&Format);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Format != GUID_WICPixelFormat32bppBGRA)
    {
        Hr = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
        goto Exit;
    }
    Hr = Frame->WritePixels(Pixels->Height, Pixels->Width * 4, Pixels->Width * Pixels->Height * 4, Pixels->Bytes);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Frame->Commit();
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Encoder->Commit();
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Stream->Stat(&Stat, STATFLAG_NONAME);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Stat.cbSize.QuadPart > MAX_CAPTURE_PIXELS * 4)
    {
        Hr = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        goto Exit;
    }
    *Length = (ULONG)Stat.cbSize.QuadPart;
    *Png = (PBYTE)Mem_Alloc(*Length);
    if (*Png == NULL)
    {
        Hr = E_OUTOFMEMORY;
        goto Exit;
    }
    Hr = Stream->Seek(Start, STREAM_SEEK_SET, NULL);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Stream->Read(*Png, *Length, &Read);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Read != *Length)
    {
        Hr = STG_E_READFAULT;
    }
Exit:
    if (Frame != NULL)
    {
        Frame->Release();
    }
    if (Encoder != NULL)
    {
        Encoder->Release();
    }
    if (Stream != NULL)
    {
        Stream->Release();
    }
    if (Factory != NULL)
    {
        Factory->Release();
    }
    if (FAILED(Hr))
    {
        Mem_Free(*Png);
        *Png = NULL;
        *Length = 0;
    }
    return Hr;
}

LOGICAL
DesktopCaptureReady(VOID)
{
    CAPTURE_MONITORS Monitors;
    IGraphicsCaptureItemInterop* Factory = NULL;
    IGraphicsCaptureItem* Item = NULL;
    HRESULT Hr;
    Hr = GetMonitors(&Monitors);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = GetFactory(RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem, IID_PPV_ARGS(&Factory));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    for (ULONG Index = 0; Index < Monitors.Count; ++Index)
    {
        Hr = Factory->CreateForMonitor(Monitors.Items[Index].Handle, IID_PPV_ARGS(&Item));
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Item->Release();
        Item = NULL;
    }
Exit:
    if (Item != NULL)
    {
        Item->Release();
    }
    if (Factory != NULL)
    {
        Factory->Release();
    }
    return SUCCEEDED(Hr);
}

HRESULT
GetWindowCaptureBounds(
    _In_ HWND Window,
    _Out_ RECT* Bounds)
{
    return WindowBounds(Window, FALSE, FALSE, Bounds);
}

HRESULT
MeasureFrame(
    _In_opt_ HWND Window,
    _Out_ CAPTURED_FRAME* Frame)
{
    CAPTURE_MONITORS Monitors;
    HRESULT Hr;
    RtlZeroMemory(Frame, sizeof(*Frame));

    Hr = GetMonitors(&Monitors);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Frame->ScreenBounds = Monitors.Bounds;
    if (Window != NULL)
    {
        Hr = WindowBounds(Window, FALSE, FALSE, &Frame->WindowBounds);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = WindowBounds(Window, TRUE, TRUE, &Frame->ClientBounds);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Frame->WindowDpi = GetDpiForWindow(Window);
        Frame->ScreenBounds = Frame->WindowBounds;
    }
    Hr = Dimensions(&Frame->ScreenBounds, &Frame->Width, &Frame->Height);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Frame->MonitorCount = Monitors.Count;
    for (ULONG Index = 0; Index < Monitors.Count; ++Index)
    {
        Frame->Monitors[Index] = Monitors.Items[Index].Bounds;
    }
Exit:
    return Hr;
}

static
HRESULT
CaptureFrame(
    _In_opt_ HWND Window,
    _In_ CAPTURE_BACKEND Backend,
    _In_ DWORD TimeoutMs,
    _Out_ CAPTURED_FRAME* Frame)
{
    CAPTURE_MONITORS Monitors;
    CAPTURE_DEVICE Device = { 0 };
    CAPTURE_PIXELS Pixels = { 0 }, Part = { 0 };
    IGraphicsCaptureItemInterop* Factory = NULL;
    IGraphicsCaptureItem* Item = NULL;
    RECT WindowAfter, ClientAfter;
    UINT Width, Height;
    ULONGLONG Deadline = _Inline_GetTickCount64() + TimeoutMs;
    HRESULT Init = RoInitialize(RO_INIT_MULTITHREADED);
    if (Init == RPC_E_CHANGED_MODE)
    {
        Init = RoInitialize(RO_INIT_SINGLETHREADED);
    }
    HRESULT Hr = Init;
    RtlZeroMemory(Frame, sizeof(*Frame));

    if (FAILED(Init))
    {
        return Init;
    }
    if (TimeoutMs == 0 || TimeoutMs > 60000 || (Backend != CaptureBackendWgc && Backend != CaptureBackendScreenGdi))
    {
        Hr = E_INVALIDARG;
        goto Exit;
    }
    Hr = GetMonitors(&Monitors);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = MeasureFrame(Window, Frame);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Monitors.Count != Frame->MonitorCount)
    {
        Hr = HRESULT_FROM_WIN32(ERROR_RETRY);
        goto Exit;
    }
    for (ULONG Index = 0; Index < Monitors.Count; ++Index)
    {
        if (!EqualRect(&Monitors.Items[Index].Bounds, &Frame->Monitors[Index]))
        {
            Hr = HRESULT_FROM_WIN32(ERROR_RETRY);
            goto Exit;
        }
    }
    if (Backend == CaptureBackendScreenGdi)
    {

        Hr = ScreenPixels(&Frame->ScreenBounds, &Pixels);
        if (FAILED(Hr))
        {
            goto Exit;
        }
    } else
    {

        Hr = CreateDevice(&Device);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = GetFactory(RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem, IID_PPV_ARGS(&Factory));
        if (FAILED(Hr))
        {
            goto Exit;
        }
        if (Window != NULL)
        {

            Hr = Factory->CreateForWindow(Window, IID_PPV_ARGS(&Item));
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Hr = CaptureItem(&Device, Item, Deadline, &Pixels);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Hr = Dimensions(&Frame->WindowBounds, &Width, &Height);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            if (Pixels.Width != Width || Pixels.Height != Height)
            {
                Hr = HRESULT_FROM_WIN32(ERROR_RETRY);
                goto Exit;
            }
        } else
        {
            Hr = AllocatePixels(Frame->Width, Frame->Height, &Pixels);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            for (SIZE_T Index = 3; Index < (SIZE_T)Pixels.Width * Pixels.Height * 4; Index += 4)
            {
                Pixels.Bytes[Index] = 255;
            }
            for (ULONG Index = 0; Index < Monitors.Count; ++Index)
            {

                Hr = Factory->CreateForMonitor(Monitors.Items[Index].Handle, IID_PPV_ARGS(&Item));
                if (FAILED(Hr))
                {
                    goto Exit;
                }
                Hr = CaptureItem(&Device, Item, Deadline, &Part);
                if (FAILED(Hr))
                {
                    goto Exit;
                }
                Hr = Dimensions(&Monitors.Items[Index].Bounds, &Width, &Height);
                if (FAILED(Hr))
                {
                    goto Exit;
                }
                if (Part.Width != Width || Part.Height != Height)
                {
                    Hr = HRESULT_FROM_WIN32(ERROR_RETRY);
                    goto Exit;
                }
                UINT X = Monitors.Items[Index].Bounds.left - Frame->ScreenBounds.left;
                UINT Y = Monitors.Items[Index].Bounds.top - Frame->ScreenBounds.top;
                for (UINT Row = 0; Row < Part.Height; ++Row)
                {
                    RtlCopyMemory(Pixels.Bytes + (((SIZE_T)Y + Row) * Pixels.Width + X) * 4,
                                  Part.Bytes + (SIZE_T)Row * Part.Width * 4, (SIZE_T)Part.Width * 4);
                }
                Mem_Free(Part.Bytes);
                Part.Bytes = NULL;
                Item->Release();
                Item = NULL;
            }
        }
    }

    if (Window != NULL)
    {
        Hr = WindowBounds(Window, FALSE, FALSE, &WindowAfter);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = WindowBounds(Window, TRUE, TRUE, &ClientAfter);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        if (!EqualRect(&Frame->WindowBounds, &WindowAfter) || !EqualRect(&Frame->ClientBounds, &ClientAfter) ||
            Frame->WindowDpi != GetDpiForWindow(Window))
        {
            Hr = HRESULT_FROM_WIN32(ERROR_RETRY);
            goto Exit;
        }
    }
    Hr = VerifyMonitors(&Monitors);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = CheckDeadline(Deadline);
    if (FAILED(Hr))
    {
        goto Exit;
    }

    Hr = EncodePng(&Pixels, &Frame->Png, &Frame->PngLength);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = CheckDeadline(Deadline);
Exit:
    Mem_Free(Part.Bytes);
    Mem_Free(Pixels.Bytes);
    if (Item != NULL)
    {
        Item->Release();
    }
    if (Factory != NULL)
    {
        Factory->Release();
    }
    if (Device.RuntimeDevice != NULL)
    {
        Device.RuntimeDevice->Release();
    }
    if (Device.Context != NULL)
    {
        Device.Context->Release();
    }
    if (Device.Device != NULL)
    {
        Device.Device->Release();
    }
    if (SUCCEEDED(Init))
    {
        RoUninitialize();
    }
    if (FAILED(Hr))
    {
        Mem_Free(Frame->Png);
        Frame->Png = NULL;
        Frame->PngLength = 0;
    }
    return Hr;
}

HRESULT
CaptureWindow(
    _In_ HWND Window,
    _In_ CAPTURE_BACKEND Backend,
    _In_ DWORD TimeoutMs,
    _Out_ CAPTURED_FRAME* Frame)
{
    return CaptureFrame(Window, Backend, TimeoutMs, Frame);
}

HRESULT
CaptureDesktop(
    _In_ CAPTURE_BACKEND Backend,
    _In_ DWORD TimeoutMs,
    _Out_ CAPTURED_FRAME* Frame)
{
    return CaptureFrame(NULL, Backend, TimeoutMs, Frame);
}
