#include "pch.h"
#include "Cli.h"
#include "Server.h"
#include "Transport.h"
#include "Worker.h"
#include "../../Tests/TestHost.h"
#include "../../Tests/TestRpc.h"
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

static
HRESULT
BindStringParameter(
    _Inout_ CUA_PARAMETERS* Parameters,
    _In_ CUA_PARAMETER Parameter,
    _In_ PCWSTR Text)
{
    PCWSTR End;
    SIZE_T Length;
    HRESULT Hr;

    if (Parameter == CuaParamKeys)
    {
        for (;;)
        {
            End = wcschr(Text, L'+');
            Length = End != NULL ? (SIZE_T)(End - Text) : wcslen(Text);
            if (Length == 0 || Parameters->KeyCount == CUA_MAX_KEYS)
            {
                return E_INVALIDARG;
            }
            Hr = _Inline_WindowsCreateString(Text, (UINT32)Length, &Parameters->Keys[Parameters->KeyCount]);
            if (FAILED(Hr))
            {
                return Hr;
            }
            Parameters->KeyCount++;
            if (End == NULL)
            {
                break;
            }
            Text = End + 1;
        }
        Parameters->Present |= CUA_PARAM_BIT(Keys);
        return S_OK;
    }
    return CuaSetString(Parameters, Parameter, Text);
}

static
LOGICAL
IsSuccessfulReply(
    _In_ IJsonValue* Reply)
{
    IJsonObject* Object = NULL;
    HSTRING_HEADER Header;
    HSTRING Name;
    boolean Ok = FALSE;
    HRESULT Hr;

    Hr = _Inline_WindowsCreateStringReference(L"ok", 2, &Header, &Name);
    if (SUCCEEDED(Hr))
    {
        Hr = Reply->lpVtbl->GetObject(Reply, &Object);
    }
    if (SUCCEEDED(Hr))
    {
        Hr = Object->lpVtbl->GetNamedBoolean(Object, Name, &Ok);
    }
    if (Object != NULL)
    {
        Object->lpVtbl->Release(Object);
    }
    return SUCCEEDED(Hr) && Ok != FALSE;
}

static
int
ConfigureChildSessionMain(
    _In_ PCWSTR ExpectedSid,
    _In_ DWORD SessionId,
    _In_ PCWSTR Path)
{
    WUA_WORKER_IDENTITY Identity;
    CUA_CHILD_SETTINGS Settings;
    UNICODE_STRING SidText = { 0 };
    UNICODE_STRING Expected;
    HANDLE File = NULL;
    FILE_ATTRIBUTE_TAG_INFORMATION Attributes;
    DECLSPEC_ALIGN(4) BYTE OwnerBuffer[sizeof(SECURITY_DESCRIPTOR) + SECURITY_MAX_SID_SIZE];
    PSID Owner = NULL;
    BOOLEAN Defaulted;
    IO_STATUS_BLOCK IoStatus;
    ULONGLONG Size;
    ULONG Length;
    ULONG Written;
    NTSTATUS Status;
    HRESULT Hr;

    Hr = QueryWorkerIdentity(&Identity);
    if (FAILED(Hr))
    {
        return Hr;
    }
    Status = RtlConvertSidToUnicodeString(&SidText, (PSID)Identity.Sid, TRUE);
    if (!NT_SUCCESS(Status))
    {
        return Err_NtStatusToHr(Status);
    }
    RtlInitUnicodeString(&Expected, ExpectedSid);
    if (RtlEqualUnicodeString(&SidText, &Expected, FALSE) == FALSE || Identity.SessionId != SessionId || SessionId == 0)
    {
        Hr = E_ACCESSDENIED;
        goto Exit;
    }
    Status = IO_CreateWin32File(&File, Path, NULL, FILE_WRITE_DATA | FILE_READ_ATTRIBUTES | READ_CONTROL | SYNCHRONIZE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
                                FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT);
    if (!NT_SUCCESS(Status))
    {
        Hr = Err_NtStatusToHr(Status);
        goto Exit;
    }
    Status = NtQuerySecurityObject(File, OWNER_SECURITY_INFORMATION, OwnerBuffer, sizeof(OwnerBuffer), &Length);
    if (!NT_SUCCESS(Status))
    {
        Hr = Err_NtStatusToHr(Status);
        goto Exit;
    }
    Status = RtlGetOwnerSecurityDescriptor(OwnerBuffer, &Owner, &Defaulted);
    if (!NT_SUCCESS(Status))
    {
        Hr = Err_NtStatusToHr(Status);
        goto Exit;
    }
    if (Owner == NULL || RtlEqualSid(Owner, (PSID)Identity.Sid) == FALSE)
    {
        Hr = E_ACCESSDENIED;
        goto Exit;
    }
    Status = NtQueryInformationFile(File, &IoStatus, &Attributes, sizeof(Attributes), FileAttributeTagInformation);
    if (NT_SUCCESS(Status))
    {
        Status = IO_GetFileSize(File, &Size);
    }
    if (!NT_SUCCESS(Status))
    {
        Hr = Err_NtStatusToHr(Status);
        goto Exit;
    }
    if ((Attributes.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 || Size != 0)
    {
        Hr = E_INVALIDARG;
        goto Exit;
    }
    CuaSessionEnableChildSession(&Settings);
    Status = IO_WriteFile(File, NULL, &Settings, sizeof(Settings), &Written);
    if (!NT_SUCCESS(Status))
    {
        Hr = Err_NtStatusToHr(Status);
        goto Exit;
    }
    if (Written != sizeof(Settings))
    {
        Hr = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
        goto Exit;
    }
    Status = NtFlushBuffersFile(File, &IoStatus);
    Hr = Err_NtStatusToHr(Status);
Exit:
    if (File != NULL)
    {
        NtClose(File);
    }
    RtlFreeUnicodeString(&SidText);
    return FAILED(Hr) ? Hr : 0;
}

static
VOID
MergeSettings(
    _Inout_ CUA_CHILD_SETTINGS* Settings,
    _In_ const CUA_CHILD_SETTINGS* Elevated)
{
    CUA_CHILD_SETTING* Before[] = { &Settings->ChildSessions, &Settings->DefaultCredentials,
                                   &Settings->NtlmDefaultCredentials, &Settings->PasswordLogin };
    const CUA_CHILD_SETTING* After[] = { &Elevated->ChildSessions, &Elevated->DefaultCredentials,
                                        &Elevated->NtlmDefaultCredentials, &Elevated->PasswordLogin };
    LOGICAL Changed;

    for (ULONG Index = 0; Index < ARRAYSIZE(Before); Index++)
    {
        Changed = Before[Index]->Changed != FALSE || After[Index]->Changed != FALSE;
        *Before[Index] = *After[Index];
        Before[Index]->Changed = Changed;
    }
}

static
VOID
TryElevatedSettings(
    _In_ const WUA_WORKER_IDENTITY* Identity,
    _Inout_ CUA_CHILD_SETTINGS* Settings,
    _Out_ PLOGICAL Uncertain)
{
    const CUA_CHILD_SETTING* Items[] = { &Settings->ChildSessions, &Settings->DefaultCredentials,
                                        &Settings->NtlmDefaultCredentials, &Settings->PasswordLogin };
    const UNICODE_STRING TempName = RTL_CONSTANT_STRING(L"TEMP");
    PWSTR Path = NULL;
    WCHAR Id[CUA_GUID_CCH];
    WCHAR Session[11];
    UNICODE_STRING Temp = { 0 };
    UNICODE_STRING Sid = { 0 };
    UNICODE_STRING NtPath = { 0 };
    SECURITY_DESCRIPTOR Descriptor;
    SID SystemSid = SID_LOCAL_SYSTEM;
    DECLSPEC_ALIGN(4)
    BYTE AclBuffer[sizeof(ACL) + 2 * (FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + SECURITY_MAX_SID_SIZE)];
    PACL Acl = (PACL)AclBuffer;
    OBJECT_ATTRIBUTES Object;
    IO_STATUS_BLOCK IoStatus;
    HANDLE File = NULL;
    SHELLEXECUTEINFOW Execute = { sizeof(Execute) };
    PCWSTR Arguments[] = { L"CUA", L"ConfigureChildSession", NULL, Session, NULL };
    PWSTR Parameters = NULL;
    CUA_CHILD_SETTINGS Elevated;
    PROCESS_BASIC_INFORMATION ProcessInfo;
    LARGE_INTEGER WaitTime;
    ULONGLONG Size;
    ULONG Read;
    ULONG Length, Capacity;
    NTSTATUS Status;
    HRESULT Hr;
    LOGICAL Denied = FALSE;

    *Uncertain = FALSE;
    if (Identity->AdministratorMember == FALSE || Identity->Administrator != FALSE)
    {
        return;
    }
    for (ULONG Index = 0; Index < ARRAYSIZE(Items); Index++)
    {
        Denied = Denied != FALSE || Items[Index]->Error == E_ACCESSDENIED ||
                 Items[Index]->Error == HRESULT_FROM_WIN32(ERROR_PRIVILEGE_NOT_HELD) ||
                 Items[Index]->Error == HRESULT_FROM_WIN32(ERROR_ELEVATION_REQUIRED);
    }
    if (Denied == FALSE)
    {
        return;
    }
    Status = RtlQueryEnvironmentVariable_U(NULL, (PUNICODE_STRING)&TempName, &Temp);
    if (Status != STATUS_BUFFER_TOO_SMALL || Temp.Length == 0 || Temp.Length > MAXUSHORT - sizeof(WCHAR))
    {
        return;
    }
    Capacity = Temp.Length / sizeof(WCHAR) + ARRAYSIZE(L"\\WUA.ChildSettings..bin") + CUA_GUID_CCH;
    Path = (PWSTR)Mem_Alloc((SIZE_T)Capacity * sizeof(WCHAR));
    if (Path == NULL)
    {
        return;
    }
    Temp.Buffer = Path;
    Temp.MaximumLength = Temp.Length + sizeof(WCHAR);
    Status = RtlQueryEnvironmentVariable_U(NULL, (PUNICODE_STRING)&TempName, &Temp);
    if (!NT_SUCCESS(Status) || Temp.Length == 0)
    {
        goto Exit;
    }
    Path[Temp.Length / sizeof(WCHAR)] = UNICODE_NULL;
    Arguments[4] = Path;
    Hr = CuaNewId(Id);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Length = Temp.Length / sizeof(WCHAR);
    if (Str_PrintfExW(Path + Length, Capacity - Length, L"%lsWUA.ChildSettings.%ls.bin",
        Path[Length - 1] == L'\\' ? L"" : L"\\", Id) == 0)
    {
        goto Exit;
    }
    Status = RtlConvertSidToUnicodeString(&Sid, (PSID)Identity->Sid, TRUE);
    if (!NT_SUCCESS(Status))
    {
        goto Exit;
    }
    Status = RtlCreateSecurityDescriptor(&Descriptor, SECURITY_DESCRIPTOR_REVISION);
    if (NT_SUCCESS(Status))
    {
        Status = RtlCreateAcl(Acl, sizeof(AclBuffer), ACL_REVISION);
    }
    if (NT_SUCCESS(Status))
    {
        Status = RtlAddAccessAllowedAce(Acl, ACL_REVISION, FILE_ALL_ACCESS, (PSID)Identity->Sid);
    }
    if (NT_SUCCESS(Status))
    {
        Status = RtlAddAccessAllowedAce(Acl, ACL_REVISION, FILE_ALL_ACCESS, &SystemSid);
    }
    if (NT_SUCCESS(Status))
    {
        Status = RtlSetOwnerSecurityDescriptor(&Descriptor, (PSID)Identity->Sid, FALSE);
    }
    if (NT_SUCCESS(Status))
    {
        Status = RtlSetDaclSecurityDescriptor(&Descriptor, TRUE, Acl, FALSE);
    }
    if (NT_SUCCESS(Status))
    {
        Status = RtlSetControlSecurityDescriptor(&Descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
    }
    if (NT_SUCCESS(Status))
    {
        Status = NT_InitWin32PathObject(&Object, Path, NULL, &NtPath);
    }
    if (!NT_SUCCESS(Status))
    {
        goto Exit;
    }
    Object.SecurityDescriptor = &Descriptor;
    Status = NtCreateFile(&File, FILE_GENERIC_READ | DELETE, &Object, &IoStatus, NULL, FILE_ATTRIBUTE_TEMPORARY,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_CREATE,
                          FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_DELETE_ON_CLOSE |
                              FILE_OPEN_REPARSE_POINT,
                          NULL, 0);
    if (!NT_SUCCESS(Status))
    {
        goto Exit;
    }
    Str_DecFromUIntW(Identity->SessionId, Session);
    Arguments[2] = Sid.Buffer;
    Status = PS_ArgvToCommandLineW(ARRAYSIZE(Arguments), Arguments, &Parameters);
    if (!NT_SUCCESS(Status))
    {
        goto Exit;
    }
    Execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    Execute.lpVerb = L"runas";
    Execute.lpFile = NtCurrentPeb()->ProcessParameters->ImagePathName.Buffer;
    Execute.lpParameters = Parameters;
    Execute.lpDirectory = NtCurrentPeb()->ProcessParameters->CurrentDirectory.DosPath.Buffer;
    Execute.nShow = SW_HIDE;
    if (ShellExecuteExW(&Execute) == FALSE)
    {
        goto Exit;
    }
    *Uncertain = TRUE;
    WaitTime.QuadPart = -60000LL * 10000;
    if (Execute.hProcess == NULL || NtWaitForSingleObject(Execute.hProcess, FALSE, &WaitTime) != STATUS_SUCCESS)
    {
        goto Exit;
    }
    *Uncertain = FALSE;
    Status =
        NtQueryInformationProcess(Execute.hProcess, ProcessBasicInformation, &ProcessInfo, sizeof(ProcessInfo), NULL);
    if (NT_SUCCESS(Status) && ProcessInfo.ExitStatus == STATUS_SUCCESS)
    {
        Status = IO_GetFileSize(File, &Size);
        if (NT_SUCCESS(Status) && Size == sizeof(Elevated))
        {
            Status = IO_ReadFile(File, NULL, &Elevated, sizeof(Elevated), &Read);
            if (NT_SUCCESS(Status) && Read == sizeof(Elevated))
            {
                MergeSettings(Settings, &Elevated);
                goto Exit;
            }
        }
    }
    // The helper may have applied settings before its result write failed.
    CuaSessionEnableChildSession(&Elevated);
    MergeSettings(Settings, &Elevated);
Exit:
    if (Execute.hProcess != NULL)
    {
        NtClose(Execute.hProcess);
    }
    if (File != NULL)
    {
        NtClose(File);
    }
    if (Parameters != NULL)
    {
        PS_FreeCommandLineBuffer(Parameters);
    }
    if (NtPath.Buffer != NULL)
    {
        NT_FreeNtPath(&NtPath);
    }
    RtlFreeUnicodeString(&Sid);
    Mem_Free(Path);
}

static
HRESULT
EnableChildSession(
    _Outptr_ IJsonValue** Reply)
{
    CUA_CHILD_SETTINGS Settings;
    WUA_WORKER_IDENTITY Identity;
    IJsonValue* Result = NULL;
    CUA_ERROR Error = { 0 };
    HRESULT Hr;
    LOGICAL Uncertain = FALSE;

    *Reply = NULL;
    CuaSessionEnableChildSession(&Settings);
    if (SUCCEEDED(QueryWorkerIdentity(&Identity)))
    {
        TryElevatedSettings(&Identity, &Settings, &Uncertain);
    }
    Hr = CuaBuildChildSettingsResult(&Settings, &Result);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Settings.ChildSessions.Applied == FALSE || Settings.DefaultCredentials.Applied == FALSE ||
        Settings.NtlmDefaultCredentials.Applied == FALSE || Settings.PasswordLogin.Applied == FALSE)
    {
        Error.Status = Settings.ChildSessions.Applied == FALSE            ? Settings.ChildSessions.Error
                       : Settings.DefaultCredentials.Applied == FALSE     ? Settings.DefaultCredentials.Error
                       : Settings.NtlmDefaultCredentials.Applied == FALSE ? Settings.NtlmDefaultCredentials.Error
                                                                          : Settings.PasswordLogin.Error;
        Str_CopyW(Error.Message, Uncertain != FALSE
                                     ? L"The configuration helper result is unknown; settings may still be changing."
                                     : L"Some child session settings were not applied.");
        if (SUCCEEDED(Error.Status))
        {
            Error.Status = E_FAIL;
        }
    }
    Hr = CuaBuildReply(Result, FAILED(Error.Status) ? &Error : NULL, Reply);
Exit:
    if (Result != NULL)
    {
        Result->lpVtbl->Release(Result);
    }
    return Hr;
}

static
HRESULT
BuildCliError(
    _In_ HRESULT Status,
    _In_ LOGICAL Sent,
    _Inout_ CUA_ERROR* Error,
    _Outptr_ IJsonValue** Reply)
{
    if (SUCCEEDED(Error->Status))
    {
        Error->Status = Status;
        Str_CopyW(Error->Message,
            Sent != FALSE
                ? L"Check the target or Session List before continuing; do not repeat the operation."
                : L"The command could not start. Check its parameters, Session List and available permissions.");
    }
    return CuaBuildReply(NULL, Error, Reply);
}

int
WuaCliMain(
    _In_ int Argc,
    _In_reads_(Argc) wchar_t** Argv,
    _In_ PCWSTR Tool)
{
    WUA_COMMAND_PARAMETER Descriptors[CuaParamCount + 2] = { 0 };
    WUA_COMMAND Parsed = { Descriptors, 0, NULL };
    CUA_PARAMETER ParameterIds[CuaParamCount];
    PWSTR Strings[CuaParamCount] = { 0 };
    ULONG Session = NtCurrentPeb()->SessionId;
    PWSTR Operation = L"";
    ULONG PublicCount;
    PWUA_COMMAND_PARAMETER Descriptor;
    CUA_PARAMETER Parameter;
    CUA_REQUEST Request = { 0 };
    CUA_ERROR Error = { 0 };
    IJsonValue* Wire = NULL;
    IJsonValue* Reply = NULL;
    WCHAR SessionText[11];
    UNICODE_STRING Path;
    UNICODE_STRING FullPath = { 0 };
    UNICODE_STRING DynamicFullPath = { 0 };
    PUNICODE_STRING UsedPath;
    RTL_PATH_TYPE PathType;
    PCWSTR Command;
    PCWSTR Invalid;
    DWORD OwnSession = NtCurrentPeb()->SessionId;
    DWORD SessionId, ParentId;
    DWORD Timeout = 30000;
    LOGICAL Sent = FALSE;
    LOGICAL ParentRoute;
    HRESULT Hr;
    HRESULT Initialized;
    NTSTATUS Status;
    int ExitCode = 1;
    int Start = 1;

    if (_wcsicmp(Tool, L"CUA") == 0 && Argc != 0)
    {
        if (_wcsicmp(Argv[0], L"TestRpc") == 0)
        {
            return TestRpcMain(Argc - 1, Argv + 1);
        }
        if (_wcsicmp(Argv[0], L"TestHost") == 0)
        {
            return TestHostMain(Argc - 1, Argv + 1);
        }
        if (_wcsicmp(Argv[0], L"Server") == 0)
        {
            return Argc == 1 ? CuaServerMain() : E_INVALIDARG;
        }
        if (_wcsicmp(Argv[0], L"SessionLauncher") == 0)
        {
            if (Argc != 3 || Str_DecToUIntW(Argv[1], &SessionId) == FALSE)
            {
                return E_INVALIDARG;
            }
            return SessionLauncherMain(SessionId, Argv[2]);
        }
        if (_wcsicmp(Argv[0], L"ConfigureChildSession") == 0)
        {
            if (Argc != 4 || Str_DecToUIntW(Argv[2], &SessionId) == FALSE)
            {
                return E_INVALIDARG;
            }
            return ConfigureChildSessionMain(Argv[1], SessionId, Argv[3]);
        }
    }
    Initialized = RoInitialize(RO_INIT_MULTITHREADED);
    if (Initialized == RPC_E_CHANGED_MODE)
    {
        Initialized = RoInitialize(RO_INIT_SINGLETHREADED);
    }
    Hr = Initialized;
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Argc == 0)
    {
        Hr = E_INVALIDARG;
        Error.Status = Hr;
        Str_CopyW(Error.Message, L"A command is required. See Source/WUA-CLI/Tools/CUA/README.md.");
        goto Exit;
    }
    Command = Argv[0];
    if (_wcsicmp(Tool, L"CUA") == 0 && _wcsicmp(Command, L"Session") == 0)
    {
        if (Argc < 2)
        {
            Hr = E_INVALIDARG;
            goto Exit;
        }
        Operation = Argv[1];
        Start = 2;
    }
    for (ULONG Index = 0; Index < CuaParamCount; Index++)
    {
        if (CuaParameterInfo[Index].Private != FALSE)
        {
            continue;
        }
        ParameterIds[Parsed.ParameterCount] = (CUA_PARAMETER)Index;
        Descriptor = &Descriptors[Parsed.ParameterCount++];
        Descriptor->Name = CuaParameterInfo[Index].CliName;
        if (CuaParameterInfo[Index].Type == CuaNumberParameter)
        {
            Descriptor->Type = WUA_Parameter_Int32;
            Descriptor->SizeOfBuffer = sizeof(LONG);
            Descriptor->Buffer = (PVOID*)&Request.Parameters.Values[Index].Number;
        } else if (CuaParameterInfo[Index].Type == CuaBooleanParameter)
        {
            Descriptor->Type = WUA_Parameter_Bool;
            Descriptor->SizeOfBuffer = sizeof(LOGICAL);
            Descriptor->Buffer = (PVOID*)&Request.Parameters.Values[Index].Boolean;
        } else
        {
            Descriptor->Type = WUA_Parameter_String;
            Descriptor->SizeOfBuffer = sizeof(PWSTR);
            Descriptor->Buffer = (PVOID*)&Strings[Index];
        }
    }
    PublicCount = Parsed.ParameterCount;
    Descriptors[Parsed.ParameterCount++] = (WUA_COMMAND_PARAMETER)DEF_PARAMETER_ENTRY(Session, IntU32, FALSE);
    if (_wcsicmp(Tool, L"CUA") == 0)
    {
        if (_wcsicmp(Command, L"Mouse") == 0 || _wcsicmp(Command, L"Element") == 0)
        {
            Descriptors[Parsed.ParameterCount++] = (WUA_COMMAND_PARAMETER)DEF_PARAMETER_ENTRY(Operation, String, TRUE);
        } else if (_wcsicmp(Command, L"Window") == 0)
        {
            Descriptors[Parsed.ParameterCount++] = (WUA_COMMAND_PARAMETER){
                L"Action", (PVOID*)&Operation, { sizeof(Operation), WUA_Parameter_String, TRUE } };
        }
    }
    Invalid = InitCommandParameters(&Parsed, Argc - Start, Argv + Start);
    if (Invalid != NULL)
    {
        Hr = E_INVALIDARG;
        Error.Status = Hr;
        Str_PrintfW(Error.Message, L"Parameter \"%ls\" is invalid or required.", Invalid);
        goto Exit;
    }
    Request.Method = CuaFindCommand(Tool, Command, Operation);
    if (Request.Method == CuaMethodUnknown)
    {
        Hr = E_INVALIDARG;
        Error.Status = Hr;
        Str_CopyW(Error.Message, L"Unknown command or operation. See Tools/CUA/README.md.");
        goto Exit;
    }
    for (ULONG Index = 0; Index < PublicCount; Index++)
    {
        if (Descriptors[Index].SizeOfBuffer != 0)
        {
            continue;
        }
        Parameter = ParameterIds[Index];
        if (CuaParameterInfo[Parameter].Type == CuaStringParameter ||
            CuaParameterInfo[Parameter].Type == CuaKeysParameter)
        {
            Hr = BindStringParameter(&Request.Parameters, Parameter, Strings[Parameter]);
            if (FAILED(Hr))
            {
                goto Exit;
            }
        } else
        {
            Request.Parameters.Present |= 1ull << Parameter;
        }
    }
    SessionId = Session;
    if (CuaHas(&Request.Parameters, CuaParamOutFile) != FALSE)
    {
        Status = RtlInitUnicodeStringEx(&Path, CuaString(&Request.Parameters, CuaParamOutFile, L""));
        if (!NT_SUCCESS(Status) || Path.Length == 0)
        {
            Hr = E_INVALIDARG;
            goto Exit;
        }
        Status = RtlGetFullPathName_UstrEx(&Path, &FullPath, &DynamicFullPath, &UsedPath, NULL, NULL, &PathType, NULL);
        if (!NT_SUCCESS(Status))
        {
            Hr = Err_NtStatusToHr(Status);
            goto Exit;
        }
        Hr = CuaSetString(&Request.Parameters, CuaParamOutFile, UsedPath->Buffer);
        if (FAILED(Hr))
        {
            goto Exit;
        }
    }
    ParentRoute = Request.Method == CuaMethodSessionPreview || Request.Method == CuaMethodSessionDestroy;
    if (ParentRoute != FALSE)
    {
        if (Descriptors[PublicCount].SizeOfBuffer != 0 ||
            WinStationGetParentSessionId(Session, &ParentId) == FALSE || ParentId == MAXULONG || ParentId == Session)
        {
            Hr = E_INVALIDARG;
            Error.Status = Hr;
            Str_CopyW(Error.Message, L"Preview and Destroy require the Windows ID of an owned child session.");
            goto Exit;
        }
        Str_DecFromUIntW(Session, SessionText);
        Hr = CuaSetString(&Request.Parameters, CuaParamSession, SessionText);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        SessionId = ParentId;
    }
    Hr = CuaValidateParameters(&Request, &Error);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Request.Method == CuaMethodSessionEnableChildSession)
    {
        if (SessionId != OwnSession)
        {
            Hr = E_INVALIDARG;
            goto Exit;
        }
        Hr = EnableChildSession(&Reply);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        goto Emit;
    }
    if (SessionId == OwnSession)
    {
        Hr = CuaEnsureLocalServer(30000);
        if (FAILED(Hr))
        {
            goto Exit;
        }
    }
    Hr = CuaBuildRequest(&Request, &Wire);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Request.Method == CuaMethodSessionCreateChild)
    {
        Timeout = INFINITE;
    }
    Hr = CuaCallServer(SessionId, Wire, Timeout, &Reply, &Sent);
    if (FAILED(Hr))
    {
        goto Exit;
    }
Emit:
    ExitCode = IsSuccessfulReply(Reply) != FALSE ? 0 : 1;
    Hr = Util_WriteJson(Reply, _Inline_GetStdHandle(STD_OUTPUT_HANDLE));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    goto Cleanup;
Exit:
    ExitCode = 1;
    if (FAILED(Hr))
    {
        if (Reply != NULL)
        {
            Reply->lpVtbl->Release(Reply);
        }
        Reply = NULL;
        if (SUCCEEDED(BuildCliError(Hr, Sent, &Error, &Reply)))
        {
            Util_WriteJson(Reply, _Inline_GetStdHandle(STD_OUTPUT_HANDLE));
        }
    }
Cleanup:
    RtlFreeUnicodeString(&DynamicFullPath);
    CuaFreeRequest(&Request);
    if (Reply != NULL)
    {
        Reply->lpVtbl->Release(Reply);
    }
    if (Wire != NULL)
    {
        Wire->lpVtbl->Release(Wire);
    }
    if (SUCCEEDED(Initialized))
    {
        RoUninitialize();
    }
    return ExitCode;
}
