#include "pch.h"
#include "Uia.h"
#include <uiautomation.h>

typedef struct _UIA_REQUEST
{
    LONG References;
    HANDLE Completed;
    HWND Window;
    DWORD Process, Thread;
    UIA_OPTIONS Options;
    ULONGLONG Start;
    ULONG Operation, RuntimeIdCount;
    LONG RuntimeId[WUA_UIA_MAX_RUNTIME_ID];
    PWSTR Value;
    UIA_OBSERVATION Observation;
} UIA_REQUEST;

struct _UIA_CONTEXT
{
    LONG References;
    RTL_SRWLOCK Lock;
    HANDLE Wake;
    UIA_REQUEST* Pending;
    LOGICAL Stopping;
};

static
ULONG
Util_UIA_GetText(
    _In_ IUIAutomationElement* Element,
    _Out_writes_(BufferCch) PSTR Buffer,
    _In_ ULONG BufferCch);

static
VOID
ReleaseRequest(
    _In_ UIA_REQUEST* Request)
{
    if (InterlockedDecrement(&Request->References) == 0)
    {
        NtClose(Request->Completed);
        Mem_Free(Request->Observation.Elements);
        Mem_Free(Request->Value);
        Mem_Free(Request);
    }
}

static
VOID
ReleaseContext(
    _In_ UIA_CONTEXT* Context)
{
    if (InterlockedDecrement(&Context->References) == 0)
    {
        NtClose(Context->Wake);
        Mem_Free(Context);
    }
}

static
HRESULT
RunRequest(
    _Inout_ UIA_CONTEXT* Context,
    _Inout_ UIA_REQUEST* Request)
{
    NTSTATUS Status;
    HRESULT Hr = S_OK;
    ULONGLONG Elapsed;

    Request->Start = Time_StopWatchStart();
    RtlAcquireSRWLockExclusive(&Context->Lock);
    if (Context->Stopping != FALSE)
    {
        Hr = HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
    } else if (Context->Pending != NULL)
    {
        Hr = HRESULT_FROM_WIN32(ERROR_BUSY);
    } else
    {
        InterlockedIncrement(&Request->References);
        Context->Pending = Request;
        Status = NtSetEvent(Context->Wake, NULL);
        if (!NT_SUCCESS(Status))
        {
            Context->Pending = NULL;
            ReleaseRequest(Request);
            Hr = Err_NtStatusToHr(Status);
        }
    }
    RtlReleaseSRWLockExclusive(&Context->Lock);
    if (FAILED(Hr))
    {
        return Hr;
    }
    Elapsed = Time_StopWatchStop(Request->Start, 1000);
    if (Elapsed >= Request->Options.TimeoutMs)
    {
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    Status = PS_WaitForObject(Request->Completed, Request->Options.TimeoutMs - (ULONG)Elapsed);
    return Status == STATUS_TIMEOUT ? HRESULT_FROM_WIN32(ERROR_TIMEOUT) : Err_NtStatusToHr(Status);
}

static
ULONG
RuntimeId(
    _In_ IUIAutomationElement* Element,
    _Out_writes_to_(WUA_UIA_MAX_RUNTIME_ID, return) LONG* Id)
{
    SAFEARRAY* Array = NULL;
    ULONG Count = 0;
    LONG Lower = 0, Upper = -1;
    if (SUCCEEDED(Element->lpVtbl->GetRuntimeId(Element, &Array)) && Array != NULL)
    {
        if (SafeArrayGetDim(Array) == 1 && SafeArrayGetElemsize(Array) == sizeof(LONG) &&
            SUCCEEDED(SafeArrayGetLBound(Array, 1, &Lower)) && SUCCEEDED(SafeArrayGetUBound(Array, 1, &Upper)) &&
            Upper >= Lower && (LONGLONG)Upper - Lower < WUA_UIA_MAX_RUNTIME_ID)
        {
            for (LONGLONG Position = Lower; Position <= Upper; ++Position)
            {
                LONG Index = (LONG)Position;
                if (FAILED(SafeArrayGetElement(Array, &Index, &Id[Count])))
                {
                    Count = 0;
                    break;
                }
                ++Count;
            }
        }
        SafeArrayDestroy(Array);
    }
    return Count;
}

static
VOID
CachedString(
    _In_ IUIAutomationElement* Element,
    _In_ PROPERTYID Property,
    _Out_writes_z_(Capacity) PWSTR Text,
    _In_ ULONG Capacity)
{
    VARIANT Value;
    Text[0] = UNICODE_NULL;
    VariantInit(&Value);
    if (SUCCEEDED(Element->lpVtbl->GetCachedPropertyValue(Element, Property, &Value)) &&
        Value.vt == VT_BSTR && Value.bstrVal != NULL)
    {
        ULONG Size = SysStringLen(Value.bstrVal), Length = min(Size, Capacity - 1);
        if (Length != 0 && Length < Size && IS_HIGH_SURROGATE(Value.bstrVal[Length - 1]) != FALSE &&
            IS_LOW_SURROGATE(Value.bstrVal[Length]) != FALSE)
        {
            --Length;
        }
        RtlCopyMemory(Text, Value.bstrVal, Length * sizeof(WCHAR));
        Text[Length] = UNICODE_NULL;
    }
    VariantClear(&Value);
}

static
LOGICAL
CachedBool(
    _In_ IUIAutomationElement* Element,
    _In_ PROPERTYID Property,
    _In_ LOGICAL Default)
{
    VARIANT Value;
    LOGICAL Result = Default;
    VariantInit(&Value);
    if (SUCCEEDED(Element->lpVtbl->GetCachedPropertyValue(Element, Property, &Value)) && Value.vt == VT_BOOL)
    {
        Result = Value.boolVal != VARIANT_FALSE;
    }
    VariantClear(&Value);
    return Result;
}

static
HRESULT
PrepareAutomation(
    _Inout_ IUIAutomation** Automation,
    _In_ DWORD TimeoutMs)
{
    IUIAutomation2* Version2 = NULL;
    HRESULT Hr;

    if (*Automation == NULL)
    {
        Hr = CoCreateInstance(&CLSID_CUIAutomation8, NULL, CLSCTX_INPROC_SERVER,
            &IID_IUIAutomation, (PVOID*)Automation);
        if (FAILED(Hr))
        {
            Hr = CoCreateInstance(&CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER,
                &IID_IUIAutomation, (PVOID*)Automation);
        }
        if (FAILED(Hr))
        {
            return Hr;
        }
    }
    Hr = (*Automation)->lpVtbl->QueryInterface(*Automation, &IID_IUIAutomation2, (PVOID*)&Version2);
    if (Hr == E_NOINTERFACE)
    {
        return S_OK;
    }
    if (FAILED(Hr))
    {
        return Hr;
    }
    Hr = Version2->lpVtbl->put_ConnectionTimeout(Version2, min(TimeoutMs, WUA_UIA_CONNECTION_TIMEOUT_MS));
    if (SUCCEEDED(Hr))
    {
        Hr = Version2->lpVtbl->put_TransactionTimeout(Version2, min(TimeoutMs, WUA_UIA_TRANSACTION_TIMEOUT_MS));
    }
    Version2->lpVtbl->Release(Version2);
    return Hr;
}

typedef struct _UIA_TRAVERSAL
{
    IUIAutomationTreeWalker* Walker;
    IUIAutomationCacheRequest* Cache;
    const UIA_OPTIONS* Options;
    UIA_OBSERVATION* Observation;
    ULONG Capacity;
    ULONGLONG Start;
} UIA_TRAVERSAL;

static
VOID
Visit(
    _Inout_ UIA_TRAVERSAL* Traversal,
    _In_ IUIAutomationElement* Element,
    _In_ LONG Parent,
    _In_ ULONG Depth)
{
    UIA_OBSERVATION* Observation = Traversal->Observation;
    UIA_ELEMENT* Item;
    IUIAutomationElement* Child = NULL;
    IUIAutomationElement* Next = NULL;
    HRESULT Hr;
    LONG Index;
    if (Observation->Count >= Traversal->Capacity ||
        Time_StopWatchStop(Traversal->Start, 1000) >= Traversal->Options->TimeoutMs)
    {
        Observation->Truncated = TRUE;
        return;
    }
    Index = Observation->Count++;
    Item = &Observation->Elements[Index];
    RtlZeroMemory(Item, sizeof(*Item));
    Item->ParentIndex = Parent;
    Item->RuntimeIdCount = RuntimeId(Element, Item->RuntimeId);
    Item->Password = CachedBool(Element, UIA_IsPasswordPropertyId, TRUE);
    CachedString(Element, UIA_NamePropertyId, Item->Name, ARRAYSIZE(Item->Name));
    CachedString(Element, UIA_LocalizedControlTypePropertyId, Item->Role, ARRAYSIZE(Item->Role));
    CachedString(Element, UIA_AutomationIdPropertyId, Item->AutomationId, ARRAYSIZE(Item->AutomationId));
    Item->Enabled = CachedBool(Element, UIA_IsEnabledPropertyId, FALSE);
    Item->Offscreen = CachedBool(Element, UIA_IsOffscreenPropertyId, TRUE);
    Item->Focused = CachedBool(Element, UIA_HasKeyboardFocusPropertyId, FALSE);
    Item->BoundsValid = SUCCEEDED(Element->lpVtbl->get_CachedBoundingRectangle(Element, &Item->Bounds));
    if (CachedBool(Element, UIA_IsInvokePatternAvailablePropertyId, FALSE) != FALSE)
    {
        Item->Patterns |= UiaPatternInvoke;
    }
    if (CachedBool(Element, UIA_IsValuePatternAvailablePropertyId, FALSE) != FALSE)
    {
        Item->Patterns |= UiaPatternValue;
    }
    if (CachedBool(Element, UIA_IsTogglePatternAvailablePropertyId, FALSE) != FALSE)
    {
        Item->Patterns |= UiaPatternToggle;
    }
    if (CachedBool(Element, UIA_IsSelectionItemPatternAvailablePropertyId, FALSE) != FALSE)
    {
        Item->Patterns |= UiaPatternSelectionItem;
    }
    if (Item->Password == FALSE && Time_StopWatchStop(Traversal->Start, 1000) < Traversal->Options->TimeoutMs)
    {
        CHAR Text[2049] = { 0 };
        ULONG Length = Util_UIA_GetText(Element, Text, ARRAYSIZE(Text)), Written = 0;
        if (Length != 0 && NT_SUCCESS(RtlUTF8ToUnicodeN(Item->Text, sizeof(Item->Text) - sizeof(WCHAR), &Written, Text,
            Length)))
        {
            Item->Text[Written / sizeof(WCHAR)] = UNICODE_NULL;
        }
    }
    if (Item->Enabled != FALSE && Item->Offscreen == FALSE &&
        Time_StopWatchStop(Traversal->Start, 1000) < Traversal->Options->TimeoutMs)
    {
        BOOL Available = FALSE;
        Item->ClickablePointValid = SUCCEEDED(Element->lpVtbl->GetClickablePoint(Element,
            &Item->ClickablePoint, &Available)) && Available != FALSE;
    }
    if (Time_StopWatchStop(Traversal->Start, 1000) >= Traversal->Options->TimeoutMs)
    {
        Observation->Truncated = TRUE;
        goto Exit;
    }
    Hr = Traversal->Walker->lpVtbl->GetFirstChildElementBuildCache(Traversal->Walker, Element, Traversal->Cache,
        &Child);
    if (FAILED(Hr))
    {
        Observation->Status = Hr;
        Observation->Truncated = TRUE;
        goto Exit;
    }
    if (Child != NULL && Depth >= Traversal->Options->MaxDepth)
    {
        Observation->Truncated = TRUE;
        goto Exit;
    }
    while (Child != NULL)
    {
        if (Observation->Count >= Traversal->Capacity ||
            Time_StopWatchStop(Traversal->Start, 1000) >= Traversal->Options->TimeoutMs)
        {
            Observation->Truncated = TRUE;
            break;
        }
        Visit(Traversal, Child, Index, Depth + 1);
        if (Time_StopWatchStop(Traversal->Start, 1000) >= Traversal->Options->TimeoutMs)
        {
            Observation->Truncated = TRUE;
            break;
        }
        Hr = Traversal->Walker->lpVtbl->GetNextSiblingElementBuildCache(Traversal->Walker, Child, Traversal->Cache,
            &Next);
        if (FAILED(Hr))
        {
            Observation->Status = Hr;
            Observation->Truncated = TRUE;
            break;
        }
        Child->lpVtbl->Release(Child);
        Child = Next;
        Next = NULL;
    }
Exit:
    if (Child != NULL)
    {
        Child->lpVtbl->Release(Child);
    }
    if (Next != NULL)
    {
        Next->lpVtbl->Release(Next);
    }
}

static
HRESULT
ObserveInThread(
    _In_ IUIAutomation* Automation,
    _In_ HWND Window,
    _In_ const UIA_OPTIONS* Options,
    _In_ ULONGLONG Start,
    _Out_ UIA_OBSERVATION* Observation)
{
    IUIAutomationCacheRequest* Cache = NULL;
    IUIAutomationElement* Root = NULL;
    IUIAutomationTreeWalker* Walker = NULL;
    UIA_TRAVERSAL Traversal;
    const PROPERTYID Properties[] = {
        UIA_NamePropertyId, UIA_LocalizedControlTypePropertyId, UIA_AutomationIdPropertyId,
        UIA_BoundingRectanglePropertyId, UIA_IsEnabledPropertyId,
        UIA_IsOffscreenPropertyId, UIA_IsPasswordPropertyId, UIA_HasKeyboardFocusPropertyId,
        UIA_IsInvokePatternAvailablePropertyId, UIA_IsValuePatternAvailablePropertyId,
        UIA_IsTogglePatternAvailablePropertyId, UIA_IsSelectionItemPatternAvailablePropertyId
    };
    HRESULT Hr;
    RtlZeroMemory(Observation, sizeof(*Observation));
    Hr = Automation->lpVtbl->CreateCacheRequest(Automation, &Cache);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Cache->lpVtbl->put_TreeScope(Cache, TreeScope_Element);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    for (ULONG Index = 0; Index < ARRAYSIZE(Properties); ++Index)
    {
        Hr = Cache->lpVtbl->AddProperty(Cache, Properties[Index]);
        if (FAILED(Hr))
        {
            goto Exit;
        }
    }
    Hr = Automation->lpVtbl->ElementFromHandleBuildCache(Automation, Window, Cache, &Root);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Root == NULL)
    {
        Hr = UIA_E_ELEMENTNOTAVAILABLE;
        goto Exit;
    }
    Hr = Automation->lpVtbl->get_ControlViewWalker(Automation, &Walker);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Traversal.Walker = Walker;
    Traversal.Cache = Cache;
    Traversal.Options = Options;
    Traversal.Observation = Observation;
    Traversal.Capacity = Options->MaxNodes;
    Traversal.Start = Start;
    Observation->Elements = (UIA_ELEMENT*)Mem_Alloc(Traversal.Capacity * sizeof(UIA_ELEMENT));
    if (Observation->Elements == NULL)
    {
        Hr = E_OUTOFMEMORY;
        goto Exit;
    }
    Visit(&Traversal, Root, -1, 0);
    Hr = Observation->Status;
Exit:
    if (Walker != NULL)
    {
        Walker->lpVtbl->Release(Walker);
    }
    if (Root != NULL)
    {
        Root->lpVtbl->Release(Root);
    }
    if (Cache != NULL)
    {
        Cache->lpVtbl->Release(Cache);
    }
    Observation->Status = Hr;
    return Hr;
}

static
HRESULT
FindRuntimeId(
    _In_ IUIAutomationTreeWalker* Walker,
    _In_ IUIAutomationElement* Element,
    _In_ const UIA_REQUEST* Request,
    _Inout_ PULONG Remaining,
    _In_ ULONG Depth,
    _Outptr_ IUIAutomationElement** Found)
{
    LONG Id[WUA_UIA_MAX_RUNTIME_ID];
    IUIAutomationElement* Child = NULL;
    IUIAutomationElement* Next = NULL;
    HRESULT Hr;
    *Found = NULL;
    if (Time_StopWatchStop(Request->Start, 1000) >= Request->Options.TimeoutMs)
    {
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    if (*Remaining == 0 || Depth > 64)
    {
        return HRESULT_FROM_WIN32(ERROR_MORE_DATA);
    }
    --*Remaining;
    if (RuntimeId(Element, Id) == Request->RuntimeIdCount &&
        RtlEqualMemory(Id, Request->RuntimeId, Request->RuntimeIdCount * sizeof(LONG)))
    {
        Element->lpVtbl->AddRef(Element);
        *Found = Element;
        return S_OK;
    }
    if (Time_StopWatchStop(Request->Start, 1000) >= Request->Options.TimeoutMs)
    {
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    Hr = Walker->lpVtbl->GetFirstChildElement(Walker, Element, &Child);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    while (Child != NULL)
    {
        Hr = FindRuntimeId(Walker, Child, Request, Remaining, Depth + 1, Found);
        if (Hr != UIA_E_ELEMENTNOTAVAILABLE)
        {
            goto Exit;
        }
        if (Time_StopWatchStop(Request->Start, 1000) >= Request->Options.TimeoutMs)
        {
            Hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            goto Exit;
        }
        Hr = Walker->lpVtbl->GetNextSiblingElement(Walker, Child, &Next);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Child->lpVtbl->Release(Child);
        Child = Next;
        Next = NULL;
    }
    Hr = UIA_E_ELEMENTNOTAVAILABLE;
Exit:
    if (Child != NULL)
    {
        Child->lpVtbl->Release(Child);
    }
    if (Next != NULL)
    {
        Next->lpVtbl->Release(Next);
    }
    return Hr;
}

static
HRESULT
ActInThread(
    _In_ IUIAutomation* Automation,
    _In_ HWND Window,
    _In_ const UIA_REQUEST* Request)
{
    IUIAutomationElement* Root = NULL;
    IUIAutomationElement* Element = NULL;
    IUIAutomationTreeWalker* Walker = NULL;
    IUnknown* Pattern = NULL;
    ULONG Remaining = 4096;
    BOOL Enabled = FALSE;
    HRESULT Hr;
    Hr = Automation->lpVtbl->ElementFromHandle(Automation, Window, &Root);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Root == NULL)
    {
        Hr = UIA_E_ELEMENTNOTAVAILABLE;
        goto Exit;
    }
    Hr = Automation->lpVtbl->get_ControlViewWalker(Automation, &Walker);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = FindRuntimeId(Walker, Root, Request, &Remaining, 0, &Element);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Time_StopWatchStop(Request->Start, 1000) >= Request->Options.TimeoutMs)
    {
        Hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        goto Exit;
    }
    Hr = Element->lpVtbl->get_CurrentIsEnabled(Element, &Enabled);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Enabled == FALSE)
    {
        Hr = UIA_E_ELEMENTNOTENABLED;
        goto Exit;
    }
    if (Time_StopWatchStop(Request->Start, 1000) >= Request->Options.TimeoutMs)
    {
        Hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        goto Exit;
    }
    switch (Request->Operation)
    {
        case UiaActionInvoke:
            Hr = Element->lpVtbl->GetCurrentPatternAs(Element, UIA_InvokePatternId,
                &IID_IUIAutomationInvokePattern, (PVOID*)&Pattern);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            if (Time_StopWatchStop(Request->Start, 1000) >= Request->Options.TimeoutMs)
            {
                Hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
                goto Exit;
            }
            Hr = ((IUIAutomationInvokePattern*)Pattern)->lpVtbl->Invoke((IUIAutomationInvokePattern*)Pattern);
            break;
        case UiaActionSetValue:
        {
            BOOL ReadOnly = TRUE;
            IUIAutomationValuePattern* ValuePattern;
            BSTR Text;

            Hr = Element->lpVtbl->GetCurrentPatternAs(Element, UIA_ValuePatternId,
                &IID_IUIAutomationValuePattern, (PVOID*)&Pattern);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            ValuePattern = (IUIAutomationValuePattern*)Pattern;
            if (Time_StopWatchStop(Request->Start, 1000) >= Request->Options.TimeoutMs)
            {
                Hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
                goto Exit;
            }
            Hr = ValuePattern->lpVtbl->get_CurrentIsReadOnly(ValuePattern, &ReadOnly);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            if (ReadOnly != FALSE)
            {
                Hr = E_ACCESSDENIED;
                goto Exit;
            }
            Text = SysAllocString(Request->Value);
            if (Text == NULL)
            {
                Hr = E_OUTOFMEMORY;
                goto Exit;
            }
            if (Time_StopWatchStop(Request->Start, 1000) >= Request->Options.TimeoutMs)
            {
                Hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            } else
            {
                Hr = ValuePattern->lpVtbl->SetValue(ValuePattern, Text);
            }
            SysFreeString(Text);
            break;
        }
        case UiaActionToggle:
            Hr = Element->lpVtbl->GetCurrentPatternAs(Element, UIA_TogglePatternId,
                &IID_IUIAutomationTogglePattern, (PVOID*)&Pattern);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            if (Time_StopWatchStop(Request->Start, 1000) >= Request->Options.TimeoutMs)
            {
                Hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
                goto Exit;
            }
            Hr = ((IUIAutomationTogglePattern*)Pattern)->lpVtbl->Toggle((IUIAutomationTogglePattern*)Pattern);
            break;
        case UiaActionSelect:
            Hr = Element->lpVtbl->GetCurrentPatternAs(Element, UIA_SelectionItemPatternId,
                &IID_IUIAutomationSelectionItemPattern, (PVOID*)&Pattern);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            if (Time_StopWatchStop(Request->Start, 1000) >= Request->Options.TimeoutMs)
            {
                Hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
                goto Exit;
            }
            Hr = ((IUIAutomationSelectionItemPattern*)Pattern)->lpVtbl->Select(
                (IUIAutomationSelectionItemPattern*)Pattern);
            break;
        default:
            Hr = E_INVALIDARG;
            goto Exit;
    }
Exit:
    if (Pattern != NULL)
    {
        Pattern->lpVtbl->Release(Pattern);
    }
    if (Walker != NULL)
    {
        Walker->lpVtbl->Release(Walker);
    }
    if (Element != NULL)
    {
        Element->lpVtbl->Release(Element);
    }
    if (Root != NULL)
    {
        Root->lpVtbl->Release(Root);
    }
    return Hr;
}

static
_Function_class_(USER_THREAD_START_ROUTINE)
NTSTATUS
NTAPI
UiaThread(
    _In_ PVOID Parameter)
{
    UIA_CONTEXT* Context = (UIA_CONTEXT*)Parameter;
    UIA_REQUEST* Request;
    IUIAutomation* Automation = NULL;
    HRESULT Init = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    HRESULT Hr;
    NTSTATUS Status;
    DWORD Process, Thread;
    LOGICAL Stop;

    for (;;)
    {
        Status = NtWaitForSingleObject(Context->Wake, FALSE, NULL);
        RtlAcquireSRWLockExclusive(&Context->Lock);
        Stop = Context->Stopping != FALSE || !NT_SUCCESS(Status);
        Context->Stopping = Stop;
        Request = Context->Pending;
        RtlReleaseSRWLockExclusive(&Context->Lock);
        if (Request == NULL)
        {
            if (Stop != FALSE)
            {
                break;
            }
            continue;
        }
        Hr = HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
        if (Stop != FALSE)
        {
            goto Complete;
        }
        Thread = GetWindowThreadProcessId(Request->Window, &Process);
        if (Thread != Request->Thread || Process != Request->Process || !IsWindow(Request->Window))
        {
            Hr = UIA_E_ELEMENTNOTAVAILABLE;
            goto Complete;
        }
        if (Time_StopWatchStop(Request->Start, 1000) >= Request->Options.TimeoutMs)
        {
            Hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            goto Complete;
        }
        if (FAILED(Init))
        {
            Init = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        }
        Hr = Init;
        if (FAILED(Hr))
        {
            goto Complete;
        }
        if (Time_StopWatchStop(Request->Start, 1000) >= Request->Options.TimeoutMs)
        {
            Hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            goto Complete;
        }
        Hr = PrepareAutomation(&Automation, Request->Options.TimeoutMs);
        if (FAILED(Hr))
        {
            goto Complete;
        }
        if (Time_StopWatchStop(Request->Start, 1000) >= Request->Options.TimeoutMs)
        {
            Hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        } else if (Request->Operation == 0)
        {
            Hr = ObserveInThread(Automation, Request->Window, &Request->Options, Request->Start, &Request->Observation);
        } else
        {
            Hr = ActInThread(Automation, Request->Window, Request);
        }
Complete:
        Request->Observation.Status = Hr;
        RtlAcquireSRWLockExclusive(&Context->Lock);
        Context->Pending = NULL;
        RtlReleaseSRWLockExclusive(&Context->Lock);
        NtSetEvent(Request->Completed, NULL);
        ReleaseRequest(Request);
        if (Stop != FALSE)
        {
            break;
        }
    }
    if (Automation != NULL)
    {
        Automation->lpVtbl->Release(Automation);
    }
    if (SUCCEEDED(Init))
    {
        CoUninitialize();
    }
    ReleaseContext(Context);
    return STATUS_SUCCESS;
}

HRESULT
UiaCreate(
    _Outptr_ UIA_CONTEXT** Context)
{
    UIA_CONTEXT* Value;
    HRESULT Hr;

    *Context = NULL;
    Value = (UIA_CONTEXT*)Mem_Alloc(sizeof(*Value));
    if (Value == NULL)
    {
        return E_OUTOFMEMORY;
    }
    RtlZeroMemory(Value, sizeof(*Value));
    RtlInitializeSRWLock(&Value->Lock);
    Hr = Err_NtStatusToHr(NtCreateEvent(&Value->Wake, EVENT_ALL_ACCESS, NULL, SynchronizationEvent, FALSE));
    if (FAILED(Hr))
    {
        Mem_Free(Value);
        return Hr;
    }
    Value->References = 2;
    Hr = Err_NtStatusToHr(PS_CreateThread(NtCurrentProcess(), FALSE, UiaThread, Value, NULL, NULL));
    if (FAILED(Hr))
    {
        Value->References = 1;
        ReleaseContext(Value);
        return Hr;
    }
    *Context = Value;
    return S_OK;
}

VOID
UiaDestroy(
    _In_ UIA_CONTEXT* Context)
{
    RtlAcquireSRWLockExclusive(&Context->Lock);
    Context->Stopping = TRUE;
    NtSetEvent(Context->Wake, NULL);
    RtlReleaseSRWLockExclusive(&Context->Lock);
    // A blocked provider retains the thread's reference until it returns or this process exits.
    ReleaseContext(Context);
}

static
HRESULT
NewRequest(
    _In_ HWND Window,
    _In_ const UIA_OPTIONS* Options,
    _Outptr_ UIA_REQUEST** Request)
{
    UIA_REQUEST* Value;
    HRESULT Hr;

    *Request = NULL;
    if (Options->MaxNodes == 0 || Options->MaxNodes > 1024 || Options->MaxDepth > 64 ||
        Options->TimeoutMs == 0 || Options->TimeoutMs > WUA_UIA_MAX_TIMEOUT_MS)
    {
        return E_INVALIDARG;
    }
    Value = (UIA_REQUEST*)Mem_Alloc(sizeof(*Value));
    if (Value == NULL)
    {
        return E_OUTOFMEMORY;
    }
    RtlZeroMemory(Value, sizeof(*Value));
    Value->Window = Window;
    Value->Options = *Options;
    Value->Thread = GetWindowThreadProcessId(Window, &Value->Process);
    if (Value->Thread == 0 || !IsWindow(Window))
    {
        Mem_Free(Value);
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    Hr = Err_NtStatusToHr(NtCreateEvent(&Value->Completed, EVENT_ALL_ACCESS, NULL, NotificationEvent, FALSE));
    if (FAILED(Hr))
    {
        Mem_Free(Value);
        return Hr;
    }
    Value->References = 1;
    *Request = Value;
    return S_OK;
}

HRESULT
UiaObserve(
    _Inout_ UIA_CONTEXT* Context,
    _In_ HWND Window,
    _In_ const UIA_OPTIONS* Options,
    _Out_ UIA_OBSERVATION* Observation)
{
    UIA_REQUEST* Request = NULL;
    HRESULT Hr;

    RtlZeroMemory(Observation, sizeof(*Observation));
    Hr = NewRequest(Window, Options, &Request);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = RunRequest(Context, Request);
    if (SUCCEEDED(Hr))
    {
        *Observation = Request->Observation;
        Request->Observation.Elements = NULL;
        Hr = Observation->Status;
    }
Exit:
    if (Request != NULL)
    {
        ReleaseRequest(Request);
    }
    Observation->Status = Hr;
    return Hr;
}

HRESULT
UiaAct(
    _Inout_ UIA_CONTEXT* Context,
    _In_ HWND Window,
    _In_reads_(RuntimeIdCount) const LONG* RuntimeId,
    _In_ ULONG RuntimeIdCount,
    _In_ UIA_ACTION Action,
    _In_ PCWSTR Value,
    _In_ DWORD TimeoutMs)
{
    UIA_OPTIONS Options = { 256, 12, TimeoutMs };
    UIA_REQUEST* Request = NULL;
    SIZE_T Length = wcslen(Value);
    HRESULT Hr = E_INVALIDARG;

    if (RuntimeIdCount == 0 || RuntimeIdCount > WUA_UIA_MAX_RUNTIME_ID || Length > 16384 ||
        Action < UiaActionInvoke || Action > UiaActionSelect || (Action != UiaActionSetValue && Length != 0))
    {
        goto Exit;
    }
    Hr = NewRequest(Window, &Options, &Request);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Request->Operation = Action;
    Request->RuntimeIdCount = RuntimeIdCount;
    RtlCopyMemory(Request->RuntimeId, RuntimeId, RuntimeIdCount * sizeof(LONG));
    if (Action == UiaActionSetValue)
    {
        Request->Value = (PWSTR)Mem_Alloc((Length + 1) * sizeof(WCHAR));
        if (Request->Value == NULL)
        {
            Hr = E_OUTOFMEMORY;
            goto Exit;
        }
        RtlCopyMemory(Request->Value, Value, (Length + 1) * sizeof(WCHAR));
    }
    Hr = RunRequest(Context, Request);
    if (SUCCEEDED(Hr))
    {
        Hr = Request->Observation.Status;
    }
Exit:
    if (Request != NULL)
    {
        ReleaseRequest(Request);
    }
    return Hr;
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
    if (SUCCEEDED(Element->lpVtbl->GetCurrentPropertyValue(Element, PropertyId, &Variant)) &&
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
    if (SUCCEEDED(TextRange->lpVtbl->GetText(TextRange, BufferCch, &Text)) && Text != NULL)
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
    if (SUCCEEDED(Element->lpVtbl->GetCurrentPatternAs(Element, PatternId, &IID_IUIAutomationTextPattern,
        (PVOID*)&TextPattern)))
    {
        if (SUCCEEDED(TextPattern->lpVtbl->get_DocumentRange(TextPattern, &DocumentRange)))
        {
            BytesWritten = Util_UIA_GetTextFromTextRange(DocumentRange, Buffer, BufferCch);
            DocumentRange->lpVtbl->Release(DocumentRange);
        }
        TextPattern->lpVtbl->Release(TextPattern);
    }
    return BytesWritten;
}

static
ULONG
Util_UIA_GetText(
    _In_ IUIAutomationElement * Element,
    _Out_writes_(BufferCch) PSTR Buffer,
    _In_ ULONG BufferCch)
{
    IUIAutomationTextChildPattern* TextChildPattern;
    IUIAutomationTextRange* TextRange;
    ULONG BytesWritten;

    if (BufferCch == 0)
    {
        return 0;
    }

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
    if (SUCCEEDED(Element->lpVtbl->GetCurrentPatternAs(Element, UIA_TextChildPatternId,
        &IID_IUIAutomationTextChildPattern, (PVOID*)&TextChildPattern)))
    {
        if (SUCCEEDED(TextChildPattern->lpVtbl->get_TextRange(TextChildPattern, &TextRange)))
        {
            BytesWritten = Util_UIA_GetTextFromTextRange(TextRange, Buffer, BufferCch);
            TextRange->lpVtbl->Release(TextRange);
        } else
        {
            BytesWritten = 0;
        }
        TextChildPattern->lpVtbl->Release(TextChildPattern);
        if (BytesWritten > 0)
        {
            return BytesWritten;
        }
    }

    return 0;
}
