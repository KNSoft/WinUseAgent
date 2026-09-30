#include "pch.h"

static IJsonValueStatics* JsonFactory = NULL;
static IJsonValueStatics2* JsonFactory2 = NULL;
static HRESULT JsonStatus = CO_E_NOTINITIALIZED;

static
HRESULT
Json_RecordResult(
    _In_ HRESULT Hr)
{
    if (FAILED(Hr) && SUCCEEDED(JsonStatus))
    {
        JsonStatus = Hr;
    }
    return Hr;
}

HRESULT
Util_Json_Initialize(VOID)
{
    Util_Json_Shutdown();
    JsonStatus = S_OK;
    if (SUCCEEDED(Json_RecordResult(Data_JsonGetValueFactory(&JsonFactory))))
    {
        Json_RecordResult(JsonFactory->QueryInterface(IID_IJsonValueStatics2, (PVOID*)&JsonFactory2));
    }
    return JsonStatus;
}

VOID
Util_Json_Shutdown(VOID)
{
    if (JsonFactory2 != NULL)
    {
        JsonFactory2->Release();
        JsonFactory2 = NULL;
    }
    if (JsonFactory != NULL)
    {
        JsonFactory->Release();
        JsonFactory = NULL;
    }
    JsonStatus = CO_E_NOTINITIALIZED;
}

HRESULT
Util_Json_Write(
    _In_opt_ IUnknown* Value,
    _In_ HANDLE File)
{
    IJsonValue* JsonValue;
    PSTR Text;
    ULONG Length, Written, Offset;
    NTSTATUS Status;
    HRESULT Hr;

    if (FAILED(JsonStatus))
    {
        return JsonStatus;
    }
    Hr = Value->QueryInterface(IID_IJsonValue, (PVOID*)&JsonValue);
    if (FAILED(Hr))
    {
        return Json_RecordResult(Hr);
    }
    Hr = Data_JsonStringifyUtf8(JsonValue, &Text, &Length);
    JsonValue->Release();
    if (SUCCEEDED(Hr))
    {
        Offset = 0;
        while (Offset < Length)
        {
            Status = IO_WriteFile(File, NULL, Text + Offset, Length - Offset, &Written);
            if (!NT_SUCCESS(Status))
            {
                Hr = HRESULT_FROM_NT(Status);
                break;
            }
            if (Written == 0 || Written > Length - Offset)
            {
                Hr = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
                break;
            }
            Offset += Written;
        }
        Mem_Free(Text);
    }
    return Json_RecordResult(Hr);
}

_Ret_maybenull_
IJsonObject*
Util_Json_CreateObject(VOID)
{
    IJsonObject* Object = NULL;

    if (SUCCEEDED(JsonStatus))
    {
        Json_RecordResult(Data_JsonCreateObject(&Object));
    }
    return Object;
}

_Ret_maybenull_
IJsonVector*
Util_Json_CreateArray(VOID)
{
    IJsonVector* Array = NULL;

    if (SUCCEEDED(JsonStatus))
    {
        Json_RecordResult(Data_JsonCreateArray(&Array));
    }
    return Array;
}

VOID
Util_Json_AddItemToObject(
    _In_opt_ IJsonObject* Object,
    _In_ PCWSTR Key,
    _In_opt_ IUnknown* Value)
{
    IJsonValue* JsonValue;

    if (SUCCEEDED(JsonStatus))
    {
        if (SUCCEEDED(Json_RecordResult(Value->QueryInterface(IID_IJsonValue, (PVOID*)&JsonValue))))
        {
            Json_RecordResult(Data_JsonObjectSetValue(Object, Key, JsonValue));
            JsonValue->Release();
        }
    }
}

VOID
Util_Json_AddItemToArray(
    _In_opt_ IJsonVector* Array,
    _In_opt_ IUnknown* Value)
{
    IJsonValue* JsonValue;

    if (SUCCEEDED(JsonStatus))
    {
        if (SUCCEEDED(Json_RecordResult(Value->QueryInterface(IID_IJsonValue, (PVOID*)&JsonValue))))
        {
            Json_RecordResult(Array->Append(JsonValue));
            JsonValue->Release();
        }
    }
}

ULONG
Util_Json_GetArraySize(
    _In_opt_ IJsonVector* Array)
{
    UINT32 Size = 0;

    if (SUCCEEDED(JsonStatus))
    {
        Json_RecordResult(Array->get_Size(&Size));
    }
    return Size;
}

_Ret_maybenull_
IJsonObject*
Util_Json_AddObjectToObject(
    _In_opt_ IJsonObject* Object,
    _In_ PCWSTR Key)
{
    IJsonObject* Value = Util_Json_CreateObject();

    Util_Json_AddItemToObject(Object, Key, Value);
    if (FAILED(JsonStatus) && Value != NULL)
    {
        Value->Release();
        Value = NULL;
    }
    return Value;
}

VOID
Util_Json_AddNullToObject(
    _In_opt_ IJsonObject* Object,
    _In_ PCWSTR Key)
{
    if (SUCCEEDED(JsonStatus))
    {
        Json_RecordResult(Data_JsonObjectSetNull(JsonFactory2, Object, Key));
    }
}

VOID
Util_Json_AddStringToObject(
    _In_opt_ IJsonObject* Object,
    _In_ PCWSTR Key,
    _In_ PCSTR Value)
{
    SIZE_T Length;

    if (FAILED(JsonStatus))
    {
        return;
    }
    Length = strlen(Value);
    Json_RecordResult(Length <= MAXULONG ?
                      Data_JsonObjectSetStringUtf8(JsonFactory, Object, Key, Value, (ULONG)Length) : E_INVALIDARG);
}

VOID
Util_Json_AddBoolToObject(
    _In_opt_ IJsonObject* Object,
    _In_ PCWSTR Key,
    _In_ LOGICAL Value)
{
    if (SUCCEEDED(JsonStatus))
    {
        Json_RecordResult(Data_JsonObjectSetBoolean(JsonFactory, Object, Key, Value));
    }
}

VOID
Util_Json_AddNumberToObject(
    _In_opt_ IJsonObject* Object,
    _In_ PCWSTR Key,
    _In_ DOUBLE Value)
{
    if (SUCCEEDED(JsonStatus))
    {
        Json_RecordResult(Data_JsonObjectSetNumber(JsonFactory, Object, Key, Value));
    }
}

VOID
Util_Json_AddUnicodeString(
    _In_opt_ IJsonObject* j,
    _In_ PCWSTR Key,
    _When_(Length == 0, _In_opt_z_) _When_(Length != 0, _In_reads_(Length)) PCWSTR String,
    _In_opt_ ULONG Length)
{
    if (FAILED(JsonStatus))
    {
        return;
    }
    if (String == NULL)
    {
        Util_Json_AddNullToObject(j, Key);
        return;
    }
    if (Length == 0)
    {
        SIZE_T StringLength = wcslen(String);
        if (StringLength > MAXULONG)
        {
            Json_RecordResult(E_INVALIDARG);
            return;
        }
        Length = (ULONG)StringLength;
    }
    Json_RecordResult(Data_JsonObjectSetString(JsonFactory, j, Key, String, Length));
}

VOID
Util_Json_AddBstr(
    _In_opt_ IJsonObject* j,
    _In_ PCWSTR Key,
    _In_opt_ BSTR Value)
{
    if (SUCCEEDED(JsonStatus))
    {
        Json_RecordResult(Value != NULL ?
                          Data_JsonObjectSetString(JsonFactory, j, Key, Value, SysStringLen(Value)) :
                          Data_JsonObjectSetNull(JsonFactory2, j, Key));
    }
}
