#include "pch.h"

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

_Success_(return > 0)
ULONG
Utils_Window_GetHandleString(
    _In_ HWND hWnd,
    _Out_writes_(BufferCch) PSTR Buffer,
    _In_ ULONG BufferCch)
{
    return Str_FromIntExA(UI_TruncateHandle32(hWnd), TRUE, 16, Buffer, BufferCch);
}

_Success_(return != FALSE)
LOGICAL
Utils_Window_Active(
    _In_ HWND hWnd)
{
    BOOL b = BringWindowToTop(hWnd);
    b |= SetForegroundWindow(hWnd);
    return b;
}

_Success_(return != NULL)
_Ret_maybenull_
cJSON*
Utils_Window_GetInfoJson(
    _In_ HWND hWnd)
{
    cJSON *j;
    CHAR sz[512];
    ULONG u, uMaxCchW = sizeof(sz) / sizeof(WCHAR);
    PWSTR psz = reinterpret_cast<PWSTR>(sz);
    DWORD dw;
    DWORD_PTR dwp;

    /* Handle */
    if (!IsWindow(hWnd) || Utils_Window_GetHandleString(hWnd, sz, ARRAYSIZE(sz)) == 0)
    {
        return NULL;
    }
    j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "handle", sz);

    /* Title */
    if (Util_Window_SendMsgTO(hWnd, WM_GETTEXT, uMaxCchW, (LPARAM)psz, &dwp) == ERROR_SUCCESS)
    {
        u = (ULONG)dwp;
        if (u == 0)
        {
            cJSON_AddNullToObject(j, "title");
        } else if (u < uMaxCchW)
        {
            psz[u] = UNICODE_NULL;
            Util_Json_AddUnicodeString(j, "title", psz, u);
        }
    }

    /* Class */
    u = GetClassNameW(hWnd, psz, uMaxCchW);
    if (u > 0)
    {
        Util_Json_AddUnicodeString(j, "class", psz, u);
    }

    /* Visibility */
    cJSON_AddBoolToObject(j, "visible", IsWindowVisible(hWnd));

    /* PID */
    if (GetWindowThreadProcessId(hWnd, &dw) != 0 && dw != 0)
    {
        cJSON_AddNumberToObject(j, "pid", dw);
    }

    return j;
}
