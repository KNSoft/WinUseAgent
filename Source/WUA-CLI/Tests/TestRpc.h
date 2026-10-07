#pragma once

EXTERN_C_START

int
TestRpcMain(
    _In_ int Count,
    _In_reads_(Count) wchar_t** Arguments);

EXTERN_C_END
