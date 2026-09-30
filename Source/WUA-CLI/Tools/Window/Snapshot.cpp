#include "pch.h"

static PWSTR OutFile;

static WUA_COMMAND_PARAMETER Parameters[] = {
    DEF_PARAMETER_ENTRY(OutFile, String, FALSE),
};

WUA_COMMAND_FN Command;
WUA_COMMAND Window_Snapshot = { Parameters, ARRAYSIZE(Parameters), &Command };

static
_Function_class_(WUA_COMMAND_FN)
_Ret_maybenull_
IJsonObject*
Command(VOID)
{
    IJsonObject *j, *j_Active_Window = NULL, *j_Window, *j_Virtual_Screen;
    IJsonVector* j_Windows;
    IJsonObject* j_UIA;
    HWND hWnd;
    GUITHREADINFO gti;
    POINT pt;
    SIZE size;
    RECT rc;
    LONG_PTR dwpExStyle;
    BYTE bAlpha;

    /* Take snapshot */
    if (OutFile != NULL && *OutFile != UNICODE_NULL)
    {
        j = Util_Gdip_SaveSnapshot(NULL, OutFile);
        if (j != NULL)
        {
            return j;
        }
    }

    /* Get virtual screen position and size */
    j = Util_Json_CreateObject();
    UI_GetScreenPos(&pt, &size);
    j_Virtual_Screen = Util_Json_AddObjectToObject(j, L"virtual_screen");
    Util_Json_AddNumberToObject(j_Virtual_Screen, L"left", pt.x);
    Util_Json_AddNumberToObject(j_Virtual_Screen, L"top", pt.y);
    Util_Json_AddNumberToObject(j_Virtual_Screen, L"right", pt.x + size.cx);
    Util_Json_AddNumberToObject(j_Virtual_Screen, L"bottom", pt.y + size.cy);
    if (j_Virtual_Screen != NULL)
    {
        j_Virtual_Screen->Release();
    }

    /* Get foreground windows information */
    gti.cbSize = sizeof(gti);
    if (GetGUIThreadInfo(0, &gti) && gti.hwndActive != NULL && IsTopLevelWindow(gti.hwndActive))
    {
        j_Active_Window = Util_Window_GetGUIInfoJson(&gti, HWND_DESKTOP);
        j_UIA = Util_UIA_GetWindowElementJson(gti.hwndActive);
        if (j_UIA != NULL)
        {
            Util_Json_AddItemToObject(j_Active_Window, L"uia_tree", j_UIA);
            j_UIA->Release();
        } else
        {
            Util_Json_AddNullToObject(j_Active_Window, L"uia_tree");
        }
    }
    if (j_Active_Window != NULL)
    {
        Util_Json_AddItemToObject(j, L"active_window", j_Active_Window);
        j_Active_Window->Release();
    } else
    {
        Util_Json_AddNullToObject(j, L"active_window");
    }

    /* Enumerate top-level windows */
    j_Windows = Util_Json_CreateArray();
    hWnd = GetWindow(GetDesktopWindow(), GW_CHILD);
    while (hWnd != NULL)
    {
        if (IsWindowEnabled(hWnd) &&
            IsWindowVisible(hWnd) &&
            GetClientRect(hWnd, &rc) &&
            rc.right != 0 && rc.bottom != 0 &&
            !Util_Window_IsCloaked(hWnd) &&
            UI_GetWindowLong(hWnd, GWL_EXSTYLE, &dwpExStyle) == ERROR_SUCCESS &&
            !BooleanFlagOn(dwpExStyle, WS_EX_TRANSPARENT) &&
            (!BooleanFlagOn(dwpExStyle, WS_EX_LAYERED) ||
             !GetLayeredWindowAttributes(hWnd, NULL, &bAlpha, NULL) ||
             bAlpha != 0))
        {
            j_Window = Util_Window_GetInfoJson(hWnd);
            if (j_Window != NULL)
            {
                Util_Json_AddItemToArray(j_Windows, j_Window);
                j_Window->Release();
            }
        }
        hWnd = GetWindow(hWnd, GW_HWNDNEXT);
    }
    Util_Json_AddItemToObject(j, L"windows", j_Windows);
    if (j_Windows != NULL)
    {
        j_Windows->Release();
    }

    return BuildSuccessOutput(j);
}
