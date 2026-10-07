#pragma once

#include "../pch.h"

EXTERN_C_START

HRESULT
Util_WriteJson(
    _In_ IJsonValue* Value,
    _In_ HANDLE File);

_Success_(return != FALSE)
LOGICAL
Util_Proc_GetProductName(
    _In_ PCWSTR File,
    _Out_writes_(BufferCch) PWSTR Buffer,
    _In_ ULONG BufferCch);

EXTERN_C_END
