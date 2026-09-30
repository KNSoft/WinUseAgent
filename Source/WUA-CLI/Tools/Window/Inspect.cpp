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
_Ret_maybenull_
IJsonObject*
Command(VOID)
{
    IJsonObject *j, *j_Active_Window = NULL, *j_UIA;
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
    j = Util_Json_CreateObject();
    gti.cbSize = sizeof(gti);
    if (GetGUIThreadInfo(0, &gti) && gti.hwndActive == hWnd)
    {
        j_Active_Window = Util_Window_GetGUIInfoJson(&gti, hWnd);
    }
    if (j_Active_Window != NULL)
    {
        Util_Json_AddItemToObject(j, L"active_window", j_Active_Window);
        j_Active_Window->Release();
    } else
    {
        Util_Json_AddNullToObject(j, L"active_window");
    }

    /* Get UIA elements */
    j_UIA = Util_UIA_GetWindowElementJson(hWnd);
    if (j_UIA != NULL)
    {
        Util_Json_AddItemToObject(j, L"uia_tree", j_UIA);
        j_UIA->Release();
    } else
    {
        Util_Json_AddNullToObject(j, L"uia_tree");
    }

    return BuildSuccessOutput(j);
}
