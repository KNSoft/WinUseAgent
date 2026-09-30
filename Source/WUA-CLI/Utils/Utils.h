#pragma once

#include "../pch.h"

#pragma region Gdip.cpp

_Success_(return == NULL)
_Ret_maybenull_
IJsonObject*
Util_Gdip_Startup(VOID);

VOID
Util_Gdip_Shutdown(VOID);

_Success_(return == NULL)
_Ret_maybenull_
IJsonObject*
Util_Gdip_SaveSnapshot(
    _In_opt_ HWND hWnd,
    _In_ PCWSTR pszFile);

#pragma endregion

#pragma region Window

FORCEINLINE
W32ERROR
Util_Window_SendMsgTO(
    _In_ HWND Window,
    _In_ UINT Msg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam,
    _Out_opt_ PDWORD_PTR lpdwResult)
{
    return UI_SendMessageTimeout(Window, Msg, wParam, lParam, SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 200, lpdwResult);
}

FORCEINLINE
_Success_(return > 0)
ULONG
Util_Window_GetHandleString(
    _In_ HWND hWnd,
    _Out_writes_(BufferCch) PSTR Buffer,
    _In_ ULONG BufferCch)
{
    return Str_FromIntExA(UI_TruncateHandle32(hWnd), TRUE, 16, Buffer, BufferCch);
}

FORCEINLINE
_Success_(return != FALSE)
LOGICAL
Util_Window_Active(
    _In_ HWND hWnd)
{
    BOOL b = BringWindowToTop(hWnd);
    b |= SetForegroundWindow(hWnd);
    return b;
}

FORCEINLINE
_Ret_maybenull_
HWND
Util_Window_GetRoot(
    _In_opt_ HWND hWnd)
{
    return hWnd == NULL ? NULL : GetAncestor(hWnd, GA_ROOT);
}

FORCEINLINE
LOGICAL
Util_Window_IsCloaked(
    _In_ HWND hWnd)
{
    DWORD dw;
    return DwmGetWindowAttribute(hWnd, DWMWA_CLOAKED, &dw, sizeof(dw)) == S_OK && dw != 0;
}

FORCEINLINE
HRESULT
Util_Window_Uncloake(
    _In_ HWND hWnd)
{
    BOOL bCloak = FALSE;
    return DwmSetWindowAttribute(hWnd, DWMWA_CLOAK, &bCloak, sizeof(bCloak));
}

_Success_(return != NULL)
_Ret_maybenull_
IJsonObject*
Util_Window_GetInfoJson(
    _In_ HWND hWnd);

_Ret_maybenull_
IJsonObject*
Util_Window_GetGUIInfoJson(
    _In_ PGUITHREADINFO Info,
    _In_opt_ HWND CaretMapWindow);

#pragma endregion

_Success_(return != NULL)
_Ret_maybenull_
IUIAutomation*
Util_UIA_CreateInstance(VOID);

ULONG
Util_UIA_GetText(
    _In_ IUIAutomationElement * Element,
    _Out_writes_(BufferCch) PSTR Buffer,
    _In_ ULONG BufferCch);

_Ret_maybenull_
IJsonObject*
Util_UIA_GetInfoJson(
    _In_ IUIAutomationElement * Element);

_Ret_maybenull_
IJsonObject*
Util_UIA_GetWindowElementJson(
    _In_ HWND hWnd);

#pragma region Json.cpp

HRESULT
Util_Json_Initialize(VOID);

VOID
Util_Json_Shutdown(VOID);

HRESULT
Util_Json_Write(
    _In_opt_ IUnknown* Value,
    _In_ HANDLE File);

// Create functions and AddObjectToObject return owned references; other JSON inputs are borrowed.
// NULL JSON inputs are only propagated after a recorded construction failure.
_Ret_maybenull_
IJsonObject*
Util_Json_CreateObject(VOID);

_Ret_maybenull_
IJsonVector*
Util_Json_CreateArray(VOID);

VOID
Util_Json_AddItemToObject(
    _In_opt_ IJsonObject* Object,
    _In_ PCWSTR Key,
    _In_opt_ IUnknown* Value);

VOID
Util_Json_AddItemToArray(
    _In_opt_ IJsonVector* Array,
    _In_opt_ IUnknown* Value);

ULONG
Util_Json_GetArraySize(
    _In_opt_ IJsonVector* Array);

_Ret_maybenull_
IJsonObject*
Util_Json_AddObjectToObject(
    _In_opt_ IJsonObject* Object,
    _In_ PCWSTR Key);

VOID
Util_Json_AddNullToObject(
    _In_opt_ IJsonObject* Object,
    _In_ PCWSTR Key);

VOID
Util_Json_AddStringToObject(
    _In_opt_ IJsonObject* Object,
    _In_ PCWSTR Key,
    _In_ PCSTR Value);

VOID
Util_Json_AddBoolToObject(
    _In_opt_ IJsonObject* Object,
    _In_ PCWSTR Key,
    _In_ LOGICAL Value);

VOID
Util_Json_AddNumberToObject(
    _In_opt_ IJsonObject* Object,
    _In_ PCWSTR Key,
    _In_ DOUBLE Value);

FORCEINLINE
VOID
Util_Json_AddWindowHandle(
    _In_opt_ IJsonObject* j,
    _In_ PCWSTR Key,
    _In_opt_ HWND hWnd)
{
    CHAR sz[32];
    if (hWnd != NULL && Util_Window_GetHandleString(hWnd, sz, ARRAYSIZE(sz)) > 0)
    {
        Util_Json_AddStringToObject(j, Key, sz);
    } else
    {
        Util_Json_AddNullToObject(j, Key);
    }
}

VOID
Util_Json_AddUnicodeString(
    _In_opt_ IJsonObject* j,
    _In_ PCWSTR Key,
    _When_(Length == 0, _In_opt_z_) _When_(Length != 0, _In_reads_(Length)) PCWSTR String,
    _In_opt_ ULONG Length);

VOID
Util_Json_AddBstr(
    _In_opt_ IJsonObject* j,
    _In_ PCWSTR Key,
    _In_opt_ BSTR Value);

#pragma endregion

_Success_(return != FALSE)
LOGICAL
Util_Proc_GetProductName(
    _In_ PCWSTR File,
    _Out_writes_(BufferCch) PWSTR Buffer,
    _In_ ULONG BufferCch);
