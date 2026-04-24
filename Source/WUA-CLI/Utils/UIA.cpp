#include "pch.h"

#define WUA_UIA_CONNECTION_TIMEOUT 2000
#define WUA_UIA_TRANSACTION_TIMEOUT 10000
#define WUA_UIA_MAX_DEPTH 16
#define WUA_UIA_MAX_CHILDREN 256

_Success_(return != NULL)
_Ret_maybenull_
IUIAutomation*
Util_UIA_CreateInstance(VOID)
{
    IUIAutomation2* UIA2;
    IUIAutomation* UIA;

    if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&UIA2))))
    {
        UIA2->put_ConnectionTimeout(WUA_UIA_CONNECTION_TIMEOUT);
        UIA2->put_TransactionTimeout(WUA_UIA_TRANSACTION_TIMEOUT);
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
        }
        Buffer[Written] = ANSI_NULL;
        *BytesWritten = Written;
        Ret = TRUE;
    }
    VariantClear(&Variant);
    return Ret;
}

static
_Success_(return > 0)
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
_Success_(return > 0)
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

_Ret_notnull_
cJSON*
Util_UIA_GetInfoJson(
    _In_ IUIAutomationElement * Element)
{
    cJSON *j;
    UIA_HWND uiaHwnd;
    BSTR bstr;
    RECT Rect;
    POINT pt;
    BOOL b;

    j = cJSON_CreateObject();

    if (SUCCEEDED(Element->get_CurrentNativeWindowHandle(&uiaHwnd)))
    {
        Util_Json_AddWindowHandle(j, "window_handle", reinterpret_cast<HWND>(uiaHwnd));
    }

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
        cJSON_AddNumberToObject(j, "left", Rect.left);
        cJSON_AddNumberToObject(j, "top", Rect.top);
        cJSON_AddNumberToObject(j, "right", Rect.right);
        cJSON_AddNumberToObject(j, "bottom", Rect.bottom);
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

static
_Ret_maybenull_
cJSON*
AppendChildrenInfoJson(
    _In_ cJSON * j,
    _In_ PCSTR Key,
    _In_ IUIAutomationTreeWalker * Walker,
    _In_ IUIAutomationElement * Element,
    _In_ ULONG Depth)
{
    IUIAutomationElement *Child, *NextChild;
    cJSON *jChildren, *jChild;
    ULONG ChildCount;

    if (Depth >= WUA_UIA_MAX_DEPTH)
    {
        return NULL;
    }
    if (FAILED(Walker->GetFirstChildElement(Element, &Child)) || Child == NULL)
    {
        return NULL;
    }

    jChildren = cJSON_CreateArray();
    ChildCount = 0;
    do
    {
        jChild = Util_UIA_GetInfoJson(Child);
        AppendChildrenInfoJson(jChild, Key, Walker, Child, Depth + 1);
        cJSON_AddItemToArray(jChildren, jChild);
        ChildCount++;
        if (ChildCount >= WUA_UIA_MAX_CHILDREN)
        {
            Child->Release();
            break;
        }
        NextChild = NULL;
        Walker->GetNextSiblingElement(Child, &NextChild);
        Child->Release();
        Child = NextChild;
    } while (Child != NULL);

    if (cJSON_GetArraySize(jChildren) > 0)
    {
        cJSON_AddItemToObject(j, Key, jChildren);
    } else
    {
        cJSON_Delete(jChildren);
        jChildren = NULL;
    }

    return jChildren;
}

_Ret_notnull_
cJSON*
Util_UIA_GetWindowElementJson(
    _In_ HWND hWnd)
{
    HRESULT Hr;
    IUIAutomation* UIA;
    IUIAutomationElement* Element;
    IUIAutomationTreeWalker* Walker;
    cJSON* j;

    j = NULL;
    Hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if ((SUCCEEDED(Hr) || Hr == RPC_E_CHANGED_MODE) &&
        (UIA = Util_UIA_CreateInstance()) != NULL)
    {
        if (SUCCEEDED(UIA->ElementFromHandle(hWnd, &Element)))
        {
            if (SUCCEEDED(UIA->get_ControlViewWalker(&Walker)))
            {
                j = Util_UIA_GetInfoJson(Element);
                AppendChildrenInfoJson(j, "children", Walker, Element, 0);
                Walker->Release();
            }
            Element->Release();
        }
        UIA->Release();
    }
    if (SUCCEEDED(Hr))
    {
        CoUninitialize();
    }
    return j != NULL ? j : cJSON_CreateNull();
}
