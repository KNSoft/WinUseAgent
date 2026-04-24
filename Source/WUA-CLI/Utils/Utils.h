#pragma once

#include "../pch.h"

#pragma region Gdip.cpp

_Success_(return == NULL)
_Ret_maybenull_
cJSON*
Util_Gdip_Startup(VOID);

VOID
Util_Gdip_Shutdown(VOID);

_Success_(return == NULL)
_Ret_maybenull_
cJSON*
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
cJSON*
Util_Window_GetInfoJson(
    _In_ HWND hWnd);

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

_Ret_notnull_
cJSON*
Util_UIA_GetInfoJson(
    _In_ IUIAutomationElement * Element);

_Ret_notnull_
cJSON*
Util_UIA_GetWindowElementJson(
    _In_ HWND hWnd);

#pragma region Json.cpp

FORCEINLINE
_Ret_notnull_
cJSON*
Util_Json_AddWindowHandle(
    _In_ cJSON * j,
    _In_ PCSTR Key,
    _In_opt_ HWND hWnd)
{
    CHAR sz[32];
    if (hWnd != NULL && Util_Window_GetHandleString(hWnd, sz, ARRAYSIZE(sz)) > 0)
    {
        return cJSON_AddStringToObject(j, Key, sz);
    } else
    {
        return cJSON_AddNullToObject(j, Key);
    }
}

_Ret_notnull_
cJSON*
Util_Json_AddUnicodeString(
    _In_ cJSON * j,
    _In_ PCSTR Key,
    _In_reads_opt_(Length + 1) PCWSTR String,
    _In_opt_ ULONG Length);

_Ret_notnull_
cJSON*
Util_Json_AddBstr(
    _In_ cJSON * j,
    _In_ PCSTR Key,
    _In_opt_ BSTR Value);

_Ret_notnull_
cJSON*
Util_Json_AddVariant(
    _In_ cJSON * j,
    _In_ PCSTR Key,
    _In_opt_ LPVARIANT Value);

#pragma endregion

_Success_(return != FALSE)
LOGICAL
Util_Proc_GetProductName(
    _In_ PCWSTR File,
    _Out_writes_(BufferCch) PWSTR Buffer,
    _In_ ULONG BufferCch);
