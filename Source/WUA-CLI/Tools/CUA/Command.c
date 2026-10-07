#include "pch.h"
#include "Command.h"
#include <math.h>

const CUA_PARAMETER_INFO CuaParameterInfo[CuaParamCount] = {
    { L"handle", L"handle", CuaStringParameter, FALSE },
    { L"backend", L"backend", CuaStringParameter, TRUE },
    { L"uia", L"uia", CuaBooleanParameter, TRUE },
    { L"timeout_ms", L"timeoutms", CuaNumberParameter, TRUE },
    { L"max_nodes", L"maxnodes", CuaNumberParameter, TRUE },
    { L"outfile", L"outfile", CuaStringParameter, FALSE },
    { L"runtime_id", L"runtimeid", CuaStringParameter, FALSE },
    { L"x", L"x", CuaNumberParameter, FALSE },
    { L"y", L"y", CuaNumberParameter, FALSE },
    { L"to_x", L"tox", CuaNumberParameter, FALSE },
    { L"to_y", L"toy", CuaNumberParameter, FALSE },
    { L"button", L"button", CuaStringParameter, FALSE },
    { L"axis", L"axis", CuaStringParameter, FALSE },
    { L"delta", L"delta", CuaNumberParameter, FALSE },
    { L"duration_ms", L"durationms", CuaNumberParameter, FALSE },
    { L"keys", L"keys", CuaKeysParameter, FALSE },
    { L"text", L"text", CuaStringParameter, FALSE },
    { L"method", L"method", CuaStringParameter, FALSE },
    { L"path", L"path", CuaStringParameter, FALSE },
    { L"session", L"session", CuaStringParameter, TRUE },
    { L"width", L"width", CuaNumberParameter, FALSE },
    { L"height", L"height", CuaNumberParameter, FALSE },
    { L"show", L"show", CuaBooleanParameter, FALSE }
};

typedef struct _CUA_METHOD_INFO
{
    CUA_METHOD Method;
    PCWSTR Name;
    PCWSTR Tool;
    PCWSTR Command;
    PCWSTR Operation;
    ULONGLONG Parameters;
} CUA_METHOD_INFO;

#define P(Name) CUA_PARAM_BIT(Name)
#define OBSERVE (P(OutFile) | P(Uia) | P(Backend) | P(Timeout) | P(MaxNodes))
#define TARGET P(Handle)
#define POINT (TARGET | P(X) | P(Y) | P(OutFile))
static const CUA_METHOD_INFO Methods[] = {
    { CuaMethodCapabilities, L"capabilities", NULL, NULL, NULL, 0 },
    { CuaMethodInspect, L"inspect", L"CUA", L"Inspect", L"", OBSERVE | P(Handle) },
    { CuaMethodLocate, L"locate", L"Run", L"Locate", L"", P(Path) },
    { CuaMethodActivate, L"activate", L"CUA", L"Window", L"Activate", P(Handle) | P(OutFile) },
    { CuaMethodMinimize, L"minimize", L"CUA", L"Window", L"Minimize", P(Handle) | P(OutFile) },
    { CuaMethodMaximize, L"maximize", L"CUA", L"Window", L"Maximize", P(Handle) | P(OutFile) },
    { CuaMethodCloseWindow, L"close_window", L"CUA", L"Window", L"Close", P(Handle) | P(OutFile) },
    { CuaMethodSessionEnableChildSession, L"session.enable_child_session", L"CUA", L"Session",
        L"EnableChildSession", 0 },
    { CuaMethodSessionList, L"session.list", L"CUA", L"Session", L"List", 0 },
    { CuaMethodSessionCreateChild, L"session.create_child", L"CUA", L"Session", L"CreateChild",
        P(Width) | P(Height) | P(Show) },
    { CuaMethodSessionPreview, L"session.preview", L"CUA", L"Session", L"Preview", P(Session) | P(Show) },
    { CuaMethodSessionDestroy, L"session.destroy", L"CUA", L"Session", L"Destroy", P(Session) },
    { CuaMethodClick, L"click", L"CUA", L"Mouse", L"Click", POINT | P(Button) },
    { CuaMethodDoubleClick, L"double_click", L"CUA", L"Mouse", L"DoubleClick", POINT | P(Button) },
    { CuaMethodMove, L"move", L"CUA", L"Mouse", L"Move", POINT },
    { CuaMethodScroll, L"scroll", L"CUA", L"Mouse", L"Wheel", POINT | P(Delta) | P(Axis) },
    { CuaMethodDrag, L"drag", L"CUA", L"Mouse", L"Drag", POINT | P(Button) | P(ToX) | P(ToY) | P(Duration) },
    { CuaMethodDown, L"down", L"CUA", L"Mouse", L"Down", POINT | P(Button) },
    { CuaMethodUp, L"up", L"CUA", L"Mouse", L"Up", POINT | P(Button) },
    { CuaMethodKeys, L"keys", L"CUA", L"Keys", L"", TARGET | P(Keys) | P(OutFile) },
    { CuaMethodType, L"type", L"CUA", L"Text", L"", TARGET | P(Text) | P(TextMethod) | P(OutFile) },
    { CuaMethodInvoke, L"invoke", L"CUA", L"Element", L"Invoke", TARGET | P(RuntimeId) | P(OutFile) },
    { CuaMethodSetValue, L"set_value", L"CUA", L"Element", L"SetValue", TARGET | P(RuntimeId) | P(Text) | P(OutFile) },
    { CuaMethodToggle, L"toggle", L"CUA", L"Element", L"Toggle", TARGET | P(RuntimeId) | P(OutFile) },
    { CuaMethodSelect, L"select", L"CUA", L"Element", L"Select", TARGET | P(RuntimeId) | P(OutFile) }
};
#undef POINT
#undef TARGET
#undef OBSERVE
#undef P

static
_Ret_maybenull_
const CUA_METHOD_INFO*
FindMethodInfo(
    _In_ CUA_METHOD Method)
{
    for (ULONG Index = 0; Index < ARRAYSIZE(Methods); Index++)
    {
        if (Methods[Index].Method == Method)
        {
            return &Methods[Index];
        }
    }
    return NULL;
}

PCWSTR
CuaMethodName(
    _In_ CUA_METHOD Method)
{
    const CUA_METHOD_INFO* Info = FindMethodInfo(Method);
    return Info != NULL ? Info->Name : L"";
}

CUA_METHOD
CuaFindMethod(
    _In_ PCWSTR Name)
{
    for (ULONG Index = 0; Index < ARRAYSIZE(Methods); Index++)
    {
        if (wcscmp(Name, Methods[Index].Name) == 0)
        {
            return Methods[Index].Method;
        }
    }
    return CuaMethodUnknown;
}

CUA_METHOD
CuaFindCommand(
    _In_ PCWSTR Tool,
    _In_ PCWSTR Command,
    _In_ PCWSTR Operation)
{
    for (ULONG Index = 0; Index < ARRAYSIZE(Methods); Index++)
    {
        if (Methods[Index].Tool != NULL && _wcsicmp(Tool, Methods[Index].Tool) == 0 &&
            _wcsicmp(Command, Methods[Index].Command) == 0 && _wcsicmp(Operation, Methods[Index].Operation) == 0)
        {
            return Methods[Index].Method;
        }
    }
    return CuaMethodUnknown;
}

LOGICAL
CuaHas(
    _In_ const CUA_PARAMETERS* Parameters,
    _In_ CUA_PARAMETER Parameter)
{
    return (Parameters->Present & (1ull << Parameter)) != 0;
}

PCWSTR
CuaString(
    _In_ const CUA_PARAMETERS* Parameters,
    _In_ CUA_PARAMETER Parameter,
    _In_ PCWSTR Default)
{
    PCWSTR Text = CuaHas(Parameters, Parameter) != FALSE
                      ? _Inline_WindowsGetStringRawBuffer(Parameters->Values[Parameter].String, NULL)
                      : Default;
    return Text;
}

LONG
CuaNumber(
    _In_ const CUA_PARAMETERS* Parameters,
    _In_ CUA_PARAMETER Parameter,
    _In_ LONG Default)
{
    return CuaHas(Parameters, Parameter) != FALSE ? Parameters->Values[Parameter].Number : Default;
}

LOGICAL
CuaBoolean(
    _In_ const CUA_PARAMETERS* Parameters,
    _In_ CUA_PARAMETER Parameter,
    _In_ LOGICAL Default)
{
    return CuaHas(Parameters, Parameter) != FALSE ? Parameters->Values[Parameter].Boolean : Default;
}

HRESULT
CuaSetString(
    _Inout_ CUA_PARAMETERS* Parameters,
    _In_ CUA_PARAMETER Parameter,
    _In_ PCWSTR Value)
{
    HSTRING Text = NULL;
    HRESULT Hr = _Inline_WindowsCreateString(Value, (UINT32)wcslen(Value), &Text);
    if (SUCCEEDED(Hr))
    {
        if (CuaHas(Parameters, Parameter) != FALSE)
        {
            _Inline_WindowsDeleteString(Parameters->Values[Parameter].String);
        }
        Parameters->Values[Parameter].String = Text;
        Parameters->Present |= 1ull << Parameter;
    }
    return Hr;
}

VOID
CuaFreeParameters(
    _Inout_ CUA_PARAMETERS* Parameters)
{
    for (ULONG Index = 0; Index < CuaParamCount; ++Index)
    {
        if (CuaHas(Parameters, (CUA_PARAMETER)Index) != FALSE && CuaParameterInfo[Index].Type == CuaStringParameter)
        {
            _Inline_WindowsDeleteString(Parameters->Values[Index].String);
        }
    }
    for (ULONG Index = 0; Index < Parameters->KeyCount; ++Index)
    {
        _Inline_WindowsDeleteString(Parameters->Keys[Index]);
    }
    RtlZeroMemory(Parameters, sizeof(*Parameters));
}

VOID
CuaFreeRequest(
    _Inout_ CUA_REQUEST* Request)
{
    CuaFreeParameters(&Request->Parameters);
    RtlZeroMemory(Request, sizeof(*Request));
}

static
HRESULT
RequestError(
    _Out_ CUA_ERROR* Error,
    _In_ PCWSTR Message)
{
    RtlZeroMemory(Error, sizeof(*Error));
    Error->Status = E_INVALIDARG;
    Str_CopyW(Error->Message, Message);
    return Error->Status;
}

static
HRESULT
ReadString(
    _In_ IJsonValue* Value,
    _Out_ HSTRING* String)
{
    BOOL Embedded = FALSE;
    HRESULT Hr;

    *String = NULL;
    Hr = Value->lpVtbl->GetString(Value, String);
    if (SUCCEEDED(Hr))
    {
        Hr = _Inline_WindowsStringHasEmbeddedNull(*String, &Embedded);
    }
    if (SUCCEEDED(Hr) && Embedded != FALSE)
    {
        Hr = E_INVALIDARG;
    }
    if (FAILED(Hr))
    {
        _Inline_WindowsDeleteString(*String);
        *String = NULL;
    }
    return Hr;
}

static
HRESULT
ReadParameter(
    _In_ IJsonValue* Value,
    _In_ CUA_PARAMETER Parameter,
    _Inout_ CUA_PARAMETERS* Parameters)
{
    IJsonArray* Array = NULL;
    IJsonVector* Vector = NULL;
    IJsonValue* Key = NULL;
    DOUBLE Number;
    boolean Boolean;
    UINT32 Count;
    HRESULT Hr = S_OK;
    Parameters->Present |= 1ull << Parameter;
    switch (CuaParameterInfo[Parameter].Type)
    {
    case CuaStringParameter:
        return ReadString(Value, &Parameters->Values[Parameter].String);
    case CuaNumberParameter:
        Hr = Value->lpVtbl->GetNumber(Value, &Number);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        if (isfinite(Number) == FALSE || floor(Number) != Number || Number < MINLONG32 || Number > MAXLONG32)
        {
            return E_INVALIDARG;
        }
        Parameters->Values[Parameter].Number = (LONG)Number;
        return S_OK;
    case CuaBooleanParameter:
        Hr = Value->lpVtbl->GetBoolean(Value, &Boolean);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Parameters->Values[Parameter].Boolean = Boolean != FALSE;
        return S_OK;
    case CuaKeysParameter:
        Hr = Value->lpVtbl->GetArray(Value, &Array);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = Array->lpVtbl->QueryInterface(Array, &IID_IJsonVector, (PVOID*)&Vector);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = Vector->lpVtbl->get_Size(Vector, &Count);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        if (Count > CUA_MAX_KEYS)
        {
            Hr = E_INVALIDARG;
            goto Exit;
        }
        Parameters->KeyCount = Count;
        for (UINT32 Index = 0; Index < Count; ++Index)
        {
            Hr = Vector->lpVtbl->GetAt(Vector, Index, &Key);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Hr = ReadString(Key, &Parameters->Keys[Index]);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Key->lpVtbl->Release(Key);
            Key = NULL;
        }
        break;
    }
Exit:
    if (Key != NULL)
    {
        Key->lpVtbl->Release(Key);
    }
    if (Vector != NULL)
    {
        Vector->lpVtbl->Release(Vector);
    }
    if (Array != NULL)
    {
        Array->lpVtbl->Release(Array);
    }
    return Hr;
}

static
HRESULT
ParseParameters(
    _In_ IJsonValue* Value,
    _Inout_ CUA_PARAMETERS* Parameters,
    _Inout_ CUA_ERROR* Error)
{
    IJsonObject* Object = NULL;
    IJsonPairIterable* Iterable = NULL;
    IJsonPairIterator* Iterator = NULL;
    IJsonPair* Pair = NULL;
    IJsonValue* Item = NULL;
    HSTRING Key = NULL;
    boolean More;
    HRESULT Hr;
    Hr = Value->lpVtbl->GetObject(Value, &Object);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Object->lpVtbl->QueryInterface(Object, &IID_IJsonPairIterable, (PVOID*)&Iterable);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Iterable->lpVtbl->First(Iterable, &Iterator);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Iterator->lpVtbl->get_HasCurrent(Iterator, &More);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    while (More != FALSE)
    {
        ULONG Index;
        Hr = Iterator->lpVtbl->get_Current(Iterator, &Pair);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = Pair->lpVtbl->get_Key(Pair, &Key);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        PCWSTR Name = _Inline_WindowsGetStringRawBuffer(Key, NULL);
        for (Index = 0; Index < CuaParamCount; ++Index)
        {
            if (wcscmp(Name, CuaParameterInfo[Index].Name) == 0)
            {
                break;
            }
        }
        if (Index == CuaParamCount)
        {
            Hr = RequestError(Error, L"Unknown command parameter. See Tools/CUA/README.md.");
            goto Exit;
        }
        Hr = Pair->lpVtbl->get_Value(Pair, &Item);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = ReadParameter(Item, (CUA_PARAMETER)Index, Parameters);
        if (FAILED(Hr))
        {
            RequestError(Error, L"A command parameter has an invalid JSON type or value.");
            goto Exit;
        }
        _Inline_WindowsDeleteString(Key);
        Key = NULL;
        Item->lpVtbl->Release(Item);
        Item = NULL;
        Pair->lpVtbl->Release(Pair);
        Pair = NULL;
        Hr = Iterator->lpVtbl->MoveNext(Iterator, &More);
        if (FAILED(Hr))
        {
            goto Exit;
        }
    }
Exit:
    _Inline_WindowsDeleteString(Key);
    if (Item != NULL)
    {
        Item->lpVtbl->Release(Item);
    }
    if (Pair != NULL)
    {
        Pair->lpVtbl->Release(Pair);
    }
    if (Iterator != NULL)
    {
        Iterator->lpVtbl->Release(Iterator);
    }
    if (Iterable != NULL)
    {
        Iterable->lpVtbl->Release(Iterable);
    }
    if (Object != NULL)
    {
        Object->lpVtbl->Release(Object);
    }
    return Hr;
}

HRESULT
CuaParseRequest(
    _In_ IJsonValue* Wire,
    _Out_ CUA_REQUEST* Request,
    _Out_ CUA_ERROR* Error)
{
    IJsonObject* Object = NULL;
    IJsonPairIterable* Iterable = NULL;
    IJsonPairIterator* Iterator = NULL;
    IJsonPair* Pair = NULL;
    IJsonValue* Value = NULL;
    HSTRING Key = NULL;
    HSTRING String = NULL;
    boolean More;
    LOGICAL HasParameters = FALSE;
    HRESULT Hr;
    RtlZeroMemory(Request, sizeof(*Request));
    RtlZeroMemory(Error, sizeof(*Error));
    Hr = Wire->lpVtbl->GetObject(Wire, &Object);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Object->lpVtbl->QueryInterface(Object, &IID_IJsonPairIterable, (PVOID*)&Iterable);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Iterable->lpVtbl->First(Iterable, &Iterator);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Iterator->lpVtbl->get_HasCurrent(Iterator, &More);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    while (More != FALSE)
    {
        Hr = Iterator->lpVtbl->get_Current(Iterator, &Pair);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = Pair->lpVtbl->get_Key(Pair, &Key);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = Pair->lpVtbl->get_Value(Pair, &Value);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        PCWSTR Name = _Inline_WindowsGetStringRawBuffer(Key, NULL);
        if (wcscmp(Name, L"params") == 0)
        {
            Hr = ParseParameters(Value, &Request->Parameters, Error);
            HasParameters = SUCCEEDED(Hr);
            if (FAILED(Hr))
            {
                goto Exit;
            }
        } else if (wcscmp(Name, L"method") == 0)
        {
            Hr = ReadString(Value, &String);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Request->Method = CuaFindMethod(_Inline_WindowsGetStringRawBuffer(String, NULL));
        } else
        {
            Hr = RequestError(Error, L"Unknown request field.");
            goto Exit;
        }
        _Inline_WindowsDeleteString(String);
        String = NULL;
        _Inline_WindowsDeleteString(Key);
        Key = NULL;
        Value->lpVtbl->Release(Value);
        Value = NULL;
        Pair->lpVtbl->Release(Pair);
        Pair = NULL;
        Hr = Iterator->lpVtbl->MoveNext(Iterator, &More);
        if (FAILED(Hr))
        {
            goto Exit;
        }
    }
    if (Request->Method == CuaMethodUnknown || HasParameters == FALSE)
    {
        Hr = RequestError(Error, L"Specify a known method and a params object.");
    }
Exit:
    _Inline_WindowsDeleteString(String);
    _Inline_WindowsDeleteString(Key);
    if (Value != NULL)
    {
        Value->lpVtbl->Release(Value);
    }
    if (Pair != NULL)
    {
        Pair->lpVtbl->Release(Pair);
    }
    if (Iterator != NULL)
    {
        Iterator->lpVtbl->Release(Iterator);
    }
    if (Iterable != NULL)
    {
        Iterable->lpVtbl->Release(Iterable);
    }
    if (Object != NULL)
    {
        Object->lpVtbl->Release(Object);
    }
    if (FAILED(Hr) && SUCCEEDED(Error->Status))
    {
        RequestError(Error, L"The request has an invalid type or value.");
    }
    return Hr;
}

HRESULT
CuaValidateParameters(
    _In_ const CUA_REQUEST* Request,
    _Out_ CUA_ERROR* Error)
{
    const CUA_METHOD_INFO* Info = FindMethodInfo(Request->Method);

    RtlZeroMemory(Error, sizeof(*Error));
    if (Info == NULL)
    {
        return RequestError(Error, L"Unknown command. See Tools/CUA/README.md.");
    }
    if ((Request->Parameters.Present & ~Info->Parameters) != 0)
    {
        return RequestError(Error, L"A parameter is not accepted by this command. See Tools/CUA/README.md.");
    }
    return S_OK;
}

HRESULT
CuaNewId(
    _Out_writes_(CUA_GUID_CCH) PWSTR Id)
{
    GUID Guid;
    WCHAR Text[39];
    HRESULT Hr;

    Id[0] = UNICODE_NULL;
    Hr = CoCreateGuid(&Guid);
    if (FAILED(Hr))
    {
        return Hr;
    }
    if (Str_FromGUIDUpperW(Text, ARRAYSIZE(Text), &Guid) == 0)
    {
        return E_FAIL;
    }
    RtlCopyMemory(Id, Text + 1, 36 * sizeof(WCHAR));
    Id[36] = UNICODE_NULL;
    return S_OK;
}

HRESULT
CuaBuildRequest(
    _In_ const CUA_REQUEST* Request,
    _Outptr_ IJsonValue** Wire)
{
    IJsonObject* Root = NULL;
    IJsonObject* Parameters = NULL;
    IJsonVector* Keys = NULL;
    IJsonValue* Value = NULL;
    IJsonValue* Item = NULL;
    IJsonValueStatics* Factory = NULL;
    const CUA_PARAMETERS* Args = &Request->Parameters;
    HRESULT Hr;
    *Wire = NULL;
    Hr = Data_JsonGetValueFactory(&Factory);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonCreateObject(&Root);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonCreateObject(&Parameters);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    PCWSTR Method = CuaMethodName(Request->Method);
    Hr = Data_JsonObjectSetString(Factory, Root, L"method", Method, (ULONG)wcslen(Method));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    for (ULONG Index = 0; Index < CuaParamCount; ++Index)
    {
        if (CuaHas(Args, (CUA_PARAMETER)Index) == FALSE)
        {
            continue;
        }
        PCWSTR Name = CuaParameterInfo[Index].Name;
        switch (CuaParameterInfo[Index].Type)
        {
        case CuaStringParameter:
        {
            ULONG Length;
            PCWSTR Text = _Inline_WindowsGetStringRawBuffer(Args->Values[Index].String, &Length);
            Hr = Data_JsonObjectSetString(Factory, Parameters, Name, Text, Length);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            break;
        }
        case CuaNumberParameter:
            Hr = Data_JsonObjectSetNumber(Factory, Parameters, Name, Args->Values[Index].Number);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            break;
        case CuaBooleanParameter:
            Hr = Data_JsonObjectSetBoolean(Factory, Parameters, Name, Args->Values[Index].Boolean);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            break;
        case CuaKeysParameter:
            Hr = Data_JsonCreateArray(&Keys);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            for (ULONG Key = 0; Key < Args->KeyCount; ++Key)
            {
                Hr = Factory->lpVtbl->CreateStringValue(Factory, Args->Keys[Key], &Item);
                if (FAILED(Hr))
                {
                    goto Exit;
                }
                Hr = Keys->lpVtbl->Append(Keys, Item);
                if (FAILED(Hr))
                {
                    goto Exit;
                }
                Item->lpVtbl->Release(Item);
                Item = NULL;
            }
            Hr = Keys->lpVtbl->QueryInterface(Keys, &IID_IJsonValue, (PVOID*)&Value);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Hr = Data_JsonObjectSetValue(Parameters, Name, Value);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Value->lpVtbl->Release(Value);
            Value = NULL;
            break;
        }
    }
    Hr = Parameters->lpVtbl->QueryInterface(Parameters, &IID_IJsonValue, (PVOID*)&Value);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetValue(Root, L"params", Value);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Root->lpVtbl->QueryInterface(Root, &IID_IJsonValue, (PVOID*)Wire);
Exit:
    if (Item != NULL)
    {
        Item->lpVtbl->Release(Item);
    }
    if (Value != NULL)
    {
        Value->lpVtbl->Release(Value);
    }
    if (Keys != NULL)
    {
        Keys->lpVtbl->Release(Keys);
    }
    if (Parameters != NULL)
    {
        Parameters->lpVtbl->Release(Parameters);
    }
    if (Root != NULL)
    {
        Root->lpVtbl->Release(Root);
    }
    if (Factory != NULL)
    {
        Factory->lpVtbl->Release(Factory);
    }
    return Hr;
}

HRESULT
CuaBuildReply(
    _In_opt_ IJsonValue* Result,
    _In_opt_ const CUA_ERROR* Error,
    _Outptr_ IJsonValue** Reply)
{
    IJsonObject* Object;
    CHAR Message[sizeof(((CUA_ERROR*)0)->Message) / sizeof(WCHAR) * 3 + 1];
    HRESULT Hr;

    *Reply = NULL;
    if (Error != NULL)
    {
        Str_W2U(Message, Error->Message);
        Object = BuildErrorOutput(Error->Status, "%s", Message);
    } else
    {
        if (Result != NULL)
        {
            Result->lpVtbl->AddRef(Result);
        }
        Object = BuildSuccessOutput((IUnknown*)Result);
    }
    if (Object == NULL)
    {
        return E_OUTOFMEMORY;
    }
    if (Error != NULL && Result != NULL)
    {
        Hr = Data_JsonObjectSetValue(Object, L"result", Result);
        if (FAILED(Hr))
        {
            goto Exit;
        }
    }
    Hr = Object->lpVtbl->QueryInterface(Object, &IID_IJsonValue, (PVOID*)Reply);
Exit:
    Object->lpVtbl->Release(Object);
    return Hr;
}
