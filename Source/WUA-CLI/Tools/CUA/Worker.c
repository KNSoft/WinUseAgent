#include "pch.h"
#include "Worker.h"
#include <shellapi.h>
#include <wbemidl.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winsta.lib")
#pragma comment(lib, "wbemuuid.lib")


static
HRESULT
CheckLauncherProcess(
    _In_ HANDLE Process,
    _In_ PCWSTR ExpectedSid,
    _Out_ PLOGICAL Rejected);

static
HRESULT
PutNumber(
    _In_ IWbemClassObject* Object,
    _In_ PCWSTR Name,
    _In_ LONG Number)
{
    VARIANT Value = { 0 };
    Value.vt = VT_I4;
    Value.lVal = Number;
    return Object->lpVtbl->Put(Object, Name, 0, &Value, 0);
}

static
HRESULT
PutString(
    _In_ IWbemClassObject* Object,
    _In_ PCWSTR Name,
    _In_ PCWSTR Text)
{
    VARIANT Value = { 0 };
    Value.vt = VT_BSTR;
    Value.bstrVal = SysAllocString(Text);
    if (Value.bstrVal == NULL)
    {
        return E_OUTOFMEMORY;
    }
    HRESULT Hr = Object->lpVtbl->Put(Object, Name, 0, &Value, 0);
    VariantClear(&Value);
    return Hr;
}

static
HRESULT
GetClass(
    _In_ IWbemServices* Services,
    _In_ PCWSTR Name,
    _Outptr_ IWbemClassObject** Object)
{
    BSTR Path = SysAllocString(Name);
    *Object = NULL;
    if (Path == NULL)
    {
        return E_OUTOFMEMORY;
    }
    HRESULT Hr = Services->lpVtbl->GetObject(Services, Path, 0, NULL, Object, NULL);
    SysFreeString(Path);
    return Hr;
}

static
HRESULT
LaunchOutsideJob(
    _In_ DWORD SessionId,
    _In_ PCWSTR ExpectedSid,
    _Out_ PHANDLE Process,
    _Out_ PLOGICAL Submitted)
{
    PCWSTR Executable = NtCurrentPeb()->ProcessParameters->ImagePathName.Buffer;
    IWbemLocator* Locator = NULL;
    IWbemServices* Services = NULL;
    IWbemClassObject *ProcessClass = NULL, *StartupClass = NULL, *Definition = NULL;
    IWbemClassObject *Input = NULL, *Startup = NULL, *Output = NULL;
    IWbemCallResult* Call = NULL;
    BSTR Namespace = NULL, ClassName = NULL, Method = NULL;
    VARIANT StartupValue = { 0 }, Code = { 0 }, Pid = { 0 };
    WCHAR Id[11];
    PCWSTR Arguments[] = { Executable, L"CUA", L"SessionLauncher", Id, ExpectedSid };
    PWSTR CommandLine = NULL;
    NTSTATUS Status;
    LONG CallStatus;
    LOGICAL Rejected;
    HRESULT Hr;
    *Process = NULL;
    *Submitted = FALSE;
    Hr = CoCreateInstance(&CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER, &IID_IWbemLocator, (PVOID*)&Locator);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Namespace = SysAllocString(L"ROOT\\CIMV2");
    ClassName = SysAllocString(L"Win32_Process");
    Method = SysAllocString(L"Create");
    if (Namespace == NULL || ClassName == NULL || Method == NULL)
    {
        Hr = E_OUTOFMEMORY;
        goto Exit;
    }
    Hr = Locator->lpVtbl->ConnectServer(Locator, Namespace, NULL, NULL, NULL, WBEM_FLAG_CONNECT_USE_MAX_WAIT,
                                                NULL, NULL, &Services);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = CoSetProxyBlanket((IUnknown*)Services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
                                   RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = GetClass(Services, L"Win32_Process", &ProcessClass);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = GetClass(Services, L"Win32_ProcessStartup", &StartupClass);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = ProcessClass->lpVtbl->GetMethod(ProcessClass, L"Create", 0, &Definition, NULL);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Definition->lpVtbl->SpawnInstance(Definition, 0, &Input);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = StartupClass->lpVtbl->SpawnInstance(StartupClass, 0, &Startup);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = PutNumber(Startup, L"ShowWindow", SW_HIDE);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = PutNumber(Startup, L"CreateFlags", CREATE_BREAKAWAY_FROM_JOB | CREATE_NO_WINDOW);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Str_PrintfW(Id, L"%lu", SessionId);
    Status = PS_ArgvToCommandLineW(ARRAYSIZE(Arguments), Arguments, &CommandLine);
    if (Status != STATUS_SUCCESS)
    {
        Hr = Err_NtStatusToHr(Status);
        goto Exit;
    }
    Hr = PutString(Input, L"CommandLine", CommandLine);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    StartupValue.vt = VT_UNKNOWN;
    StartupValue.punkVal = (IUnknown*)Startup;
    Hr = Input->lpVtbl->Put(Input, L"ProcessStartupInformation", 0, &StartupValue, 0);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    *Submitted = TRUE;
    Hr = Services->lpVtbl->ExecMethod(Services, ClassName, Method, WBEM_FLAG_RETURN_IMMEDIATELY, NULL, Input,
                                              NULL, &Call);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Call->lpVtbl->GetCallStatus(Call, 10000, &CallStatus);
    if (Hr == WBEM_S_TIMEDOUT)
    {
        Hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        goto Exit;
    }
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = CallStatus;
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Call->lpVtbl->GetResultObject(Call, 0, &Output);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Output->lpVtbl->Get(Output, L"ReturnValue", 0, &Code, NULL, NULL);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Code.vt != VT_I4)
    {
        Hr = E_UNEXPECTED;
        goto Exit;
    }
    if (Code.lVal != 0)
    {
        *Submitted = FALSE;
        Hr = E_FAIL;
        goto Exit;
    }
    Hr = Output->lpVtbl->Get(Output, L"ProcessId", 0, &Pid, NULL, NULL);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Pid.vt != VT_I4 || Pid.ulVal == 0)
    {
        Hr = E_UNEXPECTED;
        goto Exit;
    }
    Status = PS_OpenProcess(Process, SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, Pid.ulVal);
    if (Status != STATUS_SUCCESS)
    {
        Hr = Err_NtStatusToHr(Status);
        goto Exit;
    }
    Hr = CheckLauncherProcess(*Process, ExpectedSid, &Rejected);
    if (FAILED(Hr) && Rejected != FALSE)
    {
        *Submitted = FALSE;
    }
Exit:
    if (FAILED(Hr) && *Process != NULL)
    {
        NtClose(*Process);
        *Process = NULL;
    }
    VariantClear(&Code);
    VariantClear(&Pid);
    SysFreeString(Namespace);
    SysFreeString(ClassName);
    SysFreeString(Method);
    if (CommandLine != NULL)
    {
        PS_FreeCommandLineBuffer(CommandLine);
    }
    if (Output != NULL)
    {
        Output->lpVtbl->Release(Output);
    }
    if (Call != NULL)
    {
        Call->lpVtbl->Release(Call);
    }
    if (Startup != NULL)
    {
        Startup->lpVtbl->Release(Startup);
    }
    if (Input != NULL)
    {
        Input->lpVtbl->Release(Input);
    }
    if (Definition != NULL)
    {
        Definition->lpVtbl->Release(Definition);
    }
    if (StartupClass != NULL)
    {
        StartupClass->lpVtbl->Release(StartupClass);
    }
    if (ProcessClass != NULL)
    {
        ProcessClass->lpVtbl->Release(ProcessClass);
    }
    if (Services != NULL)
    {
        Services->lpVtbl->Release(Services);
    }
    if (Locator != NULL)
    {
        Locator->lpVtbl->Release(Locator);
    }
    return Hr;
}

static
HRESULT
QueryTokenSid(
    _In_ HANDLE Token,
    _Out_writes_bytes_(SECURITY_MAX_SID_SIZE) PSID Sid)
{
    BYTE Buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    ULONG Returned;
    RtlZeroMemory(Sid, SECURITY_MAX_SID_SIZE);
    NTSTATUS Status = NtQueryInformationToken(Token, TokenUser, Buffer, sizeof(Buffer), &Returned);
    if (!NT_SUCCESS(Status))
    {
        return Err_NtStatusToHr(Status);
    }
    Status = RtlCopySid(SECURITY_MAX_SID_SIZE, Sid, ((TOKEN_USER*)Buffer)->User.Sid);
    return Err_NtStatusToHr(Status);
}

static
HRESULT
CheckLauncherProcess(
    _In_ HANDLE Process,
    _In_ PCWSTR ExpectedSid,
    _Out_ PLOGICAL Rejected)
{
    HANDLE Token = NULL;
    BYTE Sid[SECURITY_MAX_SID_SIZE];
    UNICODE_STRING Actual = { 0 }, Expected;
    ULONG Session, Returned;
    NTSTATUS Status;
    HRESULT Hr;
    *Rejected = FALSE;
    Status = NtOpenProcessToken(Process, TOKEN_QUERY, &Token);
    if (Status != STATUS_SUCCESS)
    {
        Hr = Err_NtStatusToHr(Status);
        goto Exit;
    }
    Hr = QueryTokenSid(Token, (PSID)Sid);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Status = RtlConvertSidToUnicodeString(&Actual, (PSID)Sid, TRUE);
    if (!NT_SUCCESS(Status))
    {
        Hr = Err_NtStatusToHr(Status);
        goto Exit;
    }
    RtlInitUnicodeString(&Expected, ExpectedSid);
    Status = NtQueryInformationToken(Token, TokenSessionId, &Session, sizeof(Session), &Returned);
    if (Status != STATUS_SUCCESS)
    {
        Hr = Err_NtStatusToHr(Status);
        goto Exit;
    }
    if (RtlEqualUnicodeString(&Actual, &Expected, TRUE) == FALSE || Session != NtCurrentPeb()->SessionId)
    {
        *Rejected = TRUE;
        Hr = E_ACCESSDENIED;
    }
Exit:
    if (Actual.Buffer != NULL)
    {
        RtlFreeUnicodeString(&Actual);
    }
    if (Token != NULL)
    {
        NtClose(Token);
    }
    return Hr;
}

static
HRESULT
QueryAdministratorMember(
    _In_ HANDLE Token,
    _Out_ LOGICAL* Member)
{
    SID_2 Administrators = SID_BUILTIN_ADMINISTRATORS;
    PTOKEN_GROUPS Groups = NULL;
    NTSTATUS Status = PS_GetTokenInfo(Token, TokenGroups, (PVOID*)&Groups);
    *Member = FALSE;
    if (!NT_SUCCESS(Status))
    {
        return Err_NtStatusToHr(Status);
    }
    for (ULONG Index = 0; Index < Groups->GroupCount; ++Index)
    {
        if (RtlEqualSid(Groups->Groups[Index].Sid, &Administrators.BaseType) != FALSE)
        {
            *Member = TRUE;
            break;
        }
    }
    Mem_Free(Groups);
    return S_OK;
}

static
HRESULT
CheckSession(
    _In_ DWORD CurrentSession,
    _In_ DWORD SessionId)
{
    ULONG Parent, Child;
    if (SessionId == 0)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_LOGGED_ON);
    }
    if (SessionId == CurrentSession)
    {
        return S_OK;
    }
    if (WinStationGetParentSessionId(SessionId, &Parent) == FALSE || WinStationGetChildSessionId(&Child) == FALSE)
    {
        return HRESULT_FROM_WIN32(Err_GetLastError());
    }
    return Parent == CurrentSession && Child == SessionId ? S_OK : E_ACCESSDENIED;
}

static
HRESULT
LaunchCurrent(
    _Out_ PHANDLE Process)
{
    PCWSTR Executable = NtCurrentPeb()->ProcessParameters->ImagePathName.Buffer;
    PCWSTR Arguments[] = { Executable, L"CUA", L"Server" };
    PWSTR CommandLine = NULL;
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Started = { 0 };
    WCHAR Desktop[] = L"winsta0\\default";
    NTSTATUS Status = PS_ArgvToCommandLineW(ARRAYSIZE(Arguments), Arguments, &CommandLine);
    HRESULT Hr = Err_NtStatusToHr(Status);
    *Process = NULL;
    if (!NT_SUCCESS(Status))
    {
        return Hr;
    }
    Startup.lpDesktop = Desktop;
    Startup.dwFlags = STARTF_USESHOWWINDOW;
    Startup.wShowWindow = SW_HIDE;
    Hr = HRESULT_FROM_WIN32(
        PS_CreateProcess(NULL, Executable, CommandLine, FALSE, CREATE_NO_WINDOW, NULL, &Startup, &Started));
    if (SUCCEEDED(Hr))
    {
        NtClose(Started.hThread);
        *Process = Started.hProcess;
        Hr = S_OK;
    }
    PS_FreeCommandLineBuffer(CommandLine);
    return Hr;
}

static
HRESULT
LaunchElevated(
    _In_ DWORD SessionId,
    _In_ PCWSTR ExpectedSid,
    _Out_ PHANDLE Process,
    _Out_ PLOGICAL Submitted)
{
    PCWSTR Executable = NtCurrentPeb()->ProcessParameters->ImagePathName.Buffer;
    WCHAR Id[11];
    PCWSTR Arguments[] = { L"CUA", L"SessionLauncher", Id, ExpectedSid };
    PWSTR Parameters = NULL;
    SHELLEXECUTEINFOW Execute = { sizeof(Execute) };
    LOGICAL Rejected;
    NTSTATUS Status;
    HRESULT Hr;
    *Process = NULL;
    *Submitted = FALSE;
    Str_PrintfW(Id, L"%lu", SessionId);
    Status = PS_ArgvToCommandLineW(ARRAYSIZE(Arguments), Arguments, &Parameters);
    if (!NT_SUCCESS(Status))
    {
        return Err_NtStatusToHr(Status);
    }
    Execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    Execute.lpVerb = L"runas";
    Execute.lpFile = Executable;
    Execute.lpParameters = Parameters;
    Execute.nShow = SW_HIDE;
    Hr = ShellExecuteExW(&Execute) ? S_OK : HRESULT_FROM_WIN32(Err_GetLastError());
    PS_FreeCommandLineBuffer(Parameters);
    if (FAILED(Hr))
    {
        return Hr;
    }
    *Submitted = TRUE;
    if (Execute.hProcess == NULL)
    {
        return E_UNEXPECTED;
    }
    Hr = CheckLauncherProcess(Execute.hProcess, ExpectedSid, &Rejected);
    if (FAILED(Hr))
    {
        if (Rejected != FALSE)
        {
            *Submitted = FALSE;
        }
        NtClose(Execute.hProcess);
    } else
    {
        *Process = Execute.hProcess;
        Hr = S_OK;
    }
    return Hr;
}

static
HRESULT
LaunchWithToken(
    _In_ DWORD SessionId,
    _In_ const WUA_WORKER_IDENTITY* Identity,
    _In_ LOGICAL Privileged,
    _Out_ PHANDLE Process,
    _Out_ PCWSTR* FailureText)
{
    PCWSTR Executable = NtCurrentPeb()->ProcessParameters->ImagePathName.Buffer;
    HANDLE ProcessToken = NULL, DebugToken = NULL, SystemToken = NULL, UserToken = NULL, WorkerToken = NULL;
    TOKEN_LINKED_TOKEN Linked = { 0 };
    TOKEN_ELEVATION_TYPE ElevationType;
    HANDLE Source;
    BYTE Sid[SECURITY_MAX_SID_SIZE];
    ULONG LsaId, Returned, UiAccess = TRUE, TokenSession;
    NTSTATUS Status;
    HRESULT Hr = S_OK;
    PWSTR CommandLine = NULL;
    PCWSTR Arguments[] = { Executable, L"CUA", L"Server" };
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Started = { 0 };
    WCHAR Desktop[] = L"winsta0\\default";
    LOGICAL Impersonating = FALSE;
    const ULONG Privileges[] = { SE_ASSIGNPRIMARYTOKEN_PRIVILEGE, SE_INCREASE_QUOTA_PRIVILEGE, SE_TCB_PRIVILEGE };

    *Process = NULL;
    *FailureText = L"Unable to open the caller's process token.";
    Status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &ProcessToken);
    if (Status != STATUS_SUCCESS)
    {
        Hr = Err_NtStatusToHr(Status);
        goto Exit;
    }
    if (Privileged != FALSE)
    {
        *FailureText = L"Unable to acquire privileges for UIAccess and session startup.";
        Status = PS_DuplicateToken(ProcessToken, TokenImpersonation, &DebugToken);
        if (Status != STATUS_SUCCESS)
        {
            Hr = Err_NtStatusToHr(Status);
            goto Exit;
        }
        Status = NT_AdjustTokenPrivilege(DebugToken, SE_DEBUG_PRIVILEGE, SE_PRIVILEGE_ENABLED);
        if (Status != STATUS_SUCCESS)
        {
            Hr = Err_NtStatusToHr(Status);
            goto Exit;
        }
        Status = PS_Impersonate(DebugToken);
        if (Status != STATUS_SUCCESS)
        {
            Hr = Err_NtStatusToHr(Status);
            goto Exit;
        }
        Impersonating = TRUE;
        Status = Sys_GetLsaProcessId(&LsaId);
        if (Status != STATUS_SUCCESS)
        {
            Hr = Err_NtStatusToHr(Status);
            goto Exit;
        }
        Status = PS_DuplicateSystemToken(LsaId, TokenImpersonation, &SystemToken);
        if (Status != STATUS_SUCCESS)
        {
            Hr = Err_NtStatusToHr(Status);
            goto Exit;
        }
        Status = PS_Impersonate(SystemToken);
        if (Status != STATUS_SUCCESS)
        {
            Hr = Err_NtStatusToHr(Status);
            goto Exit;
        }
        for (ULONG Index = 0; Index < ARRAYSIZE(Privileges); ++Index)
        {
            Status = NT_AdjustTokenPrivilege(SystemToken, Privileges[Index], SE_PRIVILEGE_ENABLED);
            if (Status != STATUS_SUCCESS)
            {
                Hr = Err_NtStatusToHr(Status);
                goto Exit;
            }
        }
    }
    Source = ProcessToken;
    if (SessionId != Identity->SessionId)
    {
        *FailureText = L"Unable to obtain the logged-on session's user token.";
        Hr = HRESULT_FROM_WIN32(Sys_GetSessionToken(SessionId, &UserToken));
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Source = UserToken;
        if (Privileged != FALSE)
        {
            Hr = Err_NtStatusToHr(NtQueryInformationToken(UserToken, TokenElevationType, &ElevationType,
                                                         sizeof(ElevationType), &Returned));
            if (SUCCEEDED(Hr) && ElevationType == TokenElevationTypeLimited)
            {
                Status = NtQueryInformationToken(UserToken, TokenLinkedToken, &Linked, sizeof(Linked), &Returned);
                if (NT_SUCCESS(Status))
                {
                    Source = Linked.LinkedToken;
                }
            }
        }
    }
    *FailureText = L"The session token does not belong to the caller's account and target session.";
    Hr = QueryTokenSid(Source, (PSID)Sid);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Status = NtQueryInformationToken(Source, TokenSessionId, &TokenSession, sizeof(TokenSession), &Returned);
    if (Status != STATUS_SUCCESS)
    {
        Hr = Err_NtStatusToHr(Status);
        goto Exit;
    }
    if (RtlEqualSid((PSID)Sid, (PSID)Identity->Sid) == FALSE || TokenSession != SessionId)
    {
        Hr = E_ACCESSDENIED;
        goto Exit;
    }
    *FailureText = L"Unable to duplicate the session user's primary token.";
    Status = PS_DuplicateToken(Source, TokenPrimary, &WorkerToken);
    if (Status != STATUS_SUCCESS)
    {
        Hr = Err_NtStatusToHr(Status);
        goto Exit;
    }
    if (Privileged != FALSE)
    {
        // UIAccess is best-effort; process creation retains the available user token on failure.
        NtSetInformationToken(WorkerToken, TokenUIAccess, &UiAccess, sizeof(UiAccess));
    }
    *FailureText = L"Unable to build the CUA Server command line.";
    Status = PS_ArgvToCommandLineW(ARRAYSIZE(Arguments), Arguments, &CommandLine);
    if (Status != STATUS_SUCCESS)
    {
        Hr = Err_NtStatusToHr(Status);
        goto Exit;
    }
    Startup.lpDesktop = Desktop;
    Startup.dwFlags = STARTF_USESHOWWINDOW;
    Startup.wShowWindow = SW_HIDE;
    *FailureText = L"Unable to start the CUA Server in the target session.";
    Hr = HRESULT_FROM_WIN32(
        PS_CreateProcess(WorkerToken, Executable, CommandLine, FALSE, CREATE_NO_WINDOW, NULL, &Startup, &Started));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    NtClose(Started.hThread);
    *Process = Started.hProcess;
Exit:
    if (Impersonating != FALSE && !NT_SUCCESS(PS_Impersonate(NULL)))
    {
        NtTerminateProcess(NtCurrentProcess(), STATUS_CANNOT_IMPERSONATE);
    }
    if (CommandLine != NULL)
    {
        PS_FreeCommandLineBuffer(CommandLine);
    }
    if (WorkerToken != NULL)
    {
        NtClose(WorkerToken);
    }
    if (Linked.LinkedToken != NULL)
    {
        NtClose(Linked.LinkedToken);
    }
    if (UserToken != NULL)
    {
        NtClose(UserToken);
    }
    if (SystemToken != NULL)
    {
        NtClose(SystemToken);
    }
    if (DebugToken != NULL)
    {
        NtClose(DebugToken);
    }
    if (ProcessToken != NULL)
    {
        NtClose(ProcessToken);
    }
    return Hr;
}

static
HRESULT
LaunchWorker(
    _In_ DWORD SessionId,
    _In_ LOGICAL AllowElevation,
    _Out_ PHANDLE Process,
    _Out_ PCWSTR* FailureText)
{
    WUA_WORKER_IDENTITY Identity;
    UNICODE_STRING Sid = { 0 };
    NTSTATUS Status = STATUS_SUCCESS;
    HRESULT Hr;
    *Process = NULL;
    *FailureText = L"Unable to inspect the caller's identity.";
    Hr = QueryWorkerIdentity(&Identity);
    if (FAILED(Hr))
    {
        if (NtCurrentPeb()->SessionId == SessionId && SessionId != 0)
        {
            return LaunchCurrent(Process);
        }
        return Hr;
    }
    *FailureText = L"The target session is not the caller's current or child session.";
    Hr = CheckSession(Identity.SessionId, SessionId);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (AllowElevation != FALSE && SessionId == Identity.SessionId && Identity.AdministratorMember != FALSE &&
        Identity.Administrator == FALSE)
    {
        Status = RtlConvertSidToUnicodeString(&Sid, (PSID)Identity.Sid, TRUE);
        if (NT_SUCCESS(Status) && Sid.Buffer != NULL)
        {
            LOGICAL Submitted;
            Hr = LaunchElevated(SessionId, Sid.Buffer, Process, &Submitted);
            if (SUCCEEDED(Hr) || Submitted != FALSE)
            {
                if (FAILED(Hr))
                {
                    *FailureText = L"Launcher outcome is unknown; check Session List before starting another Server.";
                }
                goto Exit;
            }
        } else
        {
            Hr = Err_NtStatusToHr(Status);
        }
    }
    if (AllowElevation != FALSE && NtIsProcessInJob(NtCurrentProcess(), NULL) == STATUS_PROCESS_IN_JOB)
    {
        if (Sid.Buffer == NULL)
        {
            Status = RtlConvertSidToUnicodeString(&Sid, (PSID)Identity.Sid, TRUE);
        }
        if (NT_SUCCESS(Status) && Sid.Buffer != NULL)
        {
            LOGICAL Submitted;
            Hr = LaunchOutsideJob(SessionId, Sid.Buffer, Process, &Submitted);
            if (SUCCEEDED(Hr) || Submitted != FALSE)
            {
                if (FAILED(Hr))
                {
                    *FailureText = L"Launcher outcome is unknown; check Session List before starting another Server.";
                }
                goto Exit;
            }
        }
    }
    if (Identity.Administrator != FALSE && (SessionId != Identity.SessionId || Identity.UiAccess == FALSE))
    {
        Hr = LaunchWithToken(SessionId, &Identity, TRUE, Process, FailureText);
        if (SUCCEEDED(Hr) ||
            (SessionId != Identity.SessionId && Hr == HRESULT_FROM_WIN32(ERROR_NO_TOKEN)))
        {
            goto Exit;
        }
    }
    if (SessionId == Identity.SessionId)
    {
        *FailureText = L"CreateProcess failed to start the CUA Server in the current session.";
        Hr = LaunchCurrent(Process);
    } else
    {
        Hr = LaunchWithToken(SessionId, &Identity, FALSE, Process, FailureText);
    }
Exit:
    if (Sid.Buffer != NULL)
    {
        RtlFreeUnicodeString(&Sid);
    }
    return Hr;
}

HRESULT
LaunchServer(
    _In_ DWORD SessionId,
    _Out_ PHANDLE Process,
    _Out_ PCWSTR* FailureText)
{
    return LaunchWorker(SessionId, TRUE, Process, FailureText);
}

int
SessionLauncherMain(
    _In_ DWORD SessionId,
    _In_ PCWSTR ExpectedSid)
{
    HANDLE Server = NULL;
    PROCESS_BASIC_INFORMATION Information;
    NTSTATUS Status;
    UNICODE_STRING Actual = { 0 }, Expected;
    PCWSTR FailureText;
    WUA_WORKER_IDENTITY Identity;
    HRESULT Hr = QueryWorkerIdentity(&Identity);
    if (FAILED(Hr))
    {
        return Hr;
    }
    Status = RtlConvertSidToUnicodeString(&Actual, (PSID)Identity.Sid, TRUE);
    if (!NT_SUCCESS(Status))
    {
        return Err_NtStatusToWin32Error(Status);
    }
    RtlInitUnicodeString(&Expected, ExpectedSid);
    LOGICAL SameAccount = RtlEqualUnicodeString(&Actual, &Expected, TRUE);
    RtlFreeUnicodeString(&Actual);
    if (SameAccount == FALSE)
    {
        return ERROR_ACCESS_DENIED;
    }
    Hr = CheckSession(Identity.SessionId, SessionId);
    if (FAILED(Hr))
    {
        return Hr;
    }
    Hr = LaunchWorker(SessionId, FALSE, &Server, &FailureText);
    if (FAILED(Hr))
    {
        return Hr;
    }
    Status = PS_WaitForObject(Server, INFINITE);
    if (NT_SUCCESS(Status))
    {
        Status = NtQueryInformationProcess(Server, ProcessBasicInformation, &Information, sizeof(Information), NULL);
    }
    NtClose(Server);
    return NT_SUCCESS(Status) ? Information.ExitStatus : Status;
}

HRESULT
QueryWorkerIdentity(
    _Out_ WUA_WORKER_IDENTITY* Identity)
{
    HANDLE Token = NULL, ImpersonationToken = NULL;
    ULONG UiAccess = 0, Returned;
    NTSTATUS Status;
    HRESULT Hr = S_OK;
    RtlZeroMemory(Identity, sizeof(*Identity));
    Status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &Token);
    if (!NT_SUCCESS(Status))
    {
        Status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY, &Token);
        if (Status != STATUS_SUCCESS)
        {
            Hr = Err_NtStatusToHr(Status);
            goto Exit;
        }
    }
    Status = NtQueryInformationToken(Token, TokenSessionId, &Identity->SessionId,
        sizeof(Identity->SessionId), &Returned);
    if (Status != STATUS_SUCCESS)
    {
        Hr = Err_NtStatusToHr(Status);
        goto Exit;
    }
    Hr = QueryTokenSid(Token, (PSID)Identity->Sid);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (NT_SUCCESS(NtQueryInformationToken(Token, TokenUIAccess, &UiAccess, sizeof(UiAccess), &Returned)))
    {
        Identity->UiAccess = UiAccess != 0;
    }
    if (NT_SUCCESS(PS_DuplicateToken(Token, TokenImpersonation, &ImpersonationToken)))
    {
        Identity->Administrator = PS_IsAdminToken(ImpersonationToken) == STATUS_SUCCESS;
    }
    QueryAdministratorMember(Token, &Identity->AdministratorMember);
Exit:
    if (ImpersonationToken != NULL)
    {
        NtClose(ImpersonationToken);
    }
    if (Token != NULL)
    {
        NtClose(Token);
    }
    return Hr;
}

LOGICAL
IsInteractiveWorker(VOID)
{
    HWINSTA CurrentStation = NtUserGetProcessWindowStation();
    HDESK CurrentDesktop = NtUserGetThreadDesktop(HandleToULong(NtCurrentThreadId()));
    WCHAR Station[ARRAYSIZE(L"WinSta0")] = { 0 };
    WCHAR Desktop[ARRAYSIZE(L"Default")] = { 0 };
    USEROBJECTFLAGS Flags = { 0 };
    ULONG Needed = 0;

    return NtCurrentPeb()->SessionId != 0 &&
           NtUserGetObjectInformation(CurrentStation, UOI_FLAGS, &Flags, sizeof(Flags), &Needed) != FALSE &&
           (Flags.dwFlags & WSF_VISIBLE) != 0 &&
           NtUserGetObjectInformation(CurrentStation, UOI_NAME, Station, sizeof(Station), &Needed) != FALSE &&
           NtUserGetObjectInformation(CurrentDesktop, UOI_NAME, Desktop, sizeof(Desktop), &Needed) != FALSE &&
           Str_EqualIW(Station, L"WinSta0") != FALSE && Str_EqualIW(Desktop, L"Default") != FALSE;
}
