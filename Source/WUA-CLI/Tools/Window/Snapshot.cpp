#include "pch.h"

static PWSTR OutFile;

static WUA_COMMAND_PARAMETER Parameters[] = {
    DEF_PARAMETER_ENTRY(OutFile, String, FALSE),
};

WUA_COMMAND_FN Command;
WUA_COMMAND Window_Snapshot = { Parameters, ARRAYSIZE(Parameters), &Command };

static
_Function_class_(WUA_COMMAND_FN)
_Ret_notnull_
cJSON*
Command(VOID)
{
    cJSON *j, *j_Active_Window, *j_Windows, *j_Window, *j_Virtual_Screen;
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
    j = cJSON_CreateObject();
    UI_GetScreenPos(&pt, &size);
    j_Virtual_Screen = cJSON_AddObjectToObject(j, "virtual_screen");
    cJSON_AddNumberToObject(j_Virtual_Screen, "left", pt.x);
    cJSON_AddNumberToObject(j_Virtual_Screen, "top", pt.y);
    cJSON_AddNumberToObject(j_Virtual_Screen, "right", pt.x + size.cx);
    cJSON_AddNumberToObject(j_Virtual_Screen, "bottom", pt.y + size.cy);

    /* Get foreground windows information */
    gti.cbSize = sizeof(gti);
    if (GetGUIThreadInfo(0, &gti) && gti.hwndActive != NULL && IsTopLevelWindow(gti.hwndActive))
    {
        j_Active_Window = Util_Window_GetGUIInfoJson(&gti, HWND_DESKTOP);
        cJSON_AddItemToObject(j_Active_Window, "uia_tree", Util_UIA_GetWindowElementJson(gti.hwndActive));
    } else
    {
        j_Active_Window = cJSON_CreateNull();
    }
    cJSON_AddItemToObject(j, "active_window", j_Active_Window);

    /* Enumerate top-level windows */
    j_Windows = cJSON_CreateArray();
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
                cJSON_AddItemToArray(j_Windows, j_Window);
            }
        }
        hWnd = GetWindow(hWnd, GW_HWNDNEXT);
    }
    cJSON_AddItemToObject(j, "windows", j_Windows);

    return BuildSuccessOutput(j);
}
