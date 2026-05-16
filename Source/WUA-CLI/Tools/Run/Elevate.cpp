#include "pch.h"

static PWSTR Program;
static PWSTR Arguments;

static WUA_COMMAND_PARAMETER Parameters[] = {
    DEF_PARAMETER_ENTRY(Program, String, TRUE),
    DEF_PARAMETER_ENTRY(Arguments, Arguments, FALSE)
};

WUA_COMMAND_FN Command;
WUA_COMMAND Run_Elevate = { Parameters, ARRAYSIZE(Parameters), &Command };

static
_Function_class_(WUA_COMMAND_FN)
_Ret_notnull_
cJSON*
Command(VOID)
{
    cJSON* j;
    W32ERROR Error;

    if (Program == NULL || *Program == UNICODE_NULL)
    {
        return BuildErrorOutput(E_INVALIDARG, "Parameter \"Program\" is required.");
    }

    Error = Shell_Exec(Program, Arguments, L"runas", SW_SHOWNORMAL, NULL);
    if (Error == ERROR_SUCCESS)
    {
        j = BuildSuccessOutput(NULL);
    } else
    {
        j = BuildErrorOutput(HRESULT_FROM_WIN32(Error), "Shell_Exec failed with error %lu.", Error);
    }
    return j;
}
