#include "pch.h"

_Ret_notnull_
cJSON*
Util_Json_AddUnicodeString(
    _In_ cJSON* j,
    _In_ PCSTR Key,
    _In_reads_opt_(Length) PCWSTR String,
    _In_opt_ ULONG Length)
{
    UNICODE_STRING UnicodeString;
    UTF8_STRING UTF8String;
    cJSON* jRet;

    if (String == NULL)
    {
        return cJSON_AddNullToObject(j, Key);
    }
    if (Length == 0)
    {
        Length = (ULONG)wcslen(String);
    }

    UnicodeString.Buffer = const_cast<PWSTR>(String);
    UnicodeString.Length = (USHORT)(Length + 1) * sizeof(WCHAR);
    UnicodeString.MaximumLength = UnicodeString.Length;
    if (NT_SUCCESS(RtlUnicodeStringToUTF8String(&UTF8String, &UnicodeString, TRUE)))
    {
        jRet = cJSON_AddStringToObject(j, Key, UTF8String.Buffer);
        RtlFreeUTF8String(&UTF8String);
    } else
    {
        jRet = cJSON_AddNullToObject(j, Key);
    }
    return jRet;
}

_Ret_notnull_
cJSON*
Util_Json_AddBstr(
    _In_ cJSON* j,
    _In_ PCSTR Key,
    _In_opt_ BSTR Value)
{
    if (Value != NULL)
    {
        return Util_Json_AddUnicodeString(j, Key, Value, SysStringLen(Value));
    } else
    {
        return cJSON_AddNullToObject(j, Key);
    }
}

_Ret_notnull_
cJSON*
Util_Json_AddVariant(
    _In_ cJSON* j,
    _In_ PCSTR Key,
    _In_opt_ LPVARIANT Value)
{
    if (Value == NULL)
    {
        return cJSON_AddNullToObject(j, Key);
    }

    if (Value->vt == VT_BSTR)
    {
        return Util_Json_AddBstr(j, Key, Value->bstrVal);
    } else if (Value->vt == VT_BOOL)
    {
        return cJSON_AddBoolToObject(j, Key, !!Value->boolVal);
    } else if (Value->vt == VT_I1)
    {
        return cJSON_AddNumberToObject(j, Key, Value->bVal);
    } else if (Value->vt == VT_I2)
    {
        return cJSON_AddNumberToObject(j, Key, Value->iVal);
    } else if (Value->vt == VT_I4)
    {
        return cJSON_AddNumberToObject(j, Key, Value->lVal);
    } else if (Value->vt == VT_INT)
    {
        return cJSON_AddNumberToObject(j, Key, Value->intVal);
    } else if (Value->vt == VT_R4)
    {
        return cJSON_AddNumberToObject(j, Key, Value->fltVal);
    } else if (Value->vt == VT_R8)
    {
        return cJSON_AddNumberToObject(j, Key, Value->dblVal);
    } else if (Value->vt == VT_UI1)
    {
        return cJSON_AddNumberToObject(j, Key, Value->bVal);
    } else if (Value->vt == VT_UI2)
    {
        return cJSON_AddNumberToObject(j, Key, Value->uiVal);
    } else if (Value->vt == VT_UI4)
    {
        return cJSON_AddNumberToObject(j, Key, Value->ulVal);
    } else if (Value->vt == VT_UINT)
    {
        return cJSON_AddNumberToObject(j, Key, Value->uintVal);
    }

    return cJSON_AddNullToObject(j, Key);
}
