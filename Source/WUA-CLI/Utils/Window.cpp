#include "pch.h"

_Success_(return != NULL)
_Ret_maybenull_
IJsonObject*
Util_Window_GetInfoJson(
    _In_ HWND hWnd)
{
    IJsonObject* j;
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
    j = Util_Json_CreateObject();
    Util_Json_AddStringToObject(j, L"handle", sz);

    /* Title */
    if (Util_Window_SendMsgTO(hWnd, WM_GETTEXT, uMaxCchW, (LPARAM)psz, &dwp) == ERROR_SUCCESS)
    {
        u = (ULONG)dwp;
        if (u == 0)
        {
            Util_Json_AddNullToObject(j, L"title");
        } else if (u < uMaxCchW)
        {
            psz[u] = UNICODE_NULL;
            Util_Json_AddUnicodeString(j, L"title", psz, u);
        }
    }

    /* Class */
    u = GetClassNameW(hWnd, psz, uMaxCchW);
    if (u > 0)
    {
        Util_Json_AddUnicodeString(j, L"class", psz, u);
    }

    /* Minimized */
    b = IsIconic(hWnd);
    Util_Json_AddBoolToObject(j, L"minimized", b);

    /* PID */
    if (GetWindowThreadProcessId(hWnd, &dw) != 0 && dw != 0)
    {
        Util_Json_AddNumberToObject(j, L"pid", dw);
        Status = PS_OpenProcess(&hProc, PROCESS_QUERY_LIMITED_INFORMATION, dw);
        if (NT_SUCCESS(Status))
        {
            Status = NtQueryInformationProcess(hProc, ProcessImageFileNameWin32, sz, sizeof(sz), NULL);
            if (NT_SUCCESS(Status))
            {
                Util_Json_AddUnicodeString(j, L"process_path", pus->Buffer, pus->Length / sizeof(WCHAR));
                if (Util_Proc_GetProductName(pus->Buffer, psz, uMaxCchW))
                {
                    Util_Json_AddUnicodeString(j, L"process_product", psz, 0);
                }
            }
            NtClose(hProc);
        }
    }

    return j;
}

_Ret_maybenull_
IJsonObject*
Util_Window_GetGUIInfoJson(
    _In_ PGUITHREADINFO Info,
    _In_opt_ HWND CaretMapWindow)
{
    IJsonObject *j, *j_CaretRect;

    j = Util_Json_CreateObject();
    Util_Json_AddWindowHandle(j, L"handle", Info->hwndActive);
    Util_Json_AddWindowHandle(j, L"focus_handle", Info->hwndFocus);
    Util_Json_AddWindowHandle(j, L"caret_handle", Info->hwndCaret);
    if (Info->hwndCaret != NULL && Util_Window_GetRoot(Info->hwndCaret) == Info->hwndActive)
    {
        MapWindowPoints(Info->hwndCaret, CaretMapWindow, (LPPOINT)&Info->rcCaret, 2);
        j_CaretRect = Util_Json_AddObjectToObject(j, L"caret_rectangle");
        Util_Json_AddNumberToObject(j_CaretRect, L"left", Info->rcCaret.left);
        Util_Json_AddNumberToObject(j_CaretRect, L"top", Info->rcCaret.top);
        Util_Json_AddNumberToObject(j_CaretRect, L"right", Info->rcCaret.right);
        Util_Json_AddNumberToObject(j_CaretRect, L"bottom", Info->rcCaret.bottom);
        if (j_CaretRect != NULL)
        {
            j_CaretRect->Release();
        }
    }

    return j;
}
