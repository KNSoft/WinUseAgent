#include "pch.h"
#include "Transport.h"
#include "Server.h"
#include "Worker.h"
#include <rpcasync.h>
#include "RpcClient.g.h"

#pragma comment(lib, "rpcrt4.lib")

extern RPC_IF_HANDLE CuaServer_CuaRpc_v1_0_s_ifspec;

typedef struct _SERVER_SECURITY
{
    BYTE Sid[SECURITY_MAX_SID_SIZE];
    SECURITY_DESCRIPTOR Descriptor;
    union
    {
        ACL Header;
        BYTE Buffer[sizeof(ACL) + 2 * (FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + SECURITY_MAX_SID_SIZE)];
    } Dacl;
    union
    {
        ACL Header;
        BYTE Buffer[sizeof(ACL) + FIELD_OFFSET(SYSTEM_MANDATORY_LABEL_ACE, SidStart) + sizeof(SID)];
    } Sacl;
} SERVER_SECURITY;

static CUA_SERVER* ServerCore;
static SERVER_SECURITY* ServerSecurity;

static
DWORD
Remaining(
    _In_ ULONGLONG Deadline)
{
    ULONGLONG Now = _Inline_GetTickCount64();
    return Now >= Deadline ? 0 : (DWORD)min(Deadline - Now, MAXDWORD - 1);
}

static
VOID
EndpointName(
    _In_ DWORD SessionId,
    _Out_writes_(64) PWSTR Name)
{
    Str_PrintfExW(Name, 64, L"KNSoft.WUA.CUA.%lu", SessionId);
}

static
HRESULT
TokenSid(
    _In_ HANDLE Token,
    _Out_writes_bytes_(SECURITY_MAX_SID_SIZE) PVOID Sid)
{
    union
    {
        TOKEN_USER User;
        BYTE Buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    } Data;
    ULONG Returned;
    RtlZeroMemory(Sid, SECURITY_MAX_SID_SIZE);
    NTSTATUS Status = NtQueryInformationToken(Token, TokenUser, &Data, sizeof(Data), &Returned);
    if (!NT_SUCCESS(Status))
    {
        return Err_NtStatusToHr(Status);
    }
    return Err_NtStatusToHr(RtlCopySid(SECURITY_MAX_SID_SIZE, (PSID)Sid, Data.User.User.Sid));
}

static
HRESULT
CreateSecurity(
    _Out_ SERVER_SECURITY* Security)
{
    SID System = { SID_REVISION, 1, SECURITY_NT_AUTHORITY, { SECURITY_LOCAL_SYSTEM_RID } };
    SID Medium = { SID_REVISION, 1, SECURITY_MANDATORY_LABEL_AUTHORITY, { SECURITY_MANDATORY_MEDIUM_RID } };
    HRESULT Hr;
    Hr = TokenSid(NtCurrentProcessToken(), Security->Sid);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Err_NtStatusToHr(RtlCreateSecurityDescriptor(&Security->Descriptor, SECURITY_DESCRIPTOR_REVISION));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Err_NtStatusToHr(RtlCreateAcl(&Security->Dacl.Header, sizeof(Security->Dacl), ACL_REVISION));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Err_NtStatusToHr(RtlAddAccessAllowedAce(&Security->Dacl.Header, ACL_REVISION, GENERIC_ALL, &System));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Err_NtStatusToHr(RtlAddAccessAllowedAce(&Security->Dacl.Header, ACL_REVISION,
            GENERIC_ALL, (PSID)Security->Sid));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Err_NtStatusToHr(RtlSetDaclSecurityDescriptor(&Security->Descriptor, TRUE, &Security->Dacl.Header, FALSE));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Err_NtStatusToHr(RtlSetControlSecurityDescriptor(&Security->Descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Err_NtStatusToHr(RtlCreateAcl(&Security->Sacl.Header, sizeof(Security->Sacl), ACL_REVISION));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Err_NtStatusToHr(RtlAddMandatoryAce(&Security->Sacl.Header, ACL_REVISION, 0, &Medium,
                                            SYSTEM_MANDATORY_LABEL_ACE_TYPE, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Err_NtStatusToHr(RtlSetSaclSecurityDescriptor(&Security->Descriptor, TRUE, &Security->Sacl.Header, FALSE));
Exit:
    return Hr;
}

_Must_inspect_result_
_Ret_maybenull_
_Post_writable_byte_size_(Size)
void*
__RPC_USER
MIDL_user_allocate(
    _In_ size_t Size)
{
    return Mem_Alloc(Size);
}

void
__RPC_USER
MIDL_user_free(
    _Pre_maybenull_ _Post_invalid_ void* Buffer)
{
    Mem_Free(Buffer);
}

static
RPC_STATUS
RPC_ENTRY
Authorize(
    _In_ RPC_IF_HANDLE Interface,
    _In_ void* Binding)
{
    HANDLE Token = NULL;
    BYTE Sid[SECURITY_MAX_SID_SIZE];
    PTOKEN_MANDATORY_LABEL Label = NULL;
    HRESULT Hr;
    RPC_STATUS Status = RpcImpersonateClient(Binding);
    UNREFERENCED_PARAMETER(Interface);
    if (Status != RPC_S_OK)
    {
        return RPC_S_ACCESS_DENIED;
    }
    Hr = Err_NtStatusToHr(NtOpenThreadToken(NtCurrentThread(), TOKEN_QUERY, TRUE, &Token));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = TokenSid(Token, Sid);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (RtlEqualSid((PSID)Sid, (PSID)ServerSecurity->Sid) == FALSE)
    {
        Hr = E_ACCESSDENIED;
        goto Exit;
    }
    Hr = Err_NtStatusToHr(PS_GetTokenInfo(Token, TokenIntegrityLevel, (PVOID*)&Label));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (*RtlSubAuthoritySid(Label->Label.Sid, *RtlSubAuthorityCountSid(Label->Label.Sid) - 1) <
        SECURITY_MANDATORY_MEDIUM_RID)
    {
        Hr = E_ACCESSDENIED;
    }
Exit:
    Mem_Free(Label);
    if (Token != NULL)
    {
        NtClose(Token);
    }
    if (RpcRevertToSelfEx(Binding) != RPC_S_OK)
    {
        __fastfail(FAST_FAIL_INVALID_ARG);
    }
    return SUCCEEDED(Hr) ? RPC_S_OK : RPC_S_ACCESS_DENIED;
}

HRESULT
CuaCallServerRaw(
    _In_ DWORD SessionId,
    _In_reads_bytes_(Length) PCUCHAR Request,
    _In_ ULONG Length,
    _In_ DWORD Timeout,
    _Outptr_result_bytebuffer_(*ReplyLength) PBYTE* Reply,
    _Out_ PULONG ReplyLength,
    _Out_opt_ PLOGICAL Sent)
{
    WCHAR Endpoint[64];
    RPC_WSTR String = NULL;
    RPC_BINDING_HANDLE Binding = NULL;
    RPC_SECURITY_QOS_V3_W Security = { 0 };
    RPC_ASYNC_STATE Async;
    HANDLE Token = NULL, Event = NULL;
    BYTE Sid[SECURITY_MAX_SID_SIZE];
    byte* Response = NULL;
    ReplySize ResponseLength = 0;
    long Result = E_FAIL;
    RPC_STATUS Status = RPC_S_OK;
    HRESULT Hr;
    *Reply = NULL;
    *ReplyLength = 0;
    if (Sent != NULL)
    {
        *Sent = FALSE;
    }
    if (Length == 0 || Length > CUA_RPC_MAX_REQUEST_BYTES || Timeout == 0 || (Timeout != INFINITE && Timeout > 120000))
    {
        return E_INVALIDARG;
    }
    EndpointName(SessionId, Endpoint);
    Hr = Err_NtStatusToHr(PS_OpenCurrentThreadToken(&Token));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = TokenSid(Token, Sid);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Status = RpcStringBindingComposeW(NULL, L"ncalrpc", NULL, Endpoint, NULL, &String);
    Hr = HRESULT_FROM_WIN32(Status);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = HRESULT_FROM_WIN32(RpcBindingFromStringBindingW(String, &Binding));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Security.Version = RPC_C_SECURITY_QOS_VERSION_3;
    Security.Capabilities = RPC_C_QOS_CAPABILITIES_MUTUAL_AUTH;
    Security.IdentityTracking = RPC_C_QOS_IDENTITY_STATIC;
    Security.ImpersonationType = RPC_C_IMP_LEVEL_IDENTIFY;
    Security.Sid = (PSID)Sid;
    Hr = HRESULT_FROM_WIN32(RpcBindingSetAuthInfoExW(Binding, NULL, RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
        RPC_C_AUTHN_WINNT, NULL, RPC_C_AUTHZ_NONE, (RPC_SECURITY_QOS*)&Security));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Err_NtStatusToHr(NtCreateEvent(&Event, EVENT_ALL_ACCESS, NULL, NotificationEvent, FALSE));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = HRESULT_FROM_WIN32(RpcAsyncInitializeHandle(&Async, sizeof(Async)));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Async.NotificationType = RpcNotificationTypeEvent;
    Async.u.hEvent = Event;
    if (Sent != NULL)
    {
        *Sent = TRUE;
    }
    RpcTryExcept
    {
        CuaClient_Execute(&Async, Binding, SessionId, Length, (byte*)Request, &ResponseLength, &Response);
    }
    RpcExcept(RpcExceptionFilter(RpcExceptionCode()))
    {
        Status = RpcExceptionCode();
        Hr = HRESULT_FROM_WIN32(Status);
    }
    RpcEndExcept
    if (FAILED(Hr))
    {
        Response = NULL;
        goto Exit;
    }
    NTSTATUS Wait = PS_WaitForObject(Event, Timeout);
    if (Wait != STATUS_WAIT_0)
    {
        // Abort locally, then collect completion before releasing the call's storage.
        Status = RpcAsyncCancelCall(&Async, TRUE);
        if (Status != RPC_S_OK)
        {
            __fastfail(FAST_FAIL_INVALID_ARG);
        }
        NtWaitForSingleObject(Event, FALSE, NULL);
    }
    Status = RpcAsyncCompleteCall(&Async, &Result);
    if (Status != RPC_S_OK)
    {
        Response = NULL;
    }
    Hr = Wait == STATUS_TIMEOUT ? HRESULT_FROM_WIN32(ERROR_TIMEOUT) :
         Wait != STATUS_WAIT_0 ? Err_NtStatusToHr(Wait) : HRESULT_FROM_WIN32(Status);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Result;
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (ResponseLength == 0 || ResponseLength > CUA_RPC_MAX_REPLY_BYTES || Response == NULL)
    {
        Hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        goto Exit;
    }
    *Reply = Response;
    *ReplyLength = ResponseLength;
    Response = NULL;
Exit:
    if (FAILED(Hr) && Sent != NULL &&
        (Status == RPC_S_SERVER_UNAVAILABLE || Status == RPC_S_CALL_FAILED_DNE || Status == RPC_S_ACCESS_DENIED))
    {
        *Sent = FALSE;
    }
    Mem_Free(Response);
    if (Event != NULL)
    {
        NtClose(Event);
    }
    if (Binding != NULL)
    {
        RpcBindingFree(&Binding);
    }
    if (String != NULL)
    {
        RpcStringFreeW(&String);
    }
    if (Token != NULL)
    {
        NtClose(Token);
    }
    return Hr;
}

HRESULT
CuaCallServer(
    _In_ DWORD SessionId,
    _In_ IJsonValue* Request,
    _In_ DWORD Timeout,
    _Outptr_ IJsonValue** Reply,
    _Out_opt_ PLOGICAL Sent)
{
    PSTR Text = NULL;
    PBYTE Response = NULL;
    ULONG Length, ResponseLength;
    HRESULT Hr;
    *Reply = NULL;
    if (Sent != NULL)
    {
        *Sent = FALSE;
    }
    Hr = Data_JsonStringifyUtf8(Request, &Text, &Length);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = CuaCallServerRaw(SessionId, (PCUCHAR)Text, Length, Timeout, &Response, &ResponseLength, Sent);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonParseUtf8((PCSTR)Response, ResponseLength, Reply);
Exit:
    if (FAILED(Hr) && *Reply != NULL)
    {
        (*Reply)->lpVtbl->Release(*Reply);
        *Reply = NULL;
    }
    Mem_Free(Text);
    Mem_Free(Response);
    return Hr;
}

HRESULT
CuaProbeServer(
    _In_ DWORD SessionId,
    _In_ DWORD Timeout,
    _Out_writes_(CUA_GUID_CCH) PWSTR Instance,
    _Out_opt_ PDWORD Process)
{
    CUA_REQUEST Request = { 0 };
    IJsonValue *Wire = NULL, *Reply = NULL;
    IJsonObject *Root = NULL, *Result = NULL;
    HSTRING Name = NULL, Text = NULL;
    boolean Ok;
    HRESULT Initialized = RoInitialize(RO_INIT_MULTITHREADED);
    if (Initialized == RPC_E_CHANGED_MODE)
    {
        Initialized = RoInitialize(RO_INIT_SINGLETHREADED);
    }
    HRESULT Hr = Initialized;
    *Instance = 0;
    if (Process != NULL)
    {
        *Process = 0;
    }
    if (FAILED(Hr))
    {
        return Hr;
    }
    Request.Method = CuaMethodCapabilities;
    Hr = CuaBuildRequest(&Request, &Wire);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = CuaCallServer(SessionId, Wire, Timeout, &Reply, NULL);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Reply->lpVtbl->GetObject(Reply, &Root);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = _Inline_WindowsCreateString(L"ok", 2, &Name);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Root->lpVtbl->GetNamedBoolean(Root, Name, &Ok);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Ok == FALSE)
    {
        Hr = E_FAIL;
        goto Exit;
    }
    _Inline_WindowsDeleteString(Name);
    Name = NULL;
    Hr = _Inline_WindowsCreateString(L"result", 6, &Name);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Root->lpVtbl->GetNamedObject(Root, Name, &Result);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    _Inline_WindowsDeleteString(Name);
    Name = NULL;
    Hr = _Inline_WindowsCreateString(L"instance", 8, &Name);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Result->lpVtbl->GetNamedString(Result, Name, &Text);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    ULONG Length;
    PCWSTR Id = _Inline_WindowsGetStringRawBuffer(Text, &Length);
    if (Length != CUA_GUID_CCH - 1)
    {
        Hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        goto Exit;
    }
    RtlCopyMemory(Instance, Id, CUA_GUID_CCH * sizeof(WCHAR));
    if (Process != NULL)
    {
        DOUBLE Pid;
        _Inline_WindowsDeleteString(Name);
        Name = NULL;
        Hr = _Inline_WindowsCreateString(L"pid", 3, &Name);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = Result->lpVtbl->GetNamedNumber(Result, Name, &Pid);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        if (!(Pid >= 1 && Pid <= MAXDWORD) || Pid != (DWORD)Pid)
        {
            Hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            goto Exit;
        }
        *Process = (DWORD)Pid;
    }
    _Inline_WindowsDeleteString(Name);
    Name = NULL;
    Hr = _Inline_WindowsCreateString(L"desktop_ready", 13, &Name);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Result->lpVtbl->GetNamedBoolean(Result, Name, &Ok);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Ok ? S_OK : S_FALSE;
Exit:
    if (FAILED(Hr))
    {
        *Instance = 0;
    }
    _Inline_WindowsDeleteString(Text);
    _Inline_WindowsDeleteString(Name);
    if (Result != NULL)
    {
        Result->lpVtbl->Release(Result);
    }
    if (Root != NULL)
    {
        Root->lpVtbl->Release(Root);
    }
    if (Reply != NULL)
    {
        Reply->lpVtbl->Release(Reply);
    }
    if (Wire != NULL)
    {
        Wire->lpVtbl->Release(Wire);
    }
    CuaFreeRequest(&Request);
    if (SUCCEEDED(Initialized))
    {
        RoUninitialize();
    }
    return Hr;
}

_Success_(return >= 0)
long
CuaServer_Execute(
    _In_ handle_t Binding,
    _In_ unsigned long SessionId,
    _In_ unsigned long Length,
    _In_reads_bytes_(Length) byte* Text,
    _Out_ ReplySize* ReplyLength,
    _Outptr_result_bytebuffer_(*ReplyLength) byte** ReplyText)
{
    CUA_REQUEST Request = { 0 };
    CUA_ERROR Error = { 0 };
    IJsonValue *Wire = NULL, *Reply = NULL;
    LOGICAL Started = FALSE;
    HRESULT Initialized, Hr;
    *ReplyLength = 0;
    *ReplyText = NULL;
    if (SessionId != NtCurrentPeb()->SessionId)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    if (Length == 0 || Length > CUA_RPC_MAX_REQUEST_BYTES)
    {
        return E_INVALIDARG;
    }
    if (RpcServerTestCancel(Binding) == RPC_S_OK)
    {
        return HRESULT_FROM_WIN32(RPC_S_CALL_CANCELLED);
    }
    Initialized = RoInitialize(RO_INIT_MULTITHREADED);
    if (Initialized == RPC_E_CHANGED_MODE)
    {
        Initialized = RoInitialize(RO_INIT_SINGLETHREADED);
    }
    if (FAILED(Initialized))
    {
        return Initialized;
    }
    Hr = Data_JsonParseUtf8((PCSTR)Text, Length, &Wire);
    if (FAILED(Hr))
    {
        goto ErrorReply;
    }
    Hr = CuaParseRequest(Wire, &Request, &Error);
    if (FAILED(Hr))
    {
        goto ErrorReply;
    }
    Started = TRUE;
    Hr = CuaExecute(ServerCore, &Request, &Reply);
    if (SUCCEEDED(Hr))
    {
        goto Respond;
    }
ErrorReply:
    Error.Status = Hr;
    if (Error.Message[0] == UNICODE_NULL)
    {
        Str_CopyExW(Error.Message, ARRAYSIZE(Error.Message),
            Started != FALSE
                ? L"The result could not be confirmed. Inspect before continuing; do not repeat input."
                : L"The request is invalid.");
    }
    Hr = CuaBuildReply(NULL, &Error, &Reply);
    if (FAILED(Hr))
    {
        goto Exit;
    }
Respond:
    Hr = Data_JsonStringifyUtf8(Reply, (PSTR*)ReplyText, ReplyLength);
    if (SUCCEEDED(Hr) && (*ReplyLength == 0 || *ReplyLength > CUA_RPC_MAX_REPLY_BYTES))
    {
        Hr = HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    }
Exit:
    if (FAILED(Hr))
    {
        Mem_Free(*ReplyText);
        *ReplyText = NULL;
        *ReplyLength = 0;
    }
    CuaFreeRequest(&Request);
    if (Reply != NULL)
    {
        Reply->lpVtbl->Release(Reply);
    }
    if (Wire != NULL)
    {
        Wire->lpVtbl->Release(Wire);
    }
    RoUninitialize();
    return Hr;
}

static
LOGICAL
MayStart(
    _In_ HRESULT Hr)
{
    return Hr == HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE);
}

HRESULT
CuaEnsureLocalServer(
    _In_ DWORD Timeout)
{
    SERVER_SECURITY Security = { 0 };
    HANDLE Mutex = NULL, Process = NULL;
    PCWSTR Failure;
    WCHAR Instance[CUA_GUID_CCH], LockName[128];
    UNICODE_STRING Lock;
    OBJECT_ATTRIBUTES Object;
    LOGICAL Locked = FALSE;
    DWORD SessionId = NtCurrentPeb()->SessionId, ServerPid = 0;
    ULONGLONG Deadline = _Inline_GetTickCount64() + Timeout;
    NTSTATUS Status;
    HRESULT Hr;
    if (Timeout == 0 || Timeout > 120000)
    {
        return E_INVALIDARG;
    }
    if (SessionId == 0)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_LOGGED_ON);
    }
    Hr = CuaProbeServer(SessionId, min(Remaining(Deadline), 1000), Instance, &ServerPid);
    if (SUCCEEDED(Hr) || MayStart(Hr) == FALSE)
    {
        goto Exit;
    }
    Hr = CreateSecurity(&Security);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Str_PrintfExW(LockName, ARRAYSIZE(LockName), L"\\Sessions\\%lu\\BaseNamedObjects\\KNSoft.WUA.CUA.Bootstrap",
               SessionId);
    RtlInitUnicodeString(&Lock, LockName);
    NT_InitObject(&Object, &Lock, OBJ_CASE_INSENSITIVE | OBJ_OPENIF, NULL);
    Object.SecurityDescriptor = &Security.Descriptor;
    Hr = Err_NtStatusToHr(NtCreateMutant(&Mutex, MUTANT_ALL_ACCESS, &Object, FALSE));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Status = PS_WaitForObject(Mutex, Remaining(Deadline));
    Locked = Status == STATUS_WAIT_0 || Status == STATUS_ABANDONED;
    if (Locked == FALSE)
    {
        Hr = Status == STATUS_TIMEOUT ? HRESULT_FROM_WIN32(ERROR_TIMEOUT) : Err_NtStatusToHr(Status);
        goto Exit;
    }
    Hr = CuaProbeServer(SessionId, min(Remaining(Deadline), 1000), Instance, &ServerPid);
    if (SUCCEEDED(Hr) || MayStart(Hr) == FALSE)
    {
        goto Exit;
    }
    Hr = LaunchServer(SessionId, &Process, &Failure);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Deadline = _Inline_GetTickCount64() + Timeout;
    while (Remaining(Deadline) != 0)
    {
        Hr = CuaProbeServer(SessionId, min(Remaining(Deadline), 1000), Instance, &ServerPid);
        if (SUCCEEDED(Hr) || Hr == E_ACCESSDENIED)
        {
            goto Exit;
        }
        if (PS_WaitForObject(Process, 0) == STATUS_WAIT_0)
        {
            PROCESS_BASIC_INFORMATION Basic;
            Hr = Err_NtStatusToHr(
                NtQueryInformationProcess(Process, ProcessBasicInformation, &Basic, sizeof(Basic), NULL));
            if (FAILED(Hr))
            {
                goto Exit;
            }
            if (Basic.ExitStatus != STATUS_SUCCESS)
            {
                Hr = Basic.ExitStatus < 0 ? (HRESULT)Basic.ExitStatus : HRESULT_FROM_WIN32(Basic.ExitStatus);
                goto Exit;
            }
        }
        PS_DelayExec(min(Remaining(Deadline), 50));
    }
    Hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
Exit:
    if (SUCCEEDED(Hr) && ServerPid != 0)
    {
        // Transfer the caller's foreground permission when Windows allows it.
        NtUserAllowSetForegroundWindow(ServerPid);
    }
    if (Locked != FALSE)
    {
        NtReleaseMutant(Mutex, NULL);
    }
    if (Mutex != NULL)
    {
        NtClose(Mutex);
    }
    if (Process != NULL)
    {
        NtClose(Process);
    }
    return Hr;
}

int
CuaServerMain(VOID)
{
    SERVER_SECURITY Security = { 0 };
    CUA_SERVER* Server = NULL;
    HANDLE Mutex = NULL;
    WCHAR MutexName[128], Endpoint[64];
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Object;
    LOGICAL Registered = FALSE;
    HRESULT Initialized = RoInitialize(RO_INIT_MULTITHREADED);
    HRESULT Hr = Initialized;
    DWORD SessionId = NtCurrentPeb()->SessionId;
    if (FAILED(Hr))
    {
        return (int)Hr;
    }
    if (IsInteractiveWorker() == FALSE)
    {
        Hr = E_ACCESSDENIED;
        goto Exit;
    }
    Hr = CreateSecurity(&Security);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Str_PrintfExW(MutexName, ARRAYSIZE(MutexName), L"\\Sessions\\%lu\\BaseNamedObjects\\KNSoft.WUA.CUA", SessionId);
    RtlInitUnicodeString(&Name, MutexName);
    NT_InitObject(&Object, &Name, OBJ_CASE_INSENSITIVE, NULL);
    Object.SecurityDescriptor = &Security.Descriptor;
    Hr = Err_NtStatusToHr(NtCreateMutant(&Mutex, MUTANT_ALL_ACCESS, &Object, FALSE));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = CuaServerCreate(&Server);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    ServerCore = Server;
    ServerSecurity = &Security;
    EndpointName(SessionId, Endpoint);
    Hr = HRESULT_FROM_WIN32(RpcServerUseProtseqEpW(L"ncalrpc", RPC_C_PROTSEQ_MAX_REQS_DEFAULT,
        Endpoint, &Security.Descriptor));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = HRESULT_FROM_WIN32(RpcServerRegisterAuthInfoW(NULL, RPC_C_AUTHN_WINNT, NULL, NULL));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = HRESULT_FROM_WIN32(RpcServerRegisterIf3(CuaServer_CuaRpc_v1_0_s_ifspec, NULL, NULL,
        RPC_IF_ALLOW_LOCAL_ONLY | RPC_IF_ALLOW_SECURE_ONLY | RPC_IF_SEC_NO_CACHE,
        RPC_C_LISTEN_MAX_CALLS_DEFAULT, CUA_RPC_MAX_REQUEST_BYTES, Authorize, &Security.Descriptor));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Registered = TRUE;
    Hr = HRESULT_FROM_WIN32(RpcServerListen(1, RPC_C_LISTEN_MAX_CALLS_DEFAULT, FALSE));
Exit:
    if (Registered != FALSE)
    {
        RPC_STATUS Status = RpcServerUnregisterIf(CuaServer_CuaRpc_v1_0_s_ifspec, NULL, TRUE);
        if (Status != RPC_S_OK)
        {
            __fastfail(FAST_FAIL_INVALID_ARG);
        }
    }
    ServerCore = NULL;
    ServerSecurity = NULL;
    CuaServerDestroy(Server);
    if (Mutex != NULL)
    {
        NtClose(Mutex);
    }
    RoUninitialize();
    return FAILED(Hr) ? (int)Hr : 0;
}
