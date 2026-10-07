#pragma once

#include "Command.h"

EXTERN_C_START

// S_FALSE: the Worker is reachable, but its graphics desktop is not available yet.
HRESULT
CuaProbeServer(
    _In_ DWORD SessionId,
    _In_ DWORD Timeout,
    _Out_writes_(CUA_GUID_CCH) PWSTR Instance,
    _Out_opt_ PDWORD Process);
HRESULT
CuaCallServer(
    _In_ DWORD SessionId,
    _In_ IJsonValue* Request,
    _In_ DWORD Timeout,
    _Outptr_ IJsonValue** Reply,
    _Out_opt_ PLOGICAL Sent);
HRESULT
CuaCallServerRaw(
    _In_ DWORD SessionId,
    _In_reads_bytes_(Length) PCUCHAR Request,
    _In_ ULONG Length,
    _In_ DWORD Timeout,
    _Outptr_result_bytebuffer_(*ReplyLength) PBYTE* Reply,
    _Out_ PULONG ReplyLength,
    _Out_opt_ PLOGICAL Sent);
HRESULT
CuaEnsureLocalServer(
    _In_ DWORD Timeout);
int CuaServerMain(VOID);

EXTERN_C_END
