#pragma once

#include "../pch.h"

#pragma region Gdip.cpp

_Success_(return == NULL)
_Ret_maybenull_
cJSON*
Util_GdipStartup(VOID);

VOID
Util_GdipShutdown(VOID);

#pragma endregion

#pragma region Json.cpp

_Ret_notnull_
cJSON*
Util_Json_AddUnicodeString(
    _In_ cJSON * j,
    _In_ PCSTR Key,
    _In_reads_opt_(Length) PCWSTR String,
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

W32ERROR
Util_Window_SendMsgTO(_In_ HWND Window,
                      _In_ UINT Msg,
                      _In_ WPARAM wParam,
                      _In_ LPARAM lParam,
                      _Out_opt_ PDWORD_PTR lpdwResult);

_Success_(return > 0)
ULONG
Utils_Window_GetHandleString(
    _In_ HWND hWnd,
    _Out_writes_(BufferCch) PSTR Buffer,
    _In_ ULONG BufferCch);

_Success_(return != NULL)
_Ret_maybenull_
cJSON*
Utils_Window_GetInfoJson(
    _In_ HWND hWnd);

_Success_(return != FALSE)
LOGICAL
Utils_Window_Active(
    _In_ HWND hWnd);

_Success_(return != NULL)
_Ret_maybenull_
IUIAutomation*
Util_UIA_CreateInstance(VOID);

ULONG
Util_UIA_GetText(
    _In_ IUIAutomationElement * Element,
    _Out_writes_(BufferCch) PSTR Buffer,
    _In_ ULONG BufferCch);

_Success_(return != NULL)
_Ret_maybenull_
cJSON*
Utils_UIA_GetInfoJson(
    _In_ IUIAutomationElement * Element);
