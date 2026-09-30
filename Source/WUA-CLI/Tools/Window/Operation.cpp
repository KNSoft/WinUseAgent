#include "pch.h"

static ULONG Handle;
static PWSTR Action;

static WUA_COMMAND_PARAMETER Parameters[] = {
    DEF_PARAMETER_ENTRY(Handle, HexU32, TRUE),
    DEF_PARAMETER_ENTRY(Action, String, TRUE),
};

WUA_COMMAND_FN Command;
WUA_COMMAND Window_Operation = { Parameters, ARRAYSIZE(Parameters), &Command };

static
_Function_class_(WUA_COMMAND_FN)
_Ret_maybenull_
IJsonObject*
Command(VOID)
{
    HWND hWnd = reinterpret_cast<HWND>(UI_32ToHandle(Handle));
    if (!IsWindow(hWnd))
    {
        return BuildErrorOutput(E_INVALIDARG, "Parameter \"Handle\" is not a valid window handle.");
    }
    _Analysis_assume_(hWnd != NULL);

    if (_wcsicmp(Action, L"Activate") == 0)
    {
        ShowWindow(hWnd, SW_RESTORE);
        if (Util_Window_IsCloaked(hWnd))
        {
            Util_Window_Uncloake(hWnd);
        }
        Util_Window_Active(hWnd);
        if (GetForegroundWindow() != hWnd)
        {
            return BuildErrorOutput(E_FAIL, "Failed to activate the window.");
        }
    } else if (_wcsicmp(Action, L"Minimize") == 0)
    {
        ShowWindow(hWnd, SW_MINIMIZE);
    } else if (_wcsicmp(Action, L"Maximize") == 0)
    {
        ShowWindow(hWnd, SW_MAXIMIZE);
        Util_Window_Active(hWnd);
    } else
    {
        return BuildErrorOutput(E_INVALIDARG, "Parameter \"Action\" is invalid.");
    }
    return BuildSuccessOutput(NULL);
}
