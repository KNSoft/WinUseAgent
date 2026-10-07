#pragma once

#include <KNSoft/NDK/NDK.h>

#define MLE_API
#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>

typedef struct IUIAutomationElement IUIAutomationElement;
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uiautomationcore.lib")
#pragma comment(lib, "KNSoft.NDK.Win32u.lib")
#pragma comment(lib, "Version.lib")

#include <roapi.h>
#pragma comment(lib, "runtimeobject.lib")

#include "Utils/Utils.h"

typedef struct _WUA_KV
{
    PCWSTR Key;
    PVOID Value;
} WUA_KV, *PWUA_KV;

typedef
_Function_class_(WUA_COMMAND_FN)
_Ret_maybenull_
IJsonObject*
WUA_COMMAND_FN(VOID);

typedef enum
{
    WUA_Parameter_String = 0,   // PWSTR
    WUA_Parameter_HexU32,       // UINT
    WUA_Parameter_IntU32,       // UINT
    WUA_Parameter_Int32,        // INT
    WUA_Parameter_Bool,         // LOGICAL
    WUA_Parameter_Arguments,    // PWSTR
} WUA_COMMAND_PARAMETER_TYPE;

typedef struct _WUA_COMMAND_PARAMETER
{
    PCWSTR Name;
    PVOID* Buffer;
    struct
    {
        ULONG SizeOfBuffer : 4;
        WUA_COMMAND_PARAMETER_TYPE Type : 4;
        ULONG Required : 1;
    };
} WUA_COMMAND_PARAMETER, *PWUA_COMMAND_PARAMETER;

#define DEF_PARAMETER_ENTRY(Name, Type, Required) { L""#Name, (PVOID*)&Name, { sizeof(Name), WUA_Parameter_##Type, \
    Required} }

typedef struct _WUA_COMMAND
{
    PWUA_COMMAND_PARAMETER Parameters;
    ULONG ParameterCount;
    WUA_COMMAND_FN* Func;
} WUA_COMMAND, *PWUA_COMMAND;

EXTERN_C_START

_Success_(return == NULL)
_Ret_maybenull_
PCWSTR
InitCommandParameters(
    _Inout_ PWUA_COMMAND Command,
    _In_ int Argc,
    _In_reads_(Argc) _Pre_z_ wchar_t** Argv);

_Ret_maybenull_
IJsonObject*
BuildErrorOutput(
    _In_ HRESULT Hr,
    _In_opt_ _Printf_format_string_ PCSTR DetailsFormat,
    ...);

// Consumes Result, including on failure; returns an owned reference.
_Ret_maybenull_
IJsonObject*
BuildSuccessOutput(
    _In_opt_ IUnknown* Result);

NTSTATUS
BuildCommandLineWithProgram(
    _In_ PCWSTR Program,
    _In_opt_ PCWSTR Arguments,
    _Outptr_result_z_ PWSTR* CommandLine);

EXTERN_C_END
