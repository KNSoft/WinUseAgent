#include "pch.h"
#include "Server.h"

#pragma comment(lib, "winsta.lib")

static const UNICODE_STRING CredentialKey =
    RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SOFTWARE\\Policies\\Microsoft\\Windows\\CredentialsDelegation");
static const UNICODE_STRING DefaultCredentials = RTL_CONSTANT_STRING(L"AllowDefaultCredentials");
static const UNICODE_STRING NtlmDefaultCredentials = RTL_CONSTANT_STRING(L"AllowDefCredentialsWhenNTLMOnly");
static const UNICODE_STRING CredentialTarget = RTL_CONSTANT_STRING(L"TERMSRV/localhost");
static const UNICODE_STRING PasswordlessKey =
    RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\PasswordLess\\Device");
static const UNICODE_STRING PasswordlessValue = RTL_CONSTANT_STRING(L"DevicePasswordLessBuildVersion");

static
NTSTATUS
SetDwordSetting(
    _In_ HANDLE Key,
    _In_ PCUNICODE_STRING Name,
    _In_ DWORD Desired,
    _Inout_ CUA_CHILD_SETTING* Setting)
{
    DWORD Value;
    NTSTATUS Status = Sys_RegQueryDword(Key, Name, &Value);
    if (!NT_SUCCESS(Status) || Value != Desired)
    {
        Status = Sys_RegSetDword(Key, Name, Desired);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        Setting->Changed = TRUE;
    }
    Status = Sys_RegQueryDword(Key, Name, &Value);
    if (NT_SUCCESS(Status) && Value != Desired)
    {
        Status = STATUS_DATA_ERROR;
    }
    return Status;
}

static
NTSTATUS
FindCredentialTarget(
    _In_ HANDLE Key,
    _Out_ PBOOLEAN Found)
{
    NTSTATUS Status;
    ULONG Length;
    PKEY_VALUE_FULL_INFORMATION Value;
    *Found = FALSE;
    for (ULONG Index = 0;; ++Index)
    {
        Status = NtEnumerateValueKey(Key, Index, KeyValueFullInformation, NULL, 0, &Length);
        if (Status == STATUS_NO_MORE_ENTRIES)
        {
            break;
        }
        if (Status != STATUS_BUFFER_TOO_SMALL && Status != STATUS_BUFFER_OVERFLOW)
        {
            return Status;
        }
        if (Length > 65536)
        {
            continue;
        }
        Value = (PKEY_VALUE_FULL_INFORMATION)Mem_Alloc(Length);
        if (Value == NULL)
        {
            return STATUS_NO_MEMORY;
        }
        Status = NtEnumerateValueKey(Key, Index, KeyValueFullInformation, Value, Length, &Length);
        BOOLEAN Matches = NT_SUCCESS(Status) && Value->Type == REG_SZ &&
                          Value->DataLength == CredentialTarget.Length + sizeof(WCHAR) &&
                          _wcsnicmp((PCWSTR)((PBYTE)Value + Value->DataOffset), CredentialTarget.Buffer,
                                    CredentialTarget.Length / sizeof(WCHAR)) == 0 &&
                          *(PCWSTR)((PBYTE)Value + Value->DataOffset + CredentialTarget.Length) == UNICODE_NULL;
        Mem_Free(Value);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        if (Matches != FALSE)
        {
            *Found = TRUE;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
EnsureCredentialTarget(
    _In_ HANDLE Key,
    _Inout_ CUA_CHILD_SETTING* Setting)
{
    BOOLEAN Found;
    ULONG Length;
    WCHAR NameBuffer[16];
    UNICODE_STRING Name;
    KEY_VALUE_BASIC_INFORMATION Existing;
    NTSTATUS Status = FindCredentialTarget(Key, &Found);
    if (!NT_SUCCESS(Status) || Found != FALSE)
    {
        return Status;
    }
    // Preserve every existing delegation entry, including the sample's reserved value name.
    for (ULONG Candidate = 1; Candidate <= 65535; ++Candidate)
    {
        Str_PrintfW(NameBuffer, L"%lu", Candidate);
        RtlInitUnicodeString(&Name, NameBuffer);
        Status = NtQueryValueKey(Key, &Name, KeyValueBasicInformation, &Existing, sizeof(Existing), &Length);
        if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
        {
            Status =
                Sys_RegSetData(Key, &Name, REG_SZ, CredentialTarget.Buffer, CredentialTarget.Length + sizeof(WCHAR));
            if (NT_SUCCESS(Status))
            {
                PKEY_VALUE_PARTIAL_INFORMATION Written = NULL;
                Setting->Changed = TRUE;
                Status = Sys_RegQueryData(Key, &Name, &Written);
                if (NT_SUCCESS(Status) &&
                    (Written->Type != REG_SZ || Written->DataLength != CredentialTarget.Length + sizeof(WCHAR) ||
                     RtlEqualMemory(Written->Data, CredentialTarget.Buffer,
                                    CredentialTarget.Length + sizeof(WCHAR)) == FALSE))
                {
                    Status = STATUS_DATA_ERROR;
                }
                Mem_Free(Written);
            }
            return Status;
        }
        if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_TOO_SMALL && Status != STATUS_BUFFER_OVERFLOW)
        {
            return Status;
        }
    }
    return STATUS_TOO_MANY_NAMES;
}

static
NTSTATUS
QueryCredentialPolicy(
    _In_ PCUNICODE_STRING Policy)
{
    HANDLE Key = NULL, List = NULL;
    DWORD Enabled;
    BOOLEAN Found = FALSE;
    NTSTATUS Status = Sys_RegOpenKey(&Key, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &CredentialKey);
    if (NT_SUCCESS(Status))
    {
        Status = Sys_RegQueryDword(Key, Policy, &Enabled);
    }
    if (NT_SUCCESS(Status) && Enabled != 1)
    {
        Status = STATUS_DATA_ERROR;
    }
    if (NT_SUCCESS(Status))
    {
        Status = Sys_RegOpenKeyEx(&List, Key, KEY_QUERY_VALUE | KEY_WOW64_64KEY, Policy);
    }
    if (NT_SUCCESS(Status))
    {
        Status = FindCredentialTarget(List, &Found);
    }
    if (NT_SUCCESS(Status) && Found == FALSE)
    {
        Status = STATUS_DATA_ERROR;
    }
    if (List != NULL)
    {
        NtClose(List);
    }
    if (Key != NULL)
    {
        NtClose(Key);
    }
    return Status;
}

static
VOID
EnableCredentialPolicy(
    _In_ PCUNICODE_STRING Policy,
    _Inout_ CUA_CHILD_SETTING* Setting)
{
    HANDLE Key = NULL, List = NULL;
    NTSTATUS Status = QueryCredentialPolicy(Policy);
    if (NT_SUCCESS(Status))
    {
        Setting->Applied = TRUE;
        return;
    }
    Status =
        Sys_RegCreateKey(&Key, KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_CREATE_SUB_KEY | KEY_WOW64_64KEY, &CredentialKey);
    if (NT_SUCCESS(Status))
    {
        Status = SetDwordSetting(Key, Policy, 1, Setting);
    }
    if (NT_SUCCESS(Status))
    {
        Status = Sys_RegCreateKeyEx(&List, Key, KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY, Policy,
                                    REG_OPTION_NON_VOLATILE, NULL);
    }
    if (NT_SUCCESS(Status))
    {
        Status = EnsureCredentialTarget(List, Setting);
    }
    Setting->Error = Err_NtStatusToHr(Status);
    Setting->Applied = NT_SUCCESS(Status);
    if (List != NULL)
    {
        NtClose(List);
    }
    if (Key != NULL)
    {
        NtClose(Key);
    }
}

HRESULT
CuaSessionEnableChildSession(
    _Out_ CUA_CHILD_SETTINGS* Settings)
{
    BOOLEAN Enabled = FALSE;
    HANDLE Key = NULL;
    NTSTATUS Status;
    RtlZeroMemory(Settings, sizeof(*Settings));
    if (WinStationIsChildSessionsEnabled(&Enabled) == FALSE || Enabled == FALSE)
    {
        Settings->ChildSessions.Error =
            WinStationEnableChildSessions(TRUE) != FALSE ? S_OK : HRESULT_FROM_WIN32(Err_GetLastError());
        Settings->ChildSessions.Changed = SUCCEEDED(Settings->ChildSessions.Error);
    }
    if (WinStationIsChildSessionsEnabled(&Enabled) != FALSE)
    {
        Settings->ChildSessions.Applied = Enabled;
    } else if (SUCCEEDED(Settings->ChildSessions.Error))
    {
        Settings->ChildSessions.Error = HRESULT_FROM_WIN32(Err_GetLastError());
    }
    if (Settings->ChildSessions.Applied == FALSE && SUCCEEDED(Settings->ChildSessions.Error))
    {
        Settings->ChildSessions.Error = HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    }
    EnableCredentialPolicy(&DefaultCredentials, &Settings->DefaultCredentials);
    EnableCredentialPolicy(&NtlmDefaultCredentials, &Settings->NtlmDefaultCredentials);
    DWORD Value;
    Status = Sys_RegOpenKey(&Key, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &PasswordlessKey);
    if (NT_SUCCESS(Status))
    {
        Status = Sys_RegQueryDword(Key, &PasswordlessValue, &Value);
    }
    if (!NT_SUCCESS(Status) || Value != 0)
    {
        if (Key != NULL)
        {
            NtClose(Key);
        }
        Key = NULL;
        Status = Sys_RegCreateKey(&Key, KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY, &PasswordlessKey);
        if (NT_SUCCESS(Status))
        {
            Status = SetDwordSetting(Key, &PasswordlessValue, 0, &Settings->PasswordLogin);
        }
    }
    Settings->PasswordLogin.Error = Err_NtStatusToHr(Status);
    Settings->PasswordLogin.Applied = NT_SUCCESS(Status);
    if (Key != NULL)
    {
        NtClose(Key);
    }
    return Settings->ChildSessions.Applied != FALSE && Settings->DefaultCredentials.Applied != FALSE &&
                   Settings->NtlmDefaultCredentials.Applied != FALSE && Settings->PasswordLogin.Applied != FALSE
               ? S_OK
               : S_FALSE;
}

static
HRESULT
BuildSession(
    _In_ CUA_COMMAND* Command,
    _In_ const CUA_SESSION_INFO* Session,
    _Outptr_ IJsonValue** Value)
{
    IJsonObject* Object = NULL;
    IJsonValueStatics* Factory = Command->Factory;
    HRESULT Hr;
    *Value = NULL;
    Hr = Data_JsonCreateObject(&Object);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Session->Id != MAXDWORD)
    {
        Hr = Data_JsonObjectSetNumber(Factory, Object, L"id", Session->Id);
        if (FAILED(Hr))
        {
            goto Exit;
        }
    } else
    {
        Hr = Data_JsonObjectSetNull(Command->NullFactory, Object, L"id");
        if (FAILED(Hr))
        {
            goto Exit;
        }
    }
    Hr = Data_JsonObjectSetString(Factory, Object, L"state", Session->State, (ULONG)wcslen(Session->State));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Session->Detail[0] != UNICODE_NULL)
    {
        Hr = Data_JsonObjectSetString(Factory, Object, L"details", Session->Detail, (ULONG)wcslen(Session->Detail));
        if (FAILED(Hr))
        {
            goto Exit;
        }
    }
    Hr = Data_JsonObjectSetBoolean(Factory, Object, L"owned", Session->Owned);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Session->Owned != FALSE)
    {
        Hr = Data_JsonObjectSetBoolean(Factory, Object, L"preview_visible", Session->PreviewVisible);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = Data_JsonObjectSetNumber(Factory, Object, L"width", Session->Width);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = Data_JsonObjectSetNumber(Factory, Object, L"height", Session->Height);
        if (FAILED(Hr))
        {
            goto Exit;
        }
    }
    if (FAILED(Session->LastError))
    {
        Hr = Data_JsonObjectSetNumber(Factory, Object, L"last_error", Session->LastError);
        if (FAILED(Hr))
        {
            goto Exit;
        }
    }
    Hr = Object->lpVtbl->QueryInterface(Object, &IID_IJsonValue, (PVOID*)Value);
Exit:
    if (Object != NULL)
    {
        Object->lpVtbl->Release(Object);
    }
    return Hr;
}

HRESULT
CuaBuildChildSettingsResult(
    _In_ const CUA_CHILD_SETTINGS* Settings,
    _Outptr_ IJsonValue** Result)
{
    const CUA_CHILD_SETTING* Values[] = { &Settings->ChildSessions, &Settings->DefaultCredentials,
                                          &Settings->NtlmDefaultCredentials, &Settings->PasswordLogin };
    static const PCWSTR Names[] = { L"child_sessions", L"allow_default_credentials", L"allow_ntlm_default_credentials",
                                    L"password_login" };
    IJsonValueStatics* Factory = NULL;
    IJsonObject* Object = NULL;
    IJsonObject* Setting = NULL;
    IJsonValue* Value = NULL;
    HRESULT Hr;
    *Result = NULL;
    Hr = Data_JsonGetValueFactory(&Factory);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonCreateObject(&Object);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    for (ULONG Index = 0; Index < ARRAYSIZE(Values); ++Index)
    {
        Hr = Data_JsonCreateObject(&Setting);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = Data_JsonObjectSetBoolean(Factory, Setting, L"applied", Values[Index]->Applied);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = Data_JsonObjectSetBoolean(Factory, Setting, L"changed", Values[Index]->Changed);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = Data_JsonObjectSetNumber(Factory, Setting, L"error", Values[Index]->Error);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = Setting->lpVtbl->QueryInterface(Setting, &IID_IJsonValue, (PVOID*)&Value);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = Data_JsonObjectSetValue(Object, Names[Index], Value);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        if (Value != NULL)
        {
            Value->lpVtbl->Release(Value);
        }
        Value = NULL;
        if (Setting != NULL)
        {
            Setting->lpVtbl->Release(Setting);
        }
        Setting = NULL;
    }
    Hr = Object->lpVtbl->QueryInterface(Object, &IID_IJsonValue, (PVOID*)Result);
Exit:
    if (Value != NULL)
    {
        Value->lpVtbl->Release(Value);
    }
    if (Setting != NULL)
    {
        Setting->lpVtbl->Release(Setting);
    }
    if (Object != NULL)
    {
        Object->lpVtbl->Release(Object);
    }
    if (Factory != NULL)
    {
        Factory->lpVtbl->Release(Factory);
    }
    return Hr;
}

HRESULT
CuaSessionOperation(
    _Inout_ CUA_COMMAND* Command)
{
    CUA_METHOD Method = Command->Request->Method;
    CUA_PARAMETERS* Parameters = &Command->Request->Parameters;
    CUA_SESSION_MANAGER* Manager = Command->Server->Sessions;
    CUA_SESSION_INFO Info = { 0 };
    CUA_SESSION_INFO* Items = NULL;
    WCHAR Detail[512] = { 0 };
    ULONG Count;
    DWORD Id = MAXDWORD;
    LONG Width, Height;
    IJsonVector* Array = NULL;
    IJsonObject* Created = NULL;
    IJsonValue* Item = NULL;
    HRESULT Hr;
    Info.Id = MAXDWORD;
    if ((Method == CuaMethodSessionPreview || Method == CuaMethodSessionDestroy) &&
        (Str_DecToUIntW(CuaString(Parameters, CuaParamSession, L""), &Id) == FALSE || Id == 0 || Id == MAXDWORD))
    {
        return CuaFail(Command, E_INVALIDARG, L"Specify the child Windows Session ID.");
    }
    switch (Method)
    {
        case CuaMethodSessionList:
            Hr = CuaSessionsList(Manager, &Items, &Count, Detail);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Hr = Data_JsonCreateArray(&Array);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            for (ULONG Index = 0; Index < Count; ++Index)
            {
                Hr = BuildSession(Command, &Items[Index], &Item);
                if (FAILED(Hr))
                {
                    goto Exit;
                }
                Hr = Array->lpVtbl->Append(Array, Item);
                if (FAILED(Hr))
                {
                    goto Exit;
                }
                Item->lpVtbl->Release(Item);
                Item = NULL;
            }
            Hr = Array->lpVtbl->QueryInterface(Array, &IID_IJsonValue, (PVOID*)&Command->Result);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            break;
        case CuaMethodSessionCreateChild:
            Hr = CuaInteger(Command, CuaParamWidth, 1920, 640, 7680, &Width);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Hr = CuaInteger(Command, CuaParamHeight, 1080, 480, 4320, &Height);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Hr = CuaSessionCreateChild(Manager, Width, Height, CuaBoolean(Parameters, CuaParamShow, FALSE),
                                       &Info, Detail);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Hr = Data_JsonCreateObject(&Created);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Hr = Data_JsonObjectSetNumber(Command->Factory, Created, L"id", Info.Id);
            if (SUCCEEDED(Hr))
            {
                Hr = Created->lpVtbl->QueryInterface(Created, &IID_IJsonValue, (PVOID*)&Command->Result);
            }
            if (FAILED(Hr))
            {
                goto Exit;
            }
            break;
        case CuaMethodSessionPreview:
            if (CuaHas(Parameters, CuaParamShow) == FALSE)
            {
                Hr = CuaFail(Command, E_INVALIDARG, L"Show is required.");
                goto Exit;
            }
            Hr = CuaSessionPreview(Manager, Id, CuaBoolean(Parameters, CuaParamShow, FALSE), Detail);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            break;
        case CuaMethodSessionDestroy:
            Hr = CuaSessionDestroy(Manager, Id, 10000, Detail);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            break;
        default:
            Hr = CuaFail(Command, E_INVALIDARG, L"Unknown session operation.");
            break;
    }
Exit:
    if (FAILED(Hr) && SUCCEEDED(Command->Error.Status))
    {
        CuaFail(Command, Hr, Detail[0] != UNICODE_NULL ? Detail : L"Session operation failed.");
    }
    Mem_Free(Items);
    if (Created != NULL)
    {
        Created->lpVtbl->Release(Created);
    }
    if (Item != NULL)
    {
        Item->lpVtbl->Release(Item);
    }
    if (Array != NULL)
    {
        Array->lpVtbl->Release(Array);
    }
    return Hr;
}
