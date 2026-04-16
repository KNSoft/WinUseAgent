#pragma once

#include <KNSoft/NDK/NDK.h>

#define MLE_API
#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>

#include <gdiplus.h>
#include <uiautomation.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uiautomationcore.lib")

#include "../3rdParty/cJSON/cJSON.h"

#include "Utils/Utils.h"

typedef struct _WUA_KV
{
    PCWSTR Key;
    PVOID Value;
} WUA_KV, *PWUA_KV;

typedef
_Function_class_(WUA_COMMAND_FN)
_Ret_notnull_
cJSON*
WUA_COMMAND_FN(VOID);

typedef enum
{
    WUA_Parameter_String = 0,   // PWSTR
    WUA_Parameter_HexU32,       // UINT
    WUA_Parameter_IntU32,       // UINT
    WUA_Parameter_Int32,        // INT
    WUA_Parameter_Bool,         // LOGICAL
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

#define DEF_PARAMETER_ENTRY(Name, Type, Required) { L#Name, reinterpret_cast<PVOID*>(&Name), { sizeof(Name), WUA_Parameter_##Type, Required} }

typedef struct _WUA_COMMAND
{
    PWUA_COMMAND_PARAMETER Parameters;
    ULONG ParameterCount;
    WUA_COMMAND_FN* Func;
} WUA_COMMAND, *PWUA_COMMAND;

cJSON*
BuildErrorOutput(
    _In_ HRESULT Hr,
    _In_opt_ _Printf_format_string_ PCSTR DetailsFormat,
    ...);

cJSON*
BuildSuccessOutput(
    _In_opt_ cJSON* Result);
