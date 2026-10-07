#pragma once

EXTERN_C_START

int
WuaCliMain(
    _In_ int Argc,
    _In_reads_(Argc) wchar_t** Argv,
    _In_ PCWSTR Tool);

EXTERN_C_END
