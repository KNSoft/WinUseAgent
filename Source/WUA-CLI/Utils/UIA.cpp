#include "pch.h"

_Success_(return != NULL)
_Ret_maybenull_
IUIAutomation*
Util_UIA_CreateInstance(VOID)
{
    IUIAutomation2* UIA2;
    IUIAutomation* UIA;

    if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&UIA2))))
    {
        UIA2->put_ConnectionTimeout(2000);
        UIA2->put_TransactionTimeout(10000);
        return UIA2;
    }

    if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&UIA))))
    {
        return UIA;
    }

    return NULL;
}

static
_Success_(return != FALSE)
LOGICAL
Util_UIA_GetTextByProperty(
    _In_ IUIAutomationElement * Element,
    _In_ PROPERTYID PropertyId,
    _Out_writes_(BufferCch) PSTR Buffer,
    _In_ ULONG BufferCch,
    _Out_ PULONG BytesWritten)
{
    VARIANT Variant;
    ULONG Length, Written;
    LOGICAL Ret;

    if (BufferCch == 0)
    {
        return FALSE;
    }

    Ret = FALSE;
    VariantInit(&Variant);
    if (SUCCEEDED(Element->GetCurrentPropertyValue(PropertyId, &Variant)) &&
        Variant.vt == VT_BSTR &&
        Variant.bstrVal != NULL)
    {
        Written = 0;
        Length = SysStringLen(Variant.bstrVal);
        if (Length > 0)
        {
            RtlUnicodeToUTF8N(Buffer, BufferCch - 1, &Written, Variant.bstrVal, Length * sizeof(WCHAR));
            Buffer[Written] = ANSI_NULL;
        }
        *BytesWritten = Written;
        Ret = TRUE;
    }
    VariantClear(&Variant);
    return Ret;
}

static
ULONG
Util_UIA_GetTextFromTextRange(
    _In_ IUIAutomationTextRange * TextRange,
    _Out_writes_(BufferCch) PSTR Buffer,
    _In_ ULONG BufferCch)
{
    BSTR Text;
    ULONG BytesWritten;

    BytesWritten = 0;
    if (SUCCEEDED(TextRange->GetText(BufferCch, &Text)) && Text != NULL)
    {
        RtlUnicodeToUTF8N(Buffer, BufferCch - 1, &BytesWritten, Text, SysStringLen(Text) * sizeof(WCHAR));
        SysFreeString(Text);
        Buffer[BytesWritten] = ANSI_NULL;
    }
    return BytesWritten;
}

static
ULONG
Util_UIA_GetTextFromDocumentPattern(
    _In_ IUIAutomationElement * Element,
    _In_ PATTERNID PatternId,
    _Out_writes_(BufferCch) PSTR Buffer,
    _In_ ULONG BufferCch)
{
    IUIAutomationTextPattern* TextPattern;
    IUIAutomationTextRange* DocumentRange;
    ULONG BytesWritten;

    BytesWritten = 0;
    if (SUCCEEDED(Element->GetCurrentPatternAs(PatternId, IID_PPV_ARGS(&TextPattern))))
    {
        if (SUCCEEDED(TextPattern->get_DocumentRange(&DocumentRange)))
        {
            BytesWritten = Util_UIA_GetTextFromTextRange(DocumentRange, Buffer, BufferCch);
            DocumentRange->Release();
        }
        TextPattern->Release();
    }
    return BytesWritten;
}

ULONG
Util_UIA_GetText(
    _In_ IUIAutomationElement * Element,
    _Out_writes_(BufferCch) PSTR Buffer,
    _In_ ULONG BufferCch)
{
    IUIAutomationTextChildPattern* TextChildPattern;
    IUIAutomationTextRange* TextRange;
    ULONG BytesWritten;

    if (Util_UIA_GetTextByProperty(Element, UIA_ValueValuePropertyId, Buffer, BufferCch, &BytesWritten))
    {
        return BytesWritten;
    }
    if (Util_UIA_GetTextByProperty(Element, UIA_LegacyIAccessibleValuePropertyId, Buffer, BufferCch, &BytesWritten))
    {
        return BytesWritten;
    }

    /* Try UIA_TextPatternId */
    BytesWritten = Util_UIA_GetTextFromDocumentPattern(Element, UIA_TextPatternId, Buffer, BufferCch);
    if (BytesWritten > 0)
    {
        return BytesWritten;
    }

    /* Try UIA_TextPattern2Id */
    BytesWritten = Util_UIA_GetTextFromDocumentPattern(Element, UIA_TextPattern2Id, Buffer, BufferCch);
    if (BytesWritten > 0)
    {
        return BytesWritten;
    }

    /* Try UIA_TextChildPatternId */
    if (SUCCEEDED(Element->GetCurrentPatternAs(UIA_TextChildPatternId, IID_PPV_ARGS(&TextChildPattern))))
    {
        if (SUCCEEDED(TextChildPattern->get_TextRange(&TextRange)))
        {
            BytesWritten = Util_UIA_GetTextFromTextRange(TextRange, Buffer, BufferCch);
            TextRange->Release();
        } else
        {
            BytesWritten = 0;
        }
        TextChildPattern->Release();
        if (BytesWritten > 0)
        {
            return BytesWritten;
        }
    }

    return 0;
}

_Success_(return != NULL)
_Ret_maybenull_
cJSON*
Utils_UIA_GetInfoJson(
    _In_ IUIAutomationElement * Element)
{
    cJSON *j, *j_Bounds;
    BSTR bstr;
    RECT Rect;
    POINT pt;
    BOOL b;

    j = cJSON_CreateObject();

    if (SUCCEEDED(Element->get_CurrentName(&bstr)))
    {
        Util_Json_AddBstr(j, "name", bstr);
        SysFreeString(bstr);
    } else
    {
        cJSON_AddNullToObject(j, "name");
    }

    if (SUCCEEDED(Element->get_CurrentLocalizedControlType(&bstr)))
    {
        Util_Json_AddBstr(j, "role", bstr);
        SysFreeString(bstr);
    }

    CHAR sz[2048];
    if (Util_UIA_GetText(Element, sz, ARRAYSIZE(sz)) > 0)
    {
        cJSON_AddStringToObject(j, "text", sz);
    }

    if (SUCCEEDED(Element->get_CurrentBoundingRectangle(&Rect)))
    {
        j_Bounds = cJSON_CreateObject();
        cJSON_AddNumberToObject(j_Bounds, "left", Rect.left);
        cJSON_AddNumberToObject(j_Bounds, "top", Rect.top);
        cJSON_AddNumberToObject(j_Bounds, "right", Rect.right);
        cJSON_AddNumberToObject(j_Bounds, "bottom", Rect.bottom);
        cJSON_AddItemToObject(j, "bounds", j_Bounds);
    }

    if (SUCCEEDED(Element->GetClickablePoint(&pt, &b)) && b)
    {
        cJSON* j_ClickablePoint = cJSON_CreateObject();
        cJSON_AddNumberToObject(j_ClickablePoint, "x", pt.x);
        cJSON_AddNumberToObject(j_ClickablePoint, "y", pt.y);
        cJSON_AddItemToObject(j, "clickable_point", j_ClickablePoint);
    }

    if (SUCCEEDED(Element->get_CurrentIsEnabled(&b)))
    {
        cJSON_AddBoolToObject(j, "enabled", b);
    }
    if (SUCCEEDED(Element->get_CurrentIsOffscreen(&b)))
    {
        cJSON_AddBoolToObject(j, "offscreen", b);
    }
    if (SUCCEEDED(Element->get_CurrentHasKeyboardFocus(&b)))
    {
        cJSON_AddBoolToObject(j, "focused", b);
    }

    return j;
}
