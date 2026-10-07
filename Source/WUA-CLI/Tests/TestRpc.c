#include "pch.h"
#include "TestRpc.h"
#include "../Tools/CUA/Transport.h"

static
NTSTATUS
CreateTestToken(
    _In_ PCWSTR Identity,
    _Out_ PHANDLE Token)
{
    HANDLE Source = NULL;
    NTSTATUS Status;
    struct
    {
        TOKEN_MANDATORY_LABEL Label;
        SID Sid;
    } Integrity = { 0 };

    *Token = NULL;
    if (Str_EqualIW(Identity, L"Normal") != FALSE)
    {
        return STATUS_SUCCESS;
    }
    if (Str_EqualIW(Identity, L"FilteredAdmin") != FALSE)
    {
        Status = PS_CreateRestrictedToken(FALSE, FALSE, &Source);
    } else if (Str_EqualIW(Identity, L"LowIntegrity") != FALSE)
    {
        Status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &Source);
    } else
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = PS_DuplicateToken(Source, TokenImpersonation, Token);
    NtClose(Source);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (Str_EqualIW(Identity, L"LowIntegrity") != FALSE)
    {
        Integrity.Sid = (SID)SID_ML_LOW;
        Integrity.Label.Label.Sid = &Integrity.Sid;
        Integrity.Label.Label.Attributes = SE_GROUP_INTEGRITY;
        Status = NtSetInformationToken(*Token, TokenIntegrityLevel, &Integrity, sizeof(Integrity));
        if (!NT_SUCCESS(Status))
        {
            NtClose(*Token);
            *Token = NULL;
        }
    }
    return Status;
}

int
TestRpcMain(
    _In_ int Count,
    _In_reads_(Count) wchar_t** Arguments)
{
    PWSTR RequestFile = L"";
    PWSTR Identity = L"Normal";
    DWORD Session = 0, TimeoutMs = 30000;
    WUA_COMMAND_PARAMETER Parameters[] = {
        DEF_PARAMETER_ENTRY(RequestFile, String, TRUE),
        DEF_PARAMETER_ENTRY(Session, IntU32, TRUE),
        DEF_PARAMETER_ENTRY(TimeoutMs, IntU32, FALSE),
        DEF_PARAMETER_ENTRY(Identity, String, FALSE)
    };
    WUA_COMMAND Command = { Parameters, ARRAYSIZE(Parameters), NULL };
    HANDLE Token = NULL, Previous = NULL;
    PBYTE Request = NULL, Reply = NULL;
    ULONG RequestLength = 0, ReplyLength = 0, Offset = 0, Written;
    LOGICAL Impersonating = FALSE, Sent = FALSE;
    PCWSTR Invalid;
    PCSTR Details = "Invalid TestRpc parameters.";
    IJsonObject* Error = NULL;
    IJsonValue* Value = NULL;
    NTSTATUS Status;
    HRESULT Initialized = E_FAIL, Hr;
    int Result = 1;

    Invalid = InitCommandParameters(&Command, Count, Arguments);
    if (Invalid != NULL || RequestFile[0] == UNICODE_NULL)
    {
        Hr = E_INVALIDARG;
        goto Exit;
    }
    Details = "Unable to read the raw RPC request file.";
    Status = IO_ReadWin32FileToBuffer(RequestFile, (PVOID*)&Request, &RequestLength);
    Hr = Err_NtStatusToHr(Status);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Details = "Unable to create the requested test identity; use Normal, LowIntegrity or FilteredAdmin.";
    Status = CreateTestToken(Identity, &Token);
    Hr = Err_NtStatusToHr(Status);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Token != NULL)
    {
        Details = "Unable to impersonate the requested test identity.";
        Status = NtOpenThreadToken(NtCurrentThread(), TOKEN_IMPERSONATE, TRUE, &Previous);
        if (!NT_SUCCESS(Status) && Status != STATUS_NO_TOKEN)
        {
            Hr = Err_NtStatusToHr(Status);
            goto Exit;
        }
        Hr = Err_NtStatusToHr(PS_Impersonate(Token));
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Impersonating = TRUE;
    }
    Hr = CuaCallServerRaw(Session, Request != NULL ? Request : (PCUCHAR)"", RequestLength,
                          TimeoutMs, &Reply, &ReplyLength, &Sent);
    Details = Sent != FALSE ? "The RPC outcome is unknown. Inspect before repeating an operation."
                           : "The RPC call failed before submission.";
Exit:
    if (Impersonating != FALSE && !NT_SUCCESS(PS_Impersonate(Previous)))
    {
        __fastfail(FAST_FAIL_FATAL_APP_EXIT);
    }
    if (Previous != NULL)
    {
        NtClose(Previous);
    }
    if (Token != NULL)
    {
        NtClose(Token);
    }
    if (SUCCEEDED(Hr))
    {
        while (Offset < ReplyLength)
        {
            Status = IO_WriteFile(_Inline_GetStdHandle(STD_OUTPUT_HANDLE), NULL,
                                  Reply + Offset, ReplyLength - Offset, &Written);
            if (!NT_SUCCESS(Status) || Written == 0 || Written > ReplyLength - Offset)
            {
                goto Cleanup;
            }
            Offset += Written;
        }
        Status = IO_WriteFile(_Inline_GetStdHandle(STD_OUTPUT_HANDLE), NULL, "\n", 1, &Written);
        Result = NT_SUCCESS(Status) && Written == 1 ? 0 : 1;
    } else
    {
        Initialized = RoInitialize(RO_INIT_MULTITHREADED);
        if (SUCCEEDED(Initialized))
        {
            Error = BuildErrorOutput(Hr, "%s", Details);
            if (Error != NULL)
            {
                Hr = Error->lpVtbl->QueryInterface(Error, &IID_IJsonValue, (PVOID*)&Value);
                if (SUCCEEDED(Hr))
                {
                    Util_WriteJson(Value, _Inline_GetStdHandle(STD_OUTPUT_HANDLE));
                }
            }
        }
    }
Cleanup:
    if (Value != NULL)
    {
        Value->lpVtbl->Release(Value);
    }
    if (Error != NULL)
    {
        Error->lpVtbl->Release(Error);
    }
    if (SUCCEEDED(Initialized))
    {
        RoUninitialize();
    }
    Mem_Free(Reply);
    Mem_Free(Request);
    return Result;
}
