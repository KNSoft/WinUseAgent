#pragma once

#include "Command.h"
#include "../../Utils/Capture.h"
#include "ChildSession.h"
#include "Uia.h"

typedef struct _CUA_WINDOW
{
    HWND Handle;
    DWORD Process, Thread;
} CUA_WINDOW;

typedef struct _CUA_OBSERVATION
{
    CUA_WINDOW Window, UiaWindow;
    HWND Foreground;
    LOGICAL UiaImageAligned;
    CAPTURED_FRAME Frame;
    UIA_ELEMENT* Elements;
    ULONG ElementCount;
} CUA_OBSERVATION;

typedef struct _CUA_INSPECT_OPTIONS
{
    HWND Handle;
    PCWSTR OutFile;
    LOGICAL Uia;
    CAPTURE_BACKEND Backend;
    UIA_OPTIONS UiaOptions;
} CUA_INSPECT_OPTIONS;
typedef struct _CUA_SERVER
{
    HANDLE Gate;
    DWORD SessionId, HeldButtons;
    WCHAR Instance[CUA_GUID_CCH];
    CUA_SESSION_MANAGER* Sessions;
    UIA_CONTEXT* Uia;
} CUA_SERVER;

typedef struct _CUA_COMMAND
{
    CUA_SERVER* Server;
    CUA_REQUEST* Request;
    IJsonValueStatics* Factory;
    IJsonValueStatics2* NullFactory;
    IJsonValue *Result, *ClipboardError;
    CUA_ERROR Error;
} CUA_COMMAND;

EXTERN_C_START

HRESULT
CuaServerCreate(
    _Outptr_ CUA_SERVER** Server);
VOID
CuaServerDestroy(
    _In_opt_ CUA_SERVER* Server);
HRESULT
CuaExecute(
    _Inout_ CUA_SERVER* Server,
    _In_ CUA_REQUEST* Request,
    _Outptr_ IJsonValue** Reply);
_Post_satisfies_(return < 0)
HRESULT
CuaFail(
    _Inout_ CUA_COMMAND* Command,
    _In_ HRESULT Status,
    _In_ PCWSTR Message);
HRESULT
CuaInteger(
    _Inout_ CUA_COMMAND* Command,
    _In_ CUA_PARAMETER Parameter,
    _In_ LONG Default,
    _In_ LONG Minimum,
    _In_ LONG Maximum,
    _Out_ PLONG Value);
HRESULT
CuaInputDesktop(
    _Inout_ CUA_COMMAND* Command);
HRESULT
CuaOutputPath(
    _Inout_ CUA_COMMAND* Command,
    _Outptr_ PCWSTR* Path);
LOGICAL
CuaSameWindow(
    _In_ const CUA_WINDOW* Window);
HRESULT
CuaClientBounds(
    _In_ HWND Window,
    _Out_ RECT* Bounds);
HRESULT
CuaQueryWindow(
    _In_ HWND Handle,
    _Out_ CUA_WINDOW* Window);
HRESULT
CuaTargetWindow(
    _Inout_ CUA_COMMAND* Command,
    _Out_ CUA_WINDOW* Window);
HRESULT
CuaSetWindowHandle(
    _In_ IJsonValueStatics* Factory,
    _In_ IJsonObject* Object,
    _In_ PCWSTR Name,
    _In_opt_ HWND Handle);
HRESULT
CuaPrepareWindow(
    _Inout_ CUA_COMMAND* Command,
    _In_ const CUA_WINDOW* Window,
    _In_ LOGICAL RequireForeground);
LOGICAL
CuaHitTarget(
    _In_ HWND Window,
    _In_ POINT Point);
LOGICAL
CuaElementPoint(
    _In_ const UIA_ELEMENT* Element,
    _In_ const RECT* FrameBounds,
    _In_reads_(Count) const RECT* Displays,
    _In_ ULONG Count,
    _Out_ POINT* Point);
HRESULT
CuaSetRectangle(
    _In_ IJsonValueStatics* Factory,
    _In_ IJsonObject* Object,
    _In_ PCWSTR Name,
    _In_ const RECT* Bounds);
HRESULT
CuaBuildWindowInfo(
    _Inout_ CUA_COMMAND* Command,
    _In_ const CUA_WINDOW* Window,
    _In_opt_ const CAPTURED_FRAME* Frame,
    _Outptr_ IJsonValue** Value);
HRESULT
CuaInspect(
    _Inout_ CUA_COMMAND* Command);
HRESULT
CuaObserve(
    _Inout_ CUA_COMMAND* Command,
    _In_ const CUA_INSPECT_OPTIONS* Options);
HRESULT
CuaLocate(
    _Inout_ CUA_COMMAND* Command);
HRESULT
CuaOperate(
    _Inout_ CUA_COMMAND* Command);
HRESULT
CuaSessionOperation(
    _Inout_ CUA_COMMAND* Command);
HRESULT
CuaAction(
    _Inout_ CUA_COMMAND* Command);
HRESULT
CuaAfterAction(
    _Inout_ CUA_COMMAND* Command,
    _In_opt_ HWND Handle);
HRESULT
CuaMouseMove(
    _Inout_ CUA_COMMAND* Command,
    _In_ POINT Point,
    _Out_ INPUT* Input);
HRESULT
CuaSend(
    _Inout_ CUA_COMMAND* Command,
    _In_reads_(Count) INPUT* Inputs,
    _In_ UINT Count);
HRESULT
CuaKeyCode(
    _Inout_ CUA_COMMAND* Command,
    _In_ PCWSTR Name,
    _Out_ PWORD Key);
HRESULT
CuaPressKeys(
    _Inout_ CUA_COMMAND* Command,
    _In_reads_(Count) const WORD* Keys,
    _In_ ULONG Count);
HRESULT
CuaValidateText(
    _Inout_ CUA_COMMAND* Command,
    _In_ PCWSTR Text,
    _In_ PCWSTR Method);
HRESULT
CuaParseKeys(
    _Inout_ CUA_COMMAND* Command,
    _Out_writes_(CUA_MAX_KEYS) WORD* Keys);
HRESULT
CuaTypeText(
    _Inout_ CUA_COMMAND* Command,
    _In_ PCWSTR Text,
    _In_ PCWSTR Method,
    _In_ HWND Expected,
    _In_ HWND ExpectedFocus);

EXTERN_C_END
