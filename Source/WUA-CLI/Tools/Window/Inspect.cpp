#include "pch.h"

static ULONG Handle;
static PWSTR OutFile;

static WUA_COMMAND_PARAMETER Parameters[] = {
    DEF_PARAMETER_ENTRY(Handle, HexU32, FALSE),
    DEF_PARAMETER_ENTRY(OutFile, String, TRUE),
};

WUA_COMMAND_FN Command;
WUA_COMMAND Window_Inspect = { Parameters, ARRAYSIZE(Parameters), &Command };

static
_Function_class_(WUA_COMMAND_FN)
_Ret_notnull_
cJSON*
Command(VOID)
{
    cJSON *j;
    HWND hWnd;

    /* Verify parameters */
    hWnd = reinterpret_cast<HWND>(UI_32ToHandle(Handle));
    if (hWnd == NULL || !IsTopLevelWindow(hWnd))
    {
        return BuildErrorOutput(E_INVALIDARG, "Parameter \"Handle\" is not a valid top-level window handle.");
    }
    if (OutFile == NULL || *OutFile == UNICODE_NULL)
    {
        return BuildErrorOutput(E_INVALIDARG, "Parameter \"OutFile\" is required.");
    }

    /* Take snapshot */
    j = Util_Gdip_SaveSnapshot(hWnd, OutFile);
    if (j != NULL)
    {
        return j;
    }

    /* Get UIA elements */
    j = cJSON_CreateObject();
    cJSON_AddItemToObject(j, "elements", Util_UIA_GetWindowElementJson(hWnd));

    return BuildSuccessOutput(j);
}
