#include "pch.h"

static ULONG Handle;
static PWSTR OutFile;

static WUA_COMMAND_PARAMETER Parameters[] = {
    DEF_PARAMETER_ENTRY(Handle, HexU32, FALSE),
    DEF_PARAMETER_ENTRY(OutFile, String, FALSE),
};

WUA_COMMAND_FN Command;
WUA_COMMAND Window_Inspect = { Parameters, ARRAYSIZE(Parameters), &Command };

static
_Function_class_(WUA_COMMAND_FN)
_Ret_notnull_
cJSON*
Command(VOID)
{
    cJSON *j, *j_Active_Window;
    HWND hWnd;
    GUITHREADINFO gti;

    /* Verify parameters */
    hWnd = reinterpret_cast<HWND>(UI_32ToHandle(Handle));
    if (hWnd == NULL || !IsTopLevelWindow(hWnd))
    {
        return BuildErrorOutput(E_INVALIDARG, "Parameter \"Handle\" is not a valid top-level window handle.");
    }

    /* Take snapshot */
    if (OutFile != NULL && *OutFile != UNICODE_NULL)
    {
        j = Util_Gdip_SaveSnapshot(hWnd, OutFile);
        if (j != NULL)
        {
            return j;
        }
    }

    /* Get foreground window information when the inspected window is active */
    j = cJSON_CreateObject();
    gti.cbSize = sizeof(gti);
    if (GetGUIThreadInfo(0, &gti) && gti.hwndActive == hWnd)
    {
        j_Active_Window = Util_Window_GetGUIInfoJson(&gti, hWnd);
    } else
    {
        j_Active_Window = cJSON_CreateNull();
    }
    cJSON_AddItemToObject(j, "active_window", j_Active_Window);

    /* Get UIA elements */
    cJSON_AddItemToObject(j, "uia_tree", Util_UIA_GetWindowElementJson(hWnd));

    return BuildSuccessOutput(j);
}
