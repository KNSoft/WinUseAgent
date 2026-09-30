#include "pch.h"

static PWSTR Program;
static PWSTR Level;
static PWSTR Arguments;

static WUA_COMMAND_PARAMETER Parameters[] = {
    DEF_PARAMETER_ENTRY(Program, String, TRUE),
    DEF_PARAMETER_ENTRY(Level, String, TRUE),
    DEF_PARAMETER_ENTRY(Arguments, Arguments, FALSE)
};

WUA_COMMAND_FN Command;
WUA_COMMAND Run_Restrict = { Parameters, ARRAYSIZE(Parameters), &Command };

static CONST SID AuthUsersSid = SID_AUTHENTICATED_USERS;
static CONST SID_2 AdminsSid = SID_BUILTIN_ADMINISTRATORS;

static
NTSTATUS
CreateFilteredToken(
    _In_ LOGICAL DisableAuthUsers,
    _Out_ PHANDLE FilteredToken)
{
    NTSTATUS Status;
    HANDLE Token;
    ULONG Flags;

#define GROUP_COUNT 2
    DEFINE_ANYSIZE_STRUCT(DisableGroups, TOKEN_GROUPS, SID_AND_ATTRIBUTES, GROUP_COUNT) = {
        GROUP_COUNT,
        { (PSID)&AdminsSid, SE_GROUP_MANDATORY | SE_GROUP_USE_FOR_DENY_ONLY},
        {
            { (PSID)&AuthUsersSid, SE_GROUP_MANDATORY | SE_GROUP_USE_FOR_DENY_ONLY }
        }
    };
    _STATIC_ASSERT(ARRAYSIZE(DisableGroups.Array) == GROUP_COUNT - 1);
#undef GROUP_COUNT
    if (DisableAuthUsers)
    {
        Flags = DISABLE_MAX_PRIVILEGE;
    } else
    {
        DisableGroups.BaseType.GroupCount = 1;
        Flags = 0;
    }

    Status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_ALL_ACCESS, &Token);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = NtFilterToken(Token,
                           Flags,
                           &DisableGroups.BaseType,
                           NULL,
                           NULL,
                           FilteredToken);
    NtClose(Token);
    return Status;
}

static
_Function_class_(WUA_COMMAND_FN)
_Ret_maybenull_
IJsonObject*
Command(VOID)
{
    IJsonObject* j;
    LOGICAL DisableAuthUsers;
    HANDLE Token;
    PROCESS_INFORMATION pi;
    PWSTR CommandLine;
    STARTUPINFOW si;
    NTSTATUS Status;

    if (Program == NULL || *Program == UNICODE_NULL)
    {
        return BuildErrorOutput(E_INVALIDARG, "Parameter \"Program\" is required.");
    }

    CommandLine = BuildCommandLineWithProgram(Program, Arguments);
    if (CommandLine == NULL)
    {
        return BuildErrorOutput(E_OUTOFMEMORY, "Failed to allocate command line.");
    }
    if (_wcsicmp(Level, L"AuthenticatedUsers") == 0)
    {
        DisableAuthUsers = FALSE;
    } else if (_wcsicmp(Level, L"Users") == 0)
    {
        DisableAuthUsers = TRUE;
    } else
    {
        Mem_Free(CommandLine);
        return BuildErrorOutput(E_INVALIDARG, "Parameter \"Level\" should be \"AuthenticatedUsers\" or \"Users\".");
    }

    Status = CreateFilteredToken(DisableAuthUsers, &Token);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(CommandLine);
        return BuildErrorOutput(HRESULT_FROM_NT(Status), "CreateFilteredToken failed.");
    }

    RtlZeroMemory(&si, sizeof(si));
    RtlZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    if (CreateProcessAsUserW(Token,
                             Program,
                             CommandLine,
                             NULL,
                             NULL,
                             FALSE,
                             NORMAL_PRIORITY_CLASS | CREATE_NEW_CONSOLE | CREATE_DEFAULT_ERROR_MODE,
                             NULL,
                             NULL,
                             &si,
                             &pi))
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        j = BuildSuccessOutput(NULL);
    } else
    {
        j = BuildErrorOutput(HRESULT_FROM_WIN32(Err_GetLastError()), "CreateProcessAsUserW failed.");
    }
    CloseHandle(Token);
    Mem_Free(CommandLine);

    return j;
}
