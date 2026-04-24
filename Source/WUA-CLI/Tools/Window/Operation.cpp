#include "pch.h"

static ULONG Handle;
static PWSTR Verb;

static WUA_COMMAND_PARAMETER Parameters[] = {
    DEF_PARAMETER_ENTRY(Handle, HexU32, TRUE),
    DEF_PARAMETER_ENTRY(Verb, String, TRUE),
};

WUA_COMMAND_FN Command;
WUA_COMMAND Window_Operation = { Parameters, ARRAYSIZE(Parameters), &Command };

static
_Function_class_(WUA_COMMAND_FN)
_Ret_notnull_
cJSON*
Command(VOID)
{
    HWND hWnd = reinterpret_cast<HWND>(UI_32ToHandle(Handle));
    if (!IsWindow(hWnd))
    {
        return BuildErrorOutput(E_INVALIDARG, "Parameter \"Handle\" is not a valid window handle.");
    }
    _Analysis_assume_(hWnd != NULL);

    if (_wcsicmp(Verb, L"Active") == 0)
    {
        ShowWindow(hWnd, SW_RESTORE);
        if (Util_Window_IsCloaked(hWnd))
        {
            Util_Window_Uncloake(hWnd);
        }
        Util_Window_Active(hWnd);
    } else if (_wcsicmp(Verb, L"Minimize") == 0)
    {
        ShowWindow(hWnd, SW_MINIMIZE);
    } else if (_wcsicmp(Verb, L"Maximize") == 0)
    {
        ShowWindow(hWnd, SW_MAXIMIZE);
        Util_Window_Active(hWnd);
    } else
    {
        return BuildErrorOutput(E_INVALIDARG, "Parameter \"Verb\" is invalid.");
    }
    return BuildSuccessOutput(NULL);
}
