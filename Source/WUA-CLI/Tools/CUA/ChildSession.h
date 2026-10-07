#pragma once

#include "../../pch.h"
#include "Command.h"

typedef struct _CUA_SESSION_INFO
{
    DWORD Id;
    WCHAR Instance[CUA_GUID_CCH];
    WCHAR State[32];
    WCHAR Detail[512];
    LOGICAL Owned;
    LOGICAL PreviewVisible;
    DWORD Width, Height;
    HRESULT LastError;
} CUA_SESSION_INFO;

typedef struct _CUA_CHILD_SETTING
{
    HRESULT Error;
    LOGICAL Applied;
    LOGICAL Changed;
} CUA_CHILD_SETTING;

typedef struct _CUA_CHILD_SETTINGS
{
    CUA_CHILD_SETTING ChildSessions;
    CUA_CHILD_SETTING DefaultCredentials;
    CUA_CHILD_SETTING NtlmDefaultCredentials;
    CUA_CHILD_SETTING PasswordLogin;
} CUA_CHILD_SETTINGS;

typedef struct _CUA_SESSION_MANAGER CUA_SESSION_MANAGER;
typedef HRESULT (CALLBACK* CUA_SESSION_PROBE)(
    _In_ PVOID Context,
    _In_ DWORD Id,
    _In_ DWORD Timeout,
    _Out_writes_(CUA_GUID_CCH) PWSTR Instance,
    _Out_opt_ PDWORD Process);

EXTERN_C_START

HRESULT
CuaSessionsCreate(
    _In_ CUA_SESSION_PROBE Probe,
    _In_ PVOID Context,
    _Outptr_ CUA_SESSION_MANAGER** Manager);

VOID
CuaSessionsDestroy(
    _In_opt_ CUA_SESSION_MANAGER* Manager);

_Success_(return == S_OK)
HRESULT
CuaSessionsList(
    _In_ CUA_SESSION_MANAGER* Manager,
    _Outptr_result_buffer_(*Count) CUA_SESSION_INFO** Items,
    _Out_ PULONG Count,
    _Out_writes_(512) PWSTR Detail);

HRESULT
CuaSessionEnableChildSession(
    _Out_ CUA_CHILD_SETTINGS* Settings);

HRESULT
CuaBuildChildSettingsResult(
    _In_ const CUA_CHILD_SETTINGS* Settings,
    _Outptr_ IJsonValue** Result);

HRESULT
CuaSessionCreateChild(
    _In_ CUA_SESSION_MANAGER* Manager,
    _In_ DWORD Width,
    _In_ DWORD Height,
    _In_ LOGICAL Show,
    _Out_ CUA_SESSION_INFO* Info,
    _Out_writes_(512) PWSTR Detail);

HRESULT
CuaSessionPreview(
    _In_ CUA_SESSION_MANAGER* Manager,
    _In_ DWORD Id,
    _In_ LOGICAL Show,
    _Out_writes_(512) PWSTR Detail);

HRESULT
CuaSessionDestroy(
    _In_ CUA_SESSION_MANAGER* Manager,
    _In_ DWORD Id,
    _In_ DWORD Timeout,
    _Out_writes_(512) PWSTR Detail);

EXTERN_C_END
