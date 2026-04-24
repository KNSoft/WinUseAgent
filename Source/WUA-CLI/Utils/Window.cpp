#include "pch.h"

_Success_(return != NULL)
_Ret_maybenull_
cJSON*
Util_Window_GetInfoJson(
    _In_ HWND hWnd)
{
    cJSON *j;
    CHAR sz[sizeof(UNICODE_STRING) + MAX_PATH * sizeof(WCHAR)];
    ULONG u, uMaxCchW = sizeof(sz) / sizeof(WCHAR);
    PWSTR psz = reinterpret_cast<PWSTR>(sz);
    DWORD dw;
    DWORD_PTR dwp;
    BOOL b;
    NTSTATUS Status;
    HANDLE hProc;
    PUNICODE_STRING pus = reinterpret_cast<PUNICODE_STRING>(sz);

    /* Handle */
    if (!IsWindow(hWnd) || Util_Window_GetHandleString(hWnd, sz, ARRAYSIZE(sz)) == 0)
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

    /* Minimized */
    b = IsIconic(hWnd);
    cJSON_AddBoolToObject(j, "minimized", b);

    /* PID */
    if (GetWindowThreadProcessId(hWnd, &dw) != 0 && dw != 0)
    {
        cJSON_AddNumberToObject(j, "pid", dw);
        Status = PS_OpenProcess(&hProc, PROCESS_QUERY_LIMITED_INFORMATION, dw);
        if (NT_SUCCESS(Status))
        {
            Status = NtQueryInformationProcess(hProc, ProcessImageFileNameWin32, sz, sizeof(sz), NULL);
            if (NT_SUCCESS(Status))
            {
                Util_Json_AddUnicodeString(j, "process_path", pus->Buffer, pus->Length / sizeof(WCHAR));
                if (Util_Proc_GetProductName(pus->Buffer, psz, uMaxCchW))
                {
                    Util_Json_AddUnicodeString(j, "process_product", psz, 0);
                }
            }
            NtClose(hProc);
        }
    }

    return j;
}
