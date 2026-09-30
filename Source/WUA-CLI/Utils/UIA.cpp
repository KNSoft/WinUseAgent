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

_Ret_maybenull_
IJsonObject*
Util_UIA_GetInfoJson(
    _In_ IUIAutomationElement * Element)
{
    IJsonObject* j;
    UIA_HWND uiaHwnd;
    BSTR bstr;
    RECT Rect;
    POINT pt;
    BOOL b;

    j = Util_Json_CreateObject();

    if (SUCCEEDED(Element->get_CurrentNativeWindowHandle(&uiaHwnd)))
    {
        Util_Json_AddWindowHandle(j, L"window_handle", reinterpret_cast<HWND>(uiaHwnd));
    }

    if (SUCCEEDED(Element->get_CurrentName(&bstr)))
    {
        Util_Json_AddBstr(j, L"name", bstr);
        SysFreeString(bstr);
    } else
    {
        Util_Json_AddNullToObject(j, L"name");
    }

    if (SUCCEEDED(Element->get_CurrentLocalizedControlType(&bstr)))
    {
        Util_Json_AddBstr(j, L"role", bstr);
        SysFreeString(bstr);
    }

    CHAR sz[2048];
    if (Util_UIA_GetText(Element, sz, ARRAYSIZE(sz)) > 0)
    {
        Util_Json_AddStringToObject(j, L"text", sz);
    }

    if (SUCCEEDED(Element->get_CurrentBoundingRectangle(&Rect)))
    {
        Util_Json_AddNumberToObject(j, L"left", Rect.left);
        Util_Json_AddNumberToObject(j, L"top", Rect.top);
        Util_Json_AddNumberToObject(j, L"right", Rect.right);
        Util_Json_AddNumberToObject(j, L"bottom", Rect.bottom);
    }

    if (SUCCEEDED(Element->GetClickablePoint(&pt, &b)) && b)
    {
        IJsonObject* j_ClickablePoint = Util_Json_CreateObject();
        Util_Json_AddNumberToObject(j_ClickablePoint, L"x", pt.x);
        Util_Json_AddNumberToObject(j_ClickablePoint, L"y", pt.y);
        Util_Json_AddItemToObject(j, L"clickable_point", j_ClickablePoint);
        if (j_ClickablePoint != NULL)
        {
            j_ClickablePoint->Release();
        }
    }

    if (SUCCEEDED(Element->get_CurrentIsEnabled(&b)))
    {
        Util_Json_AddBoolToObject(j, L"enabled", b);
    }
    if (SUCCEEDED(Element->get_CurrentIsOffscreen(&b)))
    {
        Util_Json_AddBoolToObject(j, L"offscreen", b);
    }
    if (SUCCEEDED(Element->get_CurrentHasKeyboardFocus(&b)))
    {
        Util_Json_AddBoolToObject(j, L"focused", b);
    }

    return j;
}

static
VOID
AppendChildrenInfoJson(
    _In_opt_ IJsonObject* j,
    _In_ PCWSTR Key,
    _In_ IUIAutomationTreeWalker * Walker,
    _In_ IUIAutomationElement * Element,
    _In_ ULONG Depth)
{
    IUIAutomationElement *Child = NULL, *NextChild = NULL;
    IJsonVector* jChildren;
    IJsonObject* jChild;
    ULONG ChildCount;

    if (Depth >= WUA_UIA_MAX_DEPTH)
    {
        return;
    }
    if (FAILED(Walker->GetFirstChildElement(Element, &Child)) || Child == NULL)
    {
        return;
    }

    jChildren = Util_Json_CreateArray();
    ChildCount = 0;
    do
    {
        jChild = Util_UIA_GetInfoJson(Child);
        AppendChildrenInfoJson(jChild, Key, Walker, Child, Depth + 1);
        Util_Json_AddItemToArray(jChildren, jChild);
        if (jChild != NULL)
        {
            jChild->Release();
        }
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

    if (Util_Json_GetArraySize(jChildren) > 0)
    {
        Util_Json_AddItemToObject(j, Key, jChildren);
    }
    if (jChildren != NULL)
    {
        jChildren->Release();
    }
}

_Ret_maybenull_
IJsonObject*
Util_UIA_GetWindowElementJson(
    _In_ HWND hWnd)
{
    HRESULT Hr;
    IUIAutomation* UIA;
    IUIAutomationElement* Element;
    IUIAutomationTreeWalker* Walker;
    IJsonObject* j = NULL;

    Hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if ((SUCCEEDED(Hr) || Hr == RPC_E_CHANGED_MODE) &&
        (UIA = Util_UIA_CreateInstance()) != NULL)
    {
        if (SUCCEEDED(UIA->ElementFromHandle(hWnd, &Element)))
        {
            if (SUCCEEDED(UIA->get_ControlViewWalker(&Walker)))
            {
                j = Util_UIA_GetInfoJson(Element);
                AppendChildrenInfoJson(j, L"children", Walker, Element, 0);
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
    return j;
}
