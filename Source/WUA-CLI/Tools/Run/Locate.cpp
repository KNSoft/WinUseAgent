#include "pch.h"

static PWSTR Path;

static WUA_COMMAND_PARAMETER Parameters[] = {
    DEF_PARAMETER_ENTRY(Path, String, TRUE)
};

WUA_COMMAND_FN Command;
WUA_COMMAND Run_Locate = { Parameters, ARRAYSIZE(Parameters), &Command };

static
_Function_class_(WUA_COMMAND_FN)
_Ret_maybenull_
IJsonObject*
Command(VOID)
{
    IJsonObject* j;
    HRESULT Hr;

    if (Path == NULL || *Path == UNICODE_NULL)
    {
        return BuildErrorOutput(E_INVALIDARG, "Parameter \"Path\" is required.");
    }

    Hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(Hr))
    {
        return BuildErrorOutput(Hr, "CoInitializeEx failed.");
    }

    Hr = Shell_LocateItem(Path);
    if (FAILED(Hr))
    {
        j = BuildErrorOutput(Hr, "Shell_LocateItem failed.");
    } else
    {
        j = BuildSuccessOutput(NULL);
    }
    CoUninitialize();
    return j;
}
