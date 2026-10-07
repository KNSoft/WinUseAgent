#pragma once

typedef struct _WUA_WORKER_IDENTITY
{
    BYTE Sid[SECURITY_MAX_SID_SIZE];
    LOGICAL Administrator;
    LOGICAL AdministratorMember;
    LOGICAL UiAccess;
    ULONG SessionId;
} WUA_WORKER_IDENTITY;

EXTERN_C_START

HRESULT
QueryWorkerIdentity(
    _Out_ WUA_WORKER_IDENTITY* Identity);

LOGICAL
IsInteractiveWorker(VOID);

HRESULT
LaunchServer(
    _In_ DWORD SessionId,
    _Out_ PHANDLE Process,
    _Out_ PCWSTR* FailureText);

int
SessionLauncherMain(
    _In_ DWORD SessionId,
    _In_ PCWSTR ExpectedSid);

EXTERN_C_END
