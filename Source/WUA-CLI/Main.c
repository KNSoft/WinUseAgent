#include "pch.h"
#include "Tools/CUA/Cli.h"

/* List of all tools and commands */

#define _DECL_COMMAND(Tool, Command) extern WUA_COMMAND Tool##_##Command

#define _DEF_COMMAND(Tool, Command) { L""#Command, &Tool##_##Command }

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

/* Run */

#define DECL_COMMAND(Command) _DECL_COMMAND(Run, Command)
#define DEF_COMMAND(Command) _DEF_COMMAND(Run, Command)

DECL_COMMAND(Elevate);
DECL_COMMAND(Restrict);

static WUA_KV Tool_Run[] = {
    DEF_COMMAND(Elevate),
    DEF_COMMAND(Restrict),
    { NULL, NULL }
};

#undef DECL_COMMAND
#undef DEF_COMMAND

/* Text */

#define DECL_COMMAND(Command) _DECL_COMMAND(Text, Command)
#define DEF_COMMAND(Command) _DEF_COMMAND(Text, Command)

DECL_COMMAND(Inspect);

static WUA_KV Tool_Text[] = {
    DEF_COMMAND(Inspect),
    { NULL, NULL }
};

#undef DECL_COMMAND
#undef DEF_COMMAND

/* All Tools */

#define DEF_TOOL(Tool) { L""#Tool, &Tool_##Tool }

static WUA_KV Tools[] = {
    DEF_TOOL(File),
    DEF_TOOL(Run),
    DEF_TOOL(Text),
};

_Ret_maybenull_
IJsonObject*
BuildErrorOutput(
    _In_ HRESULT Hr,
    _In_opt_ _Printf_format_string_ PCSTR DetailsFormat,
    ...)
{
    CHAR szText[300];
    ULONG uCchText = 0;
    IJsonObject* j = NULL;
    IJsonValueStatics* Factory = NULL;
    IJsonValueStatics2* NullFactory = NULL;
    HRESULT Result;
    PCWSTR pszHr;

    Result = Data_JsonCreateObject(&j);
    if (FAILED(Result))
    {
        goto Exit;
    }
    Result = Data_JsonGetValueFactory(&Factory);
    if (FAILED(Result))
    {
        goto Exit;
    }
    Result = Factory->lpVtbl->QueryInterface(Factory, &IID_IJsonValueStatics2, (PVOID*)&NullFactory);
    if (FAILED(Result))
    {
        goto Exit;
    }
    Result = Data_JsonObjectSetBoolean(Factory, j, L"ok", FALSE);
    if (FAILED(Result))
    {
        goto Exit;
    }
    Result = Data_JsonObjectSetNumber(Factory, j, L"hresult", Hr);
    if (FAILED(Result))
    {
        goto Exit;
    }
    pszHr = Err_GetHrInfo(Hr);
    Result = pszHr != NULL
                 ? Data_JsonObjectSetString(Factory, j, L"hresult_text", pszHr, (ULONG)wcslen(pszHr))
                 : Data_JsonObjectSetNull(NullFactory, j, L"hresult_text");
    if (FAILED(Result))
    {
        goto Exit;
    }
    if (DetailsFormat != NULL)
    {
        va_list ArgList;
        va_start(ArgList, DetailsFormat);
        uCchText = Str_VPrintfA(szText, DetailsFormat, ArgList);
        va_end(ArgList);
    }
    Result = uCchText != 0 ? Data_JsonObjectSetStringUtf8(Factory, j, L"details", szText, uCchText)
                           : Data_JsonObjectSetNull(NullFactory, j, L"details");
Exit:
    if (NullFactory != NULL)
    {
        NullFactory->lpVtbl->Release(NullFactory);
    }
    if (Factory != NULL)
    {
        Factory->lpVtbl->Release(Factory);
    }
    if (FAILED(Result) && j != NULL)
    {
        j->lpVtbl->Release(j);
        j = NULL;
    }
    return j;
}

_Ret_maybenull_
IJsonObject*
BuildSuccessOutput(
    _In_opt_ IUnknown* Result)
{
    IJsonObject* j = NULL;
    IJsonValue* Value = NULL;
    IJsonValueStatics* Factory = NULL;
    IJsonValueStatics2* NullFactory = NULL;
    HRESULT Hr;

    Hr = Data_JsonCreateObject(&j);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonGetValueFactory(&Factory);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetBoolean(Factory, j, L"ok", TRUE);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Result != NULL)
    {
        Hr = Result->lpVtbl->QueryInterface(Result, &IID_IJsonValue, (PVOID*)&Value);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = Data_JsonObjectSetValue(j, L"result", Value);
        if (FAILED(Hr))
        {
            goto Exit;
        }
    } else
    {
        Hr = Factory->lpVtbl->QueryInterface(Factory, &IID_IJsonValueStatics2, (PVOID*)&NullFactory);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = Data_JsonObjectSetNull(NullFactory, j, L"result");
        if (FAILED(Hr))
        {
            goto Exit;
        }
    }
Exit:
    if (NullFactory != NULL)
    {
        NullFactory->lpVtbl->Release(NullFactory);
    }
    if (Factory != NULL)
    {
        Factory->lpVtbl->Release(Factory);
    }
    if (Value != NULL)
    {
        Value->lpVtbl->Release(Value);
    }
    if (Result != NULL)
    {
        Result->lpVtbl->Release(Result);
    }
    if (FAILED(Hr) && j != NULL)
    {
        j->lpVtbl->Release(j);
        j = NULL;
    }
    return j;
}

NTSTATUS
BuildCommandLineWithProgram(
    _In_ PCWSTR Program,
    _In_opt_ PCWSTR Arguments,
    _Outptr_result_z_ PWSTR* CommandLine)
{
    PWSTR QuotedProgram;
    PWSTR Buffer;
    SIZE_T ProgramCch;
    SIZE_T ArgumentsCch;
    NTSTATUS Status;

    *CommandLine = NULL;
    if (wcschr(Program, L'"') != NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = PS_ArgvToCommandLineW(1, &Program, &QuotedProgram);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (Arguments == NULL || *Arguments == UNICODE_NULL)
    {
        *CommandLine = QuotedProgram;
        return STATUS_SUCCESS;
    }

    // Arguments is the raw tail after "--"; preserve its quoting and spacing.
    ProgramCch = wcslen(QuotedProgram);
    ArgumentsCch = wcslen(Arguments);
    if (ProgramCch > MAXSIZE_T / sizeof(WCHAR) - 2 || ArgumentsCch > MAXSIZE_T / sizeof(WCHAR) - ProgramCch - 2)
    {
        PS_FreeCommandLineBuffer(QuotedProgram);
        return STATUS_INTEGER_OVERFLOW;
    }
    Buffer = (PWSTR)(Mem_Alloc((ProgramCch + ArgumentsCch + 2) * sizeof(WCHAR)));
    if (Buffer == NULL)
    {
        PS_FreeCommandLineBuffer(QuotedProgram);
        return STATUS_NO_MEMORY;
    }
    RtlCopyMemory(Buffer, QuotedProgram, ProgramCch * sizeof(WCHAR));
    Buffer[ProgramCch] = L' ';
    RtlCopyMemory(Buffer + ProgramCch + 1, Arguments, (ArgumentsCch + 1) * sizeof(WCHAR));
    PS_FreeCommandLineBuffer(QuotedProgram);
    *CommandLine = Buffer;
    return STATUS_SUCCESS;
}

static
_Success_(return != FALSE)
LOGICAL
FindCommand(
    _In_ PCWSTR ToolName,
    _In_ PCWSTR CommandName,
    _Out_ PWUA_COMMAND* Command)
{
    for (ULONG i = 0; i < ARRAYSIZE(Tools); i++)
    {
        if (_wcsicmp(Tools[i].Key, ToolName) == 0)
        {
            for (PWUA_KV j = (PWUA_KV)(Tools[i].Value); j->Key != NULL; j++)
            {
                if (_wcsicmp(j->Key, CommandName) == 0)
                {
                    *Command = (PWUA_COMMAND)(j->Value);
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
SetCommandArguments(
    _Inout_ PWUA_COMMAND Command,
    _In_ PCWSTR InvalidParameter,
    _In_ PCWSTR Arguments)
{
    for (ULONG i = 0; i < Command->ParameterCount; i++)
    {
        if (Command->Parameters[i].Type == WUA_Parameter_Arguments &&
            Command->Parameters[i].SizeOfBuffer == sizeof(PWSTR))
        {
            PWSTR* p = (PWSTR*)(Command->Parameters[i].Buffer);
            *p = (PWSTR)(Arguments);
            Command->Parameters[i].SizeOfBuffer = 0;
            return NULL;
        }
    }
    return InvalidParameter;
}

static
PCWSTR
SkipCommandLineSpaces(
    _In_ PCWSTR p)
{
    while (*p == L' ' || *p == L'\t')
    {
        p++;
    }
    return p;
}

static
LOGICAL
IsCommandLineTokenDelimiter(
    _In_ WCHAR Ch)
{
    return Ch == UNICODE_NULL || Ch == L' ' || Ch == L'\t';
}

static
PCWSTR
FindRawArguments(
    VOID)
{
    PCWSTR p;
    LOGICAL InQuotes;
    LOGICAL TokenStart;
    ULONG Backslashes;

    p = NtCurrentPeb()->ProcessParameters->CommandLine.Buffer;
    InQuotes = FALSE;
    TokenStart = TRUE;
    Backslashes = 0;
    for (; *p != UNICODE_NULL; p++)
    {
        if (InQuotes == FALSE && (*p == L' ' || *p == L'\t'))
        {
            TokenStart = TRUE;
            Backslashes = 0;
            continue;
        }

        if (*p == L'\\')
        {
            Backslashes++;
            TokenStart = FALSE;
            continue;
        }

        if (*p == L'"')
        {
            if ((Backslashes % 2) == 0)
            {
                InQuotes = InQuotes == FALSE;
            }
            TokenStart = FALSE;
            Backslashes = 0;
            continue;
        }

        Backslashes = 0;
        if (InQuotes == FALSE && TokenStart != FALSE && p[0] == L'-' && p[1] == L'-' &&
            IsCommandLineTokenDelimiter(p[2]) != FALSE)
        {
            return SkipCommandLineSpaces(p + 2);
        }

        TokenStart = FALSE;
    }
    return L"";
}

_Success_(return == NULL)
_Ret_maybenull_
PCWSTR
InitCommandParameters(
    _Inout_ PWUA_COMMAND Command,
    _In_ int argc,
    _In_reads_(argc) _Pre_z_ wchar_t** argv)
{
    PCWSTR Argument;
    PCWSTR Value;
    ULONG KeyLength;
    PWUA_COMMAND_PARAMETER Parameter;

    for (int i = 0; i < argc; i++)
    {
        Argument = argv[i];
        if (wcscmp(Argument, L"--") == 0)
        {
            Value = SetCommandArguments(Command, Argument, FindRawArguments());
            if (Value != NULL)
            {
                return Value;
            }
            break;
        }
        if (*Argument == L'-')
        {
            Argument++;
        }
        Value = NULL;
        for (ULONG j = 0; j < Command->ParameterCount; j++)
        {
            Parameter = &Command->Parameters[j];
            KeyLength = (ULONG)wcslen(Parameter->Name);
            if (_wcsnicmp(Argument, Parameter->Name, KeyLength) != 0 ||
                (Argument[KeyLength] != L'=' && Argument[KeyLength] != UNICODE_NULL))
            {
                continue;
            }
            if (Parameter->SizeOfBuffer == 0)
            {
                return argv[i];
            }
            Value = Argument + KeyLength;
            if (*Value == L'=')
            {
                Value++;
            }
            if (Parameter->Type == WUA_Parameter_String && Parameter->SizeOfBuffer == sizeof(PWSTR))
            {
                *(PWSTR*)Parameter->Buffer = (PWSTR)Value;
            } else if (Parameter->Type == WUA_Parameter_HexU32 && Parameter->SizeOfBuffer == sizeof(UINT))
            {
                if (Str_HexToUIntW(Value, (PUINT)Parameter->Buffer) == FALSE)
                {
                    return argv[i];
                }
            } else if (Parameter->Type == WUA_Parameter_IntU32 && Parameter->SizeOfBuffer == sizeof(UINT))
            {
                if (Str_DecToUIntW(Value, (PUINT)Parameter->Buffer) == FALSE)
                {
                    return argv[i];
                }
            } else if (Parameter->Type == WUA_Parameter_Int32 && Parameter->SizeOfBuffer == sizeof(INT))
            {
                if (Str_DecToIntW(Value, (PINT)Parameter->Buffer) == FALSE)
                {
                    return argv[i];
                }
            } else if (Parameter->Type == WUA_Parameter_Bool && Parameter->SizeOfBuffer == sizeof(LOGICAL))
            {
                if (*Value == UNICODE_NULL || _wcsicmp(Value, L"true") == 0)
                {
                    *(PLOGICAL)Parameter->Buffer = TRUE;
                } else if (_wcsicmp(Value, L"false") == 0)
                {
                    *(PLOGICAL)Parameter->Buffer = FALSE;
                } else
                {
                    return argv[i];
                }
            } else
            {
                return argv[i];
            }
            Parameter->SizeOfBuffer = 0;
            break;
        }
        if (Value == NULL)
        {
            return argv[i];
        }
    }
    for (ULONG i = 0; i < Command->ParameterCount; i++)
    {
        if (Command->Parameters[i].Required != FALSE && Command->Parameters[i].SizeOfBuffer != 0)
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
    IJsonObject* j;
    IJsonValue* Value = NULL;
    HSTRING_HEADER Header;
    HSTRING Name;
    boolean Ok = FALSE;
    BOOL CPSet;
    UINT OriginalCP;
    PCWSTR InvalidParameter;
    PWUA_COMMAND Command = NULL;
    HRESULT Hr;

    if (argc >= 2 && (_wcsicmp(argv[1], L"CUA") == 0 ||
                      (argc >= 3 && _wcsicmp(argv[1], L"Run") == 0 && _wcsicmp(argv[2], L"Locate") == 0)))
    {
        return WuaCliMain(argc - 2, argv + 2, argv[1]);
    }

    if (argc >= 3)
    {
        FindCommand(argv[1], argv[2], &Command);
    }
    Hr = RoInitialize(RO_INIT_SINGLETHREADED);
    if (FAILED(Hr))
    {
        return 1;
    }

    OriginalCP = GetConsoleOutputCP();
    CPSet = SetConsoleOutputCP(CP_UTF8);

    if (Command != NULL)
    {
        InvalidParameter = InitCommandParameters(Command, argc - 3, argv + 3);
        if (InvalidParameter == NULL)
        {
#ifdef _DEBUG
            PS_DelayExec(2000);
#endif
            j = Command->Func();
        } else
        {
            j = BuildErrorOutput(E_INVALIDARG, "Parameter \"%ls\" is invalid or required.", InvalidParameter);
        }
    } else
    {
        j = BuildErrorOutput(E_INVALIDARG, "Invalid parameters, see README.md for more information.");
    }

    if (j == NULL)
    {
        Hr = E_OUTOFMEMORY;
    } else
    {
        Hr = _Inline_WindowsCreateStringReference(L"ok", 2, &Header, &Name);
        if (SUCCEEDED(Hr))
        {
            Hr = j->lpVtbl->GetNamedBoolean(j, Name, &Ok);
        }
        if (SUCCEEDED(Hr))
        {
            Hr = j->lpVtbl->QueryInterface(j, &IID_IJsonValue, (PVOID*)&Value);
        }
        if (SUCCEEDED(Hr))
        {
            Hr = Util_WriteJson(Value, _Inline_GetStdHandle(STD_OUTPUT_HANDLE));
            Value->lpVtbl->Release(Value);
        }
    }
    if (j != NULL)
    {
        j->lpVtbl->Release(j);
    }
    RoUninitialize();

    if (CPSet != FALSE)
    {
        SetConsoleOutputCP(OriginalCP);
    }
    return SUCCEEDED(Hr) && Ok != FALSE ? 0 : 1;
}
