#include "pch.h"
#include "Server.h"
#include "Transport.h"
#include "Worker.h"
#include <rpc.h>
#include <shlwapi.h>

_Post_satisfies_(return < 0)
HRESULT
CuaFail(
    _Inout_ CUA_COMMAND* Command,
    _In_ HRESULT Status,
    _In_ PCWSTR Message)
{
    Command->Error.Status = FAILED(Status) ? Status : E_FAIL;
    Str_CopyExW(Command->Error.Message, ARRAYSIZE(Command->Error.Message), Message);
    return Command->Error.Status;
}

HRESULT
CuaInteger(
    _Inout_ CUA_COMMAND* Command,
    _In_ CUA_PARAMETER Parameter,
    _In_ LONG Default,
    _In_ LONG Minimum,
    _In_ LONG Maximum,
    _Out_ PLONG Value)
{
    *Value = CuaNumber(&Command->Request->Parameters, Parameter, Default);
    if (*Value >= Minimum && *Value <= Maximum)
    {
        return S_OK;
    }
    WCHAR Message[256];
    Str_PrintfExW(Message, ARRAYSIZE(Message), L"%ls must be an integer in %ld..%ld.",
                  CuaParameterInfo[Parameter].CliName, Minimum, Maximum);
    return CuaFail(Command, E_INVALIDARG, Message);
}

LOGICAL
CuaSameWindow(
    _In_ const CUA_WINDOW* Window)
{
    DWORD Pid = 0;
    DWORD Tid = GetWindowThreadProcessId(Window->Handle, &Pid);
    return IsWindow(Window->Handle) && Pid == Window->Process && Tid == Window->Thread;
}

HRESULT
CuaClientBounds(
    _In_ HWND Window,
    _Out_ RECT* Bounds)
{
    POINT First, Last;
    RtlZeroMemory(Bounds, sizeof(*Bounds));
    if (!GetClientRect(Window, Bounds))
    {
        return HRESULT_FROM_WIN32(Err_GetLastError());
    }
    First.x = Bounds->left;
    First.y = Bounds->top;
    Last.x = Bounds->right;
    Last.y = Bounds->bottom;
    if (!ClientToScreen(Window, &First) || !ClientToScreen(Window, &Last))
    {
        return HRESULT_FROM_WIN32(Err_GetLastError());
    }
    Bounds->left = min(First.x, Last.x);
    Bounds->top = min(First.y, Last.y);
    Bounds->right = max(First.x, Last.x);
    Bounds->bottom = max(First.y, Last.y);
    return S_OK;
}

static
HRESULT
CALLBACK
SessionProbe(
    _In_ PVOID Context,
    _In_ DWORD Id,
    _In_ DWORD Timeout,
    _Out_writes_(CUA_GUID_CCH) PWSTR Instance,
    _Out_opt_ PDWORD Process)
{
    CUA_SERVER* Server = (CUA_SERVER*)Context;
    if (Id != Server->SessionId)
    {
        return CuaProbeServer(Id, Timeout, Instance, Process);
    }
    Str_CopyExW(Instance, CUA_GUID_CCH, Server->Instance);
    if (Process != NULL)
    {
        *Process = HandleToULong(NtCurrentProcessId());
    }
    return S_OK;
}

HRESULT
CuaServerCreate(
    _Outptr_ CUA_SERVER** Server)
{
    HRESULT Hr;
    CUA_SERVER* Item = (CUA_SERVER*)Mem_Alloc(sizeof(*Item));
    *Server = NULL;
    if (Item == NULL)
    {
        return E_OUTOFMEMORY;
    }
    RtlZeroMemory(Item, sizeof(*Item));
    Hr = CuaNewId(Item->Instance);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Item->SessionId = NtCurrentPeb()->SessionId;
    Hr = CuaSessionsCreate(SessionProbe, Item, &Item->Sessions);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Err_NtStatusToHr(NtCreateMutant(&Item->Gate, MUTANT_ALL_ACCESS, NULL, FALSE));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = UiaCreate(&Item->Uia);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    *Server = Item;
    Item = NULL;
Exit:
    CuaServerDestroy(Item);
    return Hr;
}

VOID
CuaServerDestroy(
    _In_opt_ CUA_SERVER* Server)
{
    if (Server == NULL)
    {
        return;
    }
    const DWORD Flags[] = { MOUSEEVENTF_LEFTUP, MOUSEEVENTF_RIGHTUP, MOUSEEVENTF_MIDDLEUP, MOUSEEVENTF_XUP,
                            MOUSEEVENTF_XUP };
    for (ULONG Index = 0; Index < ARRAYSIZE(Flags); ++Index)
    {
        if ((Server->HeldButtons & (1u << Index)) != 0)
        {
            INPUT Input = { 0 };
            Input.type = INPUT_MOUSE;
            Input.mi.dwFlags = Flags[Index];
            if (Index >= 3)
            {
                Input.mi.mouseData = Index == 3 ? XBUTTON1 : XBUTTON2;
            }
            NtUserSendInput(1, &Input, sizeof(Input));
        }
    }
    if (Server->Uia != NULL)
    {
        UiaDestroy(Server->Uia);
    }
    CuaSessionsDestroy(Server->Sessions);
    if (Server->Gate != NULL)
    {
        NtClose(Server->Gate);
    }
    Mem_Free(Server);
}

HRESULT
CuaQueryWindow(
    _In_ HWND Handle,
    _Out_ CUA_WINDOW* Window)
{
    RtlZeroMemory(Window, sizeof(*Window));
    Window->Handle = Handle;
    Window->Thread = GetWindowThreadProcessId(Handle, &Window->Process);
    return Window->Thread != 0 && IsWindow(Handle) ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
}

HRESULT
CuaTargetWindow(
    _Inout_ CUA_COMMAND* Command,
    _Out_ CUA_WINDOW* Window)
{
    ULONG Value;
    PCWSTR Text = CuaString(&Command->Request->Parameters, CuaParamHandle, L"0");
    RtlZeroMemory(Window, sizeof(*Window));
    if (Str_HexToUIntW(Text, &Value) == FALSE)
    {
        return CuaFail(Command, E_INVALIDARG, L"Handle must be a hexadecimal window handle; omit it for the desktop.");
    }
    if (Value == 0)
    {
        return S_OK;
    }
    HWND Handle = (HWND)UI_32ToHandle(Value);
    if (IsTopLevelWindow(Handle) == FALSE)
    {
        return CuaFail(Command, E_INVALIDARG, L"Handle must identify a top-level window in the selected session.");
    }
    return CuaQueryWindow(Handle, Window);
}

HRESULT
CuaSetWindowHandle(
    _In_ IJsonValueStatics* Factory,
    _In_ IJsonObject* Object,
    _In_ PCWSTR Name,
    _In_opt_ HWND Handle)
{
    WCHAR Text[9];
    Str_PrintfW(Text, L"%08lX", HandleToULong(Handle));
    return Data_JsonObjectSetString(Factory, Object, Name, Text, 8);
}

HRESULT
CuaOutputPath(
    _Inout_ CUA_COMMAND* Command,
    _Outptr_ PCWSTR* Path)
{
    CUA_PARAMETERS* Parameters = &Command->Request->Parameters;
    *Path = CuaString(Parameters, CuaParamOutFile, L"");
    RTL_PATH_TYPE Type = RtlDetermineDosPathNameType_U(*Path);
    if (CuaHas(Parameters, CuaParamOutFile) &&
        (Type != RtlPathTypeDriveAbsolute && Type != RtlPathTypeUncAbsolute && Type != RtlPathTypeLocalDevice))
    {
        return CuaFail(Command, E_INVALIDARG, L"OutFile must name an absolute output file.");
    }
    return S_OK;
}

HRESULT
CuaInputDesktop(
    _Inout_ CUA_COMMAND* Command)
{
    HDESK Desktop = NtUserOpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    WCHAR Actual[256] = { 0 }, Current[256] = { 0 };
    ULONG Needed = 0;
    if (Desktop == NULL)
    {
        return CuaFail(Command, HRESULT_FROM_WIN32(Err_GetLastError()), L"The input desktop is unavailable.");
    }
    LOGICAL Valid =
        NtUserGetObjectInformation(Desktop, UOI_NAME, Actual, sizeof(Actual) - sizeof(WCHAR), &Needed) != FALSE &&
        NtUserGetObjectInformation(NtUserGetThreadDesktop(HandleToULong(NtCurrentThreadId())), UOI_NAME,
            Current, sizeof(Current) - sizeof(WCHAR), &Needed) != FALSE &&
        _wcsicmp(Actual, Current) == 0;
    NtUserCloseDesktop(Desktop);
    return Valid ? S_OK
                 : CuaFail(Command, E_FAIL, L"The Server is not on the active input desktop.");
}

HRESULT
CuaPrepareWindow(
    _Inout_ CUA_COMMAND* Command,
    _In_ const CUA_WINDOW* Window,
    _In_ LOGICAL RequireForeground)
{
    if (!CuaSameWindow(Window))
    {
        return CuaFail(Command, E_FAIL, L"The target no longer exists.");
    }
    if (!IsWindowEnabled(Window->Handle))
    {
        return CuaFail(Command, E_FAIL,
                       L"The target is disabled, usually by a modal dialog; inspect its related windows.");
    }
    if (IsIconic(Window->Handle))
    {
        if (!NtUserShowWindowAsync(Window->Handle, SW_RESTORE))
        {
            DWORD Error = Err_GetLastError();
            return Error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(Error) : E_FAIL;
        }
    }
    NtUserSetWindowPos(Window->Handle, HWND_TOP, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    if (NtUserGetForegroundWindow() != Window->Handle)
    {
        NtUserSetForegroundWindow(Window->Handle);
    }
    ULONGLONG Deadline = _Inline_GetTickCount64() + 1000;
    while ((IsIconic(Window->Handle) || (RequireForeground != FALSE &&
            NtUserGetForegroundWindow() != Window->Handle)) &&
           _Inline_GetTickCount64() < Deadline)
    {
        PS_DelayExec(15);
    }
    if (CuaSameWindow(Window) == FALSE || IsWindowEnabled(Window->Handle) == FALSE)
    {
        return CuaFail(Command, E_FAIL, L"The target changed or became disabled; no input was sent.");
    }
    return IsIconic(Window->Handle) == FALSE &&
           (RequireForeground == FALSE || NtUserGetForegroundWindow() == Window->Handle)
               ? S_OK
               : CuaFail(Command, E_FAIL, L"Windows did not make the target foreground; no input was sent.");
}

LOGICAL
CuaHitTarget(
    _In_ HWND Window,
    _In_ POINT Point)
{
    HWND Hit = NtUserWindowFromPoint(Point);
    return Hit != NULL && NtUserGetAncestor(Hit, GA_ROOT) == Window;
}

LOGICAL
CuaElementPoint(
    _In_ const UIA_ELEMENT* Element,
    _In_ const RECT* FrameBounds,
    _In_reads_(Count) const RECT* Displays,
    _In_ ULONG Count,
    _Out_ POINT* Point)
{
    RECT InFrame;
    LONGLONG LargestArea = 0;
    RtlZeroMemory(Point, sizeof(*Point));
    if (Element->BoundsValid == FALSE || !IntersectRect(&InFrame, &Element->Bounds, FrameBounds))
    {
        return FALSE;
    }
    if (Element->ClickablePointValid && PtInRect(&InFrame, Element->ClickablePoint))
    {
        for (ULONG Index = 0; Index < Count; ++Index)
        {
            if (PtInRect(&Displays[Index], Element->ClickablePoint))
            {
                *Point = Element->ClickablePoint;
                return TRUE;
            }
        }
    }
    for (ULONG Index = 0; Index < Count; ++Index)
    {
        RECT Visible;
        if (!IntersectRect(&Visible, &InFrame, &Displays[Index]))
        {
            continue;
        }
        LONGLONG Area = (LONGLONG)(Visible.right - Visible.left) * (Visible.bottom - Visible.top);
        if (Area <= LargestArea)
        {
            continue;
        }
        LargestArea = Area;
        Point->x = Visible.left + (Visible.right - Visible.left) / 2;
        Point->y = Visible.top + (Visible.bottom - Visible.top) / 2;
    }
    return LargestArea != 0;
}

static
HRESULT
CuaBuildRectangle(
    _In_ IJsonValueStatics* Factory,
    _In_ const RECT* Bounds,
    _Outptr_ IJsonValue** Value)
{
    IJsonObject* Object = NULL;
    HRESULT Hr;
    *Value = NULL;
    Hr = Data_JsonCreateObject(&Object);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetNumber(Factory, Object, L"left", Bounds->left);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetNumber(Factory, Object, L"top", Bounds->top);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetNumber(Factory, Object, L"right", Bounds->right);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetNumber(Factory, Object, L"bottom", Bounds->bottom);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Object->lpVtbl->QueryInterface(Object, &IID_IJsonValue, (PVOID*)Value);
Exit:
    if (Object != NULL)
    {
        Object->lpVtbl->Release(Object);
    }
    return Hr;
}

HRESULT
CuaSetRectangle(
    _In_ IJsonValueStatics* Factory,
    _In_ IJsonObject* Object,
    _In_ PCWSTR Name,
    _In_ const RECT* Bounds)
{
    IJsonValue* Value = NULL;
    HRESULT Hr = CuaBuildRectangle(Factory, Bounds, &Value);
    if (SUCCEEDED(Hr))
    {
        Hr = Data_JsonObjectSetValue(Object, Name, Value);
    }
    if (Value != NULL)
    {
        Value->lpVtbl->Release(Value);
    }
    return Hr;
}

HRESULT
CuaBuildWindowInfo(
    _Inout_ CUA_COMMAND* Command,
    _In_ const CUA_WINDOW* Window,
    _In_opt_ const CAPTURED_FRAME* Frame,
    _Outptr_ IJsonValue** Value)
{
    IJsonObject* Object = NULL;
    IJsonValueStatics* Factory = Command->Factory;
    PUNICODE_STRING Path = NULL;
    HANDLE Process = NULL;
    WCHAR Text[1024];
    RECT Bounds;
    HRESULT Hr;
    *Value = NULL;
    Hr = Data_JsonCreateObject(&Object);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = CuaSetWindowHandle(Factory, Object, L"window_handle", Window->Handle);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetNumber(Factory, Object, L"pid", Window->Process);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetNumber(Factory, Object, L"tid", Window->Thread);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetNull(Command->NullFactory, Object, L"process_path");
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (NT_SUCCESS(PS_OpenProcess(&Process, PROCESS_QUERY_LIMITED_INFORMATION, Window->Process)))
    {
        ULONG Length = 0;
        NTSTATUS Status = NtQueryInformationProcess(Process, ProcessImageFileNameWin32, NULL, 0, &Length);
        if (Status == STATUS_INFO_LENGTH_MISMATCH && Length >= sizeof(UNICODE_STRING))
        {
            Path = (PUNICODE_STRING)Mem_Alloc(Length);
            if (Path == NULL)
            {
                Hr = E_OUTOFMEMORY;
                goto Exit;
            }
            Status = NtQueryInformationProcess(Process, ProcessImageFileNameWin32, Path, Length, NULL);
            if (NT_SUCCESS(Status))
            {
                Hr = Data_JsonObjectSetString(Factory, Object, L"process_path", Path->Buffer,
                                                   Path->Length / sizeof(WCHAR));
                if (FAILED(Hr))
                {
                    goto Exit;
                }
                if (Util_Proc_GetProductName(Path->Buffer, Text, ARRAYSIZE(Text)))
                {
                    Hr = Data_JsonObjectSetString(Factory, Object, L"process_product", Text, (ULONG)wcslen(Text));
                    if (FAILED(Hr))
                    {
                        goto Exit;
                    }
                }
            }
        }
    }
    Text[0] = 0;
    UI_GetWindowTextW(Window->Handle, Text);
    Hr = Data_JsonObjectSetString(Factory, Object, L"title", Text, (ULONG)wcslen(Text));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Text[0] = 0;
    GetClassNameW(Window->Handle, Text, ARRAYSIZE(Text));
    Hr = Data_JsonObjectSetString(Factory, Object, L"class", Text, (ULONG)wcslen(Text));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetBoolean(Factory, Object, L"foreground", Window->Handle == NtUserGetForegroundWindow());
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetBoolean(Factory, Object, L"enabled", IsWindowEnabled(Window->Handle));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetBoolean(Factory, Object, L"minimized", IsIconic(Window->Handle));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetBoolean(Factory, Object, L"maximized", IsZoomed(Window->Handle));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetBoolean(Factory, Object, L"visible", IsWindowVisible(Window->Handle));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetBoolean(Factory, Object, L"topmost",
                                        (GetWindowLongPtrW(Window->Handle, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    DWORD Cloaked = 0;
    DwmGetWindowAttribute(Window->Handle, DWMWA_CLOAKED, &Cloaked, sizeof(Cloaked));
    Hr = Data_JsonObjectSetBoolean(Factory, Object, L"cloaked", Cloaked != 0);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    HWND Owner = GetWindow(Window->Handle, GW_OWNER);
    if (Owner != NULL && IsWindow(Owner))
    {
        Hr = CuaSetWindowHandle(Factory, Object, L"owner", Owner);
        if (FAILED(Hr))
        {
            goto Exit;
        }
    }
    if (Frame != NULL)
    {
        RECT Visible;
        LOGICAL InFrame = !IsIconic(Window->Handle) &&
                          SUCCEEDED(GetWindowCaptureBounds(Window->Handle, &Bounds)) &&
                          IntersectRect(&Visible, &Bounds, &Frame->ScreenBounds);
        if (InFrame != FALSE)
        {
            OffsetRect(&Visible, -Frame->ScreenBounds.left, -Frame->ScreenBounds.top);
            Hr = CuaSetRectangle(Factory, Object, L"bounds_image_px", &Visible);
            if (FAILED(Hr))
            {
                goto Exit;
            }
        }
    }
    Hr = Object->lpVtbl->QueryInterface(Object, &IID_IJsonValue, (PVOID*)Value);
Exit:
    if (Process != NULL)
    {
        NtClose(Process);
    }
    Mem_Free(Path);
    if (Object != NULL)
    {
        Object->lpVtbl->Release(Object);
    }
    return Hr;
}

HRESULT
CuaSend(
    _Inout_ CUA_COMMAND* Command,
    _In_reads_(Count) INPUT* Inputs,
    _In_ UINT Count)
{
    Err_SetLastError(ERROR_SUCCESS);
    UINT Sent = NtUserSendInput(Count, Inputs, sizeof(INPUT));
    DWORD Error = Err_GetLastError();
    if (Sent == Count)
    {
        return S_OK;
    }
    for (UINT Index = 0; Index < Sent && Index < Count; ++Index)
    {
        INPUT Release = Inputs[Index];
        if (Release.type == INPUT_KEYBOARD && !(Release.ki.dwFlags & KEYEVENTF_KEYUP))
        {
            Release.ki.dwFlags |= KEYEVENTF_KEYUP;
        } else if (Release.type == INPUT_MOUSE)
        {
            DWORD Up = 0;
            if (Release.mi.dwFlags & MOUSEEVENTF_LEFTDOWN)
            {
                Up |= MOUSEEVENTF_LEFTUP;
            }
            if (Release.mi.dwFlags & MOUSEEVENTF_RIGHTDOWN)
            {
                Up |= MOUSEEVENTF_RIGHTUP;
            }
            if (Release.mi.dwFlags & MOUSEEVENTF_MIDDLEDOWN)
            {
                Up |= MOUSEEVENTF_MIDDLEUP;
            }
            if (Release.mi.dwFlags & MOUSEEVENTF_XDOWN)
            {
                Up |= MOUSEEVENTF_XUP;
            }
            if (Up == 0)
            {
                continue;
            }
            Release.mi.dwFlags = Up;
        } else
        {
            continue;
        }
        NtUserSendInput(1, &Release, sizeof(Release));
    }
    return CuaFail(Command, Error != 0 ? HRESULT_FROM_WIN32(Error) : E_FAIL,
                   L"Windows accepted only part of the input sequence; do not replay it automatically.");
}

static
HRESULT
Capabilities(
    _Inout_ CUA_COMMAND* Command)
{
    IJsonObject* Object = NULL;
    HRESULT Hr;
    Hr = Data_JsonCreateObject(&Object);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetString(Command->Factory, Object, L"instance", Command->Server->Instance,
                                       (ULONG)wcslen(Command->Server->Instance));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetNumber(Command->Factory, Object, L"pid", HandleToULong(NtCurrentProcessId()));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetBoolean(Command->Factory, Object, L"desktop_ready", DesktopCaptureReady());
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Object->lpVtbl->QueryInterface(Object, &IID_IJsonValue, (PVOID*)&Command->Result);
Exit:
    if (Object != NULL)
    {
        Object->lpVtbl->Release(Object);
    }
    return Hr;
}

static
HRESULT
Dispatch(
    _Inout_ CUA_COMMAND* Command)
{
    HRESULT Hr = CuaValidateParameters(Command->Request, &Command->Error);
    if (FAILED(Hr))
    {
        return Hr;
    }
    switch (Command->Request->Method)
    {
    case CuaMethodCapabilities:
        return Capabilities(Command);
    case CuaMethodInspect:
        return CuaInspect(Command);
    case CuaMethodLocate:
        return CuaLocate(Command);
    case CuaMethodActivate:
    case CuaMethodMinimize:
    case CuaMethodMaximize:
    case CuaMethodCloseWindow:
        return CuaOperate(Command);
    case CuaMethodSessionList:
    case CuaMethodSessionCreateChild:
    case CuaMethodSessionPreview:
    case CuaMethodSessionDestroy:
        return CuaSessionOperation(Command);
    case CuaMethodSessionEnableChildSession:
        return CuaFail(Command, E_INVALIDARG, L"Run EnableChildSession in the calling process.");
    default:
        return CuaAction(Command);
    }
}

HRESULT
CuaAfterAction(
    _Inout_ CUA_COMMAND* Command,
    _In_opt_ HWND Handle)
{
    CUA_ERROR ActionError = Command->Error;
    CUA_INSPECT_OPTIONS Options = { 0 };
    IJsonObject *Object = NULL, *ErrorObject = NULL;
    IJsonValue* Value = NULL;
    HRESULT Observed, Hr;
    Options.Handle = Handle;
    Options.OutFile = CuaString(&Command->Request->Parameters, CuaParamOutFile, L"");
    Options.Backend = CaptureBackendWgc;
    Options.Uia = TRUE;
    Options.UiaOptions.TimeoutMs = WUA_UIA_DEFAULT_TIMEOUT_MS;
    Options.UiaOptions.MaxNodes = 256;
    Options.UiaOptions.MaxDepth = 12;
    RtlZeroMemory(&Command->Error, sizeof(Command->Error));
    PS_DelayExec(50);
    Observed = CuaObserve(Command, &Options);
    if (Command->Result != NULL)
    {
        Hr = Command->Result->lpVtbl->GetObject(Command->Result, &Object);
    } else
    {
        Hr = Data_JsonCreateObject(&Object);
        if (SUCCEEDED(Hr) && FAILED(Observed))
        {
            ErrorObject = BuildErrorOutput(Observed,
                "The operation finished, but the subsequent inspection failed. Inspect before further input.");
            if (ErrorObject != NULL)
            {
                Hr = ErrorObject->lpVtbl->QueryInterface(ErrorObject, &IID_IJsonValue, (PVOID*)&Value);
                if (SUCCEEDED(Hr))
                {
                    Hr = Data_JsonObjectSetValue(Object, L"observation_error", Value);
                }
            }
        }
    }
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Command->ClipboardError != NULL)
    {
        Hr = Data_JsonObjectSetValue(Object, L"clipboard_error", Command->ClipboardError);
        if (FAILED(Hr))
        {
            goto Exit;
        }
    }
    if (Command->Result == NULL)
    {
        Object->lpVtbl->QueryInterface(Object, &IID_IJsonValue, (PVOID*)&Command->Result);
    }
Exit:
    Command->Error = ActionError;
    if (Value != NULL)
    {
        Value->lpVtbl->Release(Value);
    }
    if (ErrorObject != NULL)
    {
        ErrorObject->lpVtbl->Release(ErrorObject);
    }
    if (Object != NULL)
    {
        Object->lpVtbl->Release(Object);
    }
    return S_OK;
}

HRESULT
CuaExecute(
    _Inout_ CUA_SERVER* Server,
    _In_ CUA_REQUEST* Request,
    _Outptr_ IJsonValue** Reply)
{
    CUA_COMMAND Command = { 0 };
    LOGICAL Locked = FALSE;
    HRESULT Hr;
    *Reply = NULL;
    Command.Server = Server;
    Command.Request = Request;
    Hr = Data_JsonGetValueFactory(&Command.Factory);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Command.Factory->lpVtbl->QueryInterface(Command.Factory, &IID_IJsonValueStatics2,
                                              (PVOID*)&Command.NullFactory);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Request->Method != CuaMethodCapabilities && Request->Method != CuaMethodSessionList &&
        Request->Method != CuaMethodSessionCreateChild && Request->Method != CuaMethodSessionPreview &&
        Request->Method != CuaMethodSessionDestroy)
    {
        NTSTATUS Wait = PS_WaitForObject(Server->Gate, 2000);
        Locked = Wait == STATUS_WAIT_0 || Wait == STATUS_ABANDONED;
        if (Locked == FALSE)
        {
            CuaFail(&Command, HRESULT_FROM_WIN32(ERROR_BUSY), L"The session is busy; this operation did not start.");
            goto Respond;
        }
    }
    if (RpcServerTestCancel(NULL) == RPC_S_OK)
    {
        Hr = CuaFail(&Command, HRESULT_FROM_WIN32(RPC_S_CALL_CANCELLED),
                     L"The command was canceled before execution; no operation was submitted.");
        goto Respond;
    }
    Hr = Dispatch(&Command);
Respond:
    if (FAILED(Hr) && SUCCEEDED(Command.Error.Status))
    {
        CuaFail(&Command, Hr, L"A Windows API failed; inspect current state before retrying.");
    }
    Hr = CuaBuildReply(Command.Result, FAILED(Command.Error.Status) ? &Command.Error : NULL, Reply);
    if (FAILED(Hr))
    {
        goto Exit;
    }
Exit:
    if (FAILED(Hr) && *Reply != NULL)
    {
        (*Reply)->lpVtbl->Release(*Reply);
        *Reply = NULL;
    }
    if (Locked != FALSE)
    {
        NtReleaseMutant(Server->Gate, NULL);
    }
    if (Command.Result != NULL)
    {
        Command.Result->lpVtbl->Release(Command.Result);
    }
    if (Command.ClipboardError != NULL)
    {
        Command.ClipboardError->lpVtbl->Release(Command.ClipboardError);
    }
    if (Command.NullFactory != NULL)
    {
        Command.NullFactory->lpVtbl->Release(Command.NullFactory);
    }
    if (Command.Factory != NULL)
    {
        Command.Factory->lpVtbl->Release(Command.Factory);
    }
    return Hr;
}
