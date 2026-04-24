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
    cJSON *j, *j_Active_Window, *j_Windows, *j_Window, *j_CaretRect;
    HWND hWnd;
    GUITHREADINFO gti;
    RECT rc;
    LONG_PTR dwpExStyle;
    BYTE bAlpha;

    /* Verify parameters */
    if (OutFile == NULL || *OutFile == UNICODE_NULL)
    {
        return BuildErrorOutput(E_INVALIDARG, "Parameter \"OutFile\" is required.");
    }

    /* Take snapshot */
    j = Util_Gdip_SaveSnapshot(NULL, OutFile);
    if (j != NULL)
    {
        return j;
    }

    /* Get foreground windows information */
    j = cJSON_CreateObject();
    gti.cbSize = sizeof(gti);
    if (GetGUIThreadInfo(0, &gti) && gti.hwndActive != NULL && IsTopLevelWindow(gti.hwndActive))
    {
        j_Active_Window = cJSON_CreateObject();
        Util_Json_AddWindowHandle(j_Active_Window, "handle", gti.hwndActive);
        Util_Json_AddWindowHandle(j_Active_Window, "focus_handle", gti.hwndFocus);
        Util_Json_AddWindowHandle(j_Active_Window, "caret_handle", gti.hwndCaret);
        if (gti.hwndCaret != NULL && Util_Window_GetRoot(gti.hwndCaret) == gti.hwndActive)
        {
            MapWindowPoints(gti.hwndCaret, HWND_DESKTOP, (LPPOINT)&gti.rcCaret, 2);
            j_CaretRect = cJSON_AddObjectToObject(j_Active_Window, "caret_rectangle");
            cJSON_AddNumberToObject(j_CaretRect, "left", gti.rcCaret.left);
            cJSON_AddNumberToObject(j_CaretRect, "top", gti.rcCaret.top);
            cJSON_AddNumberToObject(j_CaretRect, "right", gti.rcCaret.right);
            cJSON_AddNumberToObject(j_CaretRect, "bottom", gti.rcCaret.bottom);
        }
        cJSON_AddItemToObject(j_Active_Window, "elements", Util_UIA_GetWindowElementJson(gti.hwndActive));
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
            (!BooleanFlagOn(dwpExStyle, WS_EX_LAYERED) || !GetLayeredWindowAttributes(hWnd, NULL, &bAlpha, NULL) || bAlpha != 0))
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
