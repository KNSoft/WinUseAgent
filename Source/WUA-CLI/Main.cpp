#include "pch.h"

/* List of all tools and commands */

#define _DECL_COMMAND(Tool, Command)\
extern WUA_COMMAND Tool##_##Command

#define _DEF_COMMAND(Tool, Command) { L#Command, &Tool##_##Command }

/* File */

#define DECL_COMMAND(Command) _DECL_COMMAND(File, Command)
#define DEF_COMMAND(Command) _DEF_COMMAND(File, Command)

DECL_COMMAND(Recycle);

static WUA_KV Tool_File[] = {
    DEF_COMMAND(Recycle),
    { NULL, NULL }
};

#undef DECL_COMMAND
#undef DEF_COMMAND

/* Input */

#define DECL_COMMAND(Command) _DECL_COMMAND(Input, Command)
#define DEF_COMMAND(Command) _DEF_COMMAND(Input, Command)

DECL_COMMAND(Mouse);
DECL_COMMAND(Text);

static WUA_KV Tool_Input[] = {
    DEF_COMMAND(Mouse),
    DEF_COMMAND(Text),
    { NULL, NULL }
};

#undef DECL_COMMAND
#undef DEF_COMMAND

/* Window */

#define DECL_COMMAND(Command) _DECL_COMMAND(Window, Command)
#define DEF_COMMAND(Command) _DEF_COMMAND(Window, Command)

DECL_COMMAND(Capture);
DECL_COMMAND(List);
DECL_COMMAND(Operation);

static WUA_KV Tool_Window[] = {
    DEF_COMMAND(Capture),
    DEF_COMMAND(List),
    DEF_COMMAND(Operation),
    { NULL, NULL }
};

#undef DECL_COMMAND
#undef DEF_COMMAND

/* All Tools */

#define DEF_TOOL(Tool) { L#Tool, &Tool_##Tool }

static WUA_KV Tools[] = {
    DEF_TOOL(File),
    DEF_TOOL(Input),
    DEF_TOOL(Window),
};

cJSON*
BuildErrorOutput(
    _In_ HRESULT Hr,
    _In_opt_ _Printf_format_string_ PCSTR DetailsFormat,
    ...)
{
    CHAR szText[300];
    ULONG uCchText;
    cJSON *j;
    PCWSTR pszHr;

    j = cJSON_CreateObject();

    cJSON_AddBoolToObject(j, "ok", FALSE);
    cJSON_AddNumberToObject(j, "hresult", Hr);
    pszHr = Err_GetHrInfo(Hr);
    if (pszHr != NULL && Str_W2U(szText, pszHr) > 0)
    {
        cJSON_AddStringToObject(j, "hresult_text", szText);
    } else
    {
        cJSON_AddNullToObject(j, "hresult_text");
    }
    if (DetailsFormat != NULL)
    {
        va_list ArgList;
        va_start(ArgList, DetailsFormat);
        uCchText = Str_VPrintfA(szText, DetailsFormat, ArgList);
        va_end(ArgList);
        if (uCchText > 0)
        {
            cJSON_AddStringToObject(j, "details", szText);
            goto _Exit;
        }
    }
    cJSON_AddNullToObject(j, "details");

_Exit:
    return j;
}

cJSON*
BuildSuccessOutput(
    _In_opt_ cJSON* Result)
{
    cJSON* j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "ok", TRUE);
    if (Result != NULL)
    {
        cJSON_AddItemToObject(j, "result", Result);
    } else
    {
        cJSON_AddNullToObject(j, "result");
    }
    return j;
}

static
_Success_(return != FALSE)
LOGICAL
FindCommand(
    _In_ PCWSTR ToolName,
    _In_ PCWSTR CommandName,
    _Out_ PWUA_COMMAND * Command)
{
    for (ULONG i = 0; i < ARRAYSIZE(Tools); i++)
    {
        if (_wcsicmp(Tools[i].Key, ToolName) == 0)
        {
            for (PWUA_KV j = reinterpret_cast<PWUA_KV>(Tools[i].Value); j->Key != NULL; j++)
            {
                if (_wcsicmp(j->Key, CommandName) == 0)
                {
                    *Command = reinterpret_cast<PWUA_COMMAND>(j->Value);
                    return TRUE;
                }
            }
        }
    }
    return FALSE;
}

static
_Success_(return == NULL)
_Ret_maybenull_
PCWSTR
InitCommandParameters(
    _Inout_ PWUA_COMMAND Command,
    _In_ int argc,
    _In_reads_(argc) _Pre_z_ wchar_t** argv)
{
    PCWSTR pszArg, pszValue;
    ULONG uKeyLen;

    for (int i = 0; i < argc; i++)
    {
        pszArg = argv[i];
        pszValue = NULL;
        for (ULONG j = 0; j < Command->ParameterCount; j++)
        {
            uKeyLen = (ULONG)wcslen(Command->Parameters[j].Name);
            if (_wcsnicmp(pszArg, Command->Parameters[j].Name, uKeyLen) == 0)
            {
                pszArg += uKeyLen;
                if (*pszArg == L'=')
                {
                    pszValue = pszArg + 1;
                } else if (*pszArg == UNICODE_NULL)
                {
                    pszValue = L"";
                } else
                {
                    continue;
                }
                if (Command->Parameters[j].Type == WUA_Parameter_String &&
                    Command->Parameters[j].SizeOfBuffer == sizeof(PVOID))
                {
                    PWSTR* p = reinterpret_cast<PWSTR*>(Command->Parameters[j].Buffer);
                    *p = const_cast<PWSTR>(pszValue);
                } else if (Command->Parameters[j].Type == WUA_Parameter_HexU32 &&
                           Command->Parameters[j].SizeOfBuffer == sizeof(UINT))
                {
                    PUINT p = reinterpret_cast<PUINT>(Command->Parameters[j].Buffer);
                    if (!Str_HexToUIntW(pszValue, p))
                    {
                        pszValue = NULL;
                        break;
                    }

                } else if (Command->Parameters[j].Type == WUA_Parameter_IntU32 &&
                           Command->Parameters[j].SizeOfBuffer == sizeof(UINT))
                {
                    PUINT p = reinterpret_cast<PUINT>(Command->Parameters[j].Buffer);
                    if (!Str_DecToUIntW(pszValue, p))
                    {
                        pszValue = NULL;
                        break;
                    }
                } else if (Command->Parameters[j].Type == WUA_Parameter_Int32 &&
                           Command->Parameters[j].SizeOfBuffer == sizeof(INT))
                {
                    PINT p = reinterpret_cast<PINT>(Command->Parameters[j].Buffer);
                    if (!Str_DecToIntW(pszValue, p))
                    {
                        pszValue = NULL;
                        break;
                    }
                } else if (Command->Parameters[j].Type == WUA_Parameter_Bool &&
                           Command->Parameters[j].SizeOfBuffer == sizeof(LOGICAL))
                {
                    *reinterpret_cast<PLOGICAL>(Command->Parameters[j].Buffer) = TRUE;
                } else
                {
                    pszValue = NULL;
                    break;
                }
                Command->Parameters[j].SizeOfBuffer = 0;
                break;
            }
        }
        if (pszValue == NULL)
        {
            return argv[i];
        }
    }

    for (ULONG i = 0; i < Command->ParameterCount; i++)
    {
        if (Command->Parameters[i].Required && Command->Parameters[i].SizeOfBuffer != 0)
        {
            return Command->Parameters[i].Name;
        }
    }
    return NULL;
}

int
_cdecl
wmain(
    _In_ int argc,
    _In_reads_(argc) _Pre_z_ wchar_t** argv)
{
    cJSON* j;
    BOOL CPSet;
    UINT OriginalCP;
    PCWSTR InvalidParameter;
    PWUA_COMMAND Command;
    NTSTATUS Status;

    OriginalCP = GetConsoleOutputCP();
    CPSet = SetConsoleOutputCP(CP_UTF8);

    if (argc >= 3 && FindCommand(argv[1], argv[2], &Command))
    {
        InvalidParameter = InitCommandParameters(Command, argc - 3, argv + 3);
        if (InvalidParameter == NULL)
        {
            j = Command->Func();
        } else
        {
            j = BuildErrorOutput(E_INVALIDARG, "Parameter \"%ls\" is invalid or required.", InvalidParameter);
        }
    } else
    {
        j = BuildErrorOutput(E_INVALIDARG, "Invalid parameters, see README.md for more information.");
    }

    PSTR JsonText = cJSON_Print(j);
    Status = IO_WriteFile(_Inline_GetStdHandle(STD_OUTPUT_HANDLE),
                          NULL,
                          JsonText,
                          (ULONG)strlen(JsonText));
    cJSON_free(JsonText);
    cJSON_Delete(j);

    if (CPSet)
    {
        SetConsoleOutputCP(OriginalCP);
    }
    return NT_SUCCESS(Status) ? 0 : Status;
}
