#include "pch.h"
#include "Server.h"
#include <corerror.h>

typedef struct _INPUT_OBSERVATION
{
    GUITHREADINFO Gui;
    DWORD Thread;
    HWND CaretRoot;
    RECT CaretBounds, CaretRootBounds;
    LOGICAL Available, CaretMapped, CaretGeometryKnown;
} INPUT_OBSERVATION;

static
LOGICAL
CaretScreenBounds(
    _In_ const GUITHREADINFO* Gui,
    _Out_ RECT* Bounds)
{
    POINT Origin = { 0 }, First, Last;
    RtlZeroMemory(Bounds, sizeof(*Bounds));
    if (Gui->hwndCaret == NULL || !IsWindow(Gui->hwndCaret))
    {
        return FALSE;
    }
    HWND Root = NtUserGetAncestor(Gui->hwndCaret, GA_ROOT);
    if (Root == NULL ||
        !AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(Root),
                                      GetWindowDpiAwarenessContext(Gui->hwndCaret)) ||
        GetDpiForWindow(Root) != GetDpiForWindow(Gui->hwndCaret))
    {
        return FALSE;
    }
    // Use the top-level transform; child-window point bounds can reject valid logical coordinates at scaled DPI.
    Err_SetLastError(ERROR_SUCCESS);
    if (!MapWindowPoints(Gui->hwndCaret, NULL, &Origin, 1) && Err_GetLastError() != 0)
    {
        return FALSE;
    }
    if (!PhysicalToLogicalPointForPerMonitorDPI(Root, &Origin))
    {
        return FALSE;
    }
    // rcCaret is in the target's logical client space. The manifest keeps this process in physical space.
    LONG Direction = (GetWindowLongPtrW(Gui->hwndCaret, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) != 0 ? -1 : 1;
    LONGLONG Left = (LONGLONG)Origin.x + Direction * (LONGLONG)Gui->rcCaret.left;
    LONGLONG Right = (LONGLONG)Origin.x + Direction * (LONGLONG)Gui->rcCaret.right;
    LONGLONG Top = (LONGLONG)Origin.y + Gui->rcCaret.top, Bottom = (LONGLONG)Origin.y + Gui->rcCaret.bottom;
    if (Left < MINLONG32 || Left > MAXLONG32 || Right < MINLONG32 || Right > MAXLONG32 || Top < MINLONG32 ||
        Top > MAXLONG32 || Bottom < MINLONG32 || Bottom > MAXLONG32)
    {
        return FALSE;
    }
    First.x = (LONG)Left;
    First.y = (LONG)Top;
    Last.x = (LONG)Right;
    Last.y = (LONG)Bottom;
    if (!LogicalToPhysicalPointForPerMonitorDPI(Root, &First) || !LogicalToPhysicalPointForPerMonitorDPI(Root, &Last))
    {
        return FALSE;
    }
    Bounds->left = min(First.x, Last.x);
    Bounds->right = max(First.x, Last.x);
    Bounds->top = min(First.y, Last.y);
    Bounds->bottom = max(First.y, Last.y);
    return TRUE;
}

static
LOGICAL
SameGuiState(
    _In_ const GUITHREADINFO* A,
    _In_ const GUITHREADINFO* B)
{
    return A->hwndActive == B->hwndActive && A->hwndFocus == B->hwndFocus && A->hwndCapture == B->hwndCapture &&
           A->hwndMenuOwner == B->hwndMenuOwner && A->hwndMoveSize == B->hwndMoveSize && A->hwndCaret == B->hwndCaret &&
           ((A->flags ^ B->flags) & ~GUI_CARETBLINKING) == 0 &&
           (A->hwndCaret == NULL || EqualRect(&A->rcCaret, &B->rcCaret));
}

static
HRESULT
BuildInteraction(
    _Inout_ CUA_COMMAND* Command,
    _Inout_ CUA_OBSERVATION* Observed,
    _In_ const INPUT_OBSERVATION* Input,
    _Outptr_ IJsonValue** Value)
{
    IJsonObject *Object = NULL, *Focus = NULL, *Caret = NULL;
    IJsonValue* Child = NULL;
    IJsonValueStatics* Factory = Command->Factory;
    const GUITHREADINFO* Gui = &Input->Gui;
    GUITHREADINFO Current = { sizeof(Current) };
    HWND FocusRoot;
    WCHAR ClassName[256];
    HRESULT Hr;
    *Value = NULL;
    LOGICAL ModalLoop = (Gui->flags &
                         (GUI_INMENUMODE | GUI_SYSTEMMENUMODE | GUI_POPUPMENUMODE | GUI_INMOVESIZE)) != 0;
    LOGICAL WasCurrent = Input->Available && Observed->UiaWindow.Handle == Observed->Foreground &&
                         Gui->hwndActive == Observed->UiaWindow.Handle &&
                         NtUserGetAncestor(Gui->hwndActive, GA_ROOT) == Gui->hwndActive;
    PCWSTR State = Observed->Window.Handle != NULL && Observed->Window.Handle != Observed->Foreground ? L"background"
                   : !Input->Available                                            ? L"unavailable"
                   : WasCurrent                                                   ? L"current"
                                                                                  : L"changed";
    if (NtUserGetForegroundWindow() != Observed->Foreground ||
        (WasCurrent && (!NtUserGetGUIThreadInfo(Input->Thread, &Current) || !SameGuiState(Gui, &Current))))
    {
        State = L"changed";
    }
    Hr = Data_JsonCreateObject(&Object);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (wcscmp(State, L"current") == 0)
    {
        Hr = CuaSetWindowHandle(Factory, Object, L"active_window", Gui->hwndActive);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        FocusRoot = ModalLoop == FALSE && Gui->hwndFocus != NULL
                        ? NtUserGetAncestor(Gui->hwndFocus, GA_ROOT) : NULL;
        if (FocusRoot != NULL)
        {
            Hr = Data_JsonCreateObject(&Focus);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Hr = CuaSetWindowHandle(Factory, Focus, L"window_handle", Gui->hwndFocus);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Hr = Data_JsonObjectSetBoolean(Factory, Focus, L"within_observed_window",
                                                FocusRoot == Observed->UiaWindow.Handle);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            ClassName[0] = 0;
            GetClassNameW(Gui->hwndFocus, ClassName, ARRAYSIZE(ClassName));
            Hr = Data_JsonObjectSetString(Factory, Focus, L"control_class", ClassName, (ULONG)wcslen(ClassName));
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Hr = Focus->lpVtbl->QueryInterface(Focus, &IID_IJsonValue, (PVOID*)&Child);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Hr = Data_JsonObjectSetValue(Object, L"focus", Child);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Child->lpVtbl->Release(Child);
            Child = NULL;
        } else
        {
            Hr = Data_JsonObjectSetNull(Command->NullFactory, Object, L"focus");
            if (FAILED(Hr))
            {
                goto Exit;
            }
        }
        Hr = CuaSetWindowHandle(Factory, Object, L"mouse_capture_window", Gui->hwndCapture);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        PCWSTR Menu = Gui->flags & GUI_SYSTEMMENUMODE  ? L"system"
                      : Gui->flags & GUI_POPUPMENUMODE ? L"popup"
                      : Gui->flags & GUI_INMENUMODE    ? L"menu"
                                                       : L"none";
        Hr = Data_JsonObjectSetString(Factory, Object, L"menu", Menu, (ULONG)wcslen(Menu));
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = CuaSetWindowHandle(
            Factory, Object, L"menu_owner_window",
            Gui->flags & (GUI_INMENUMODE | GUI_SYSTEMMENUMODE | GUI_POPUPMENUMODE) ? Gui->hwndMenuOwner : NULL);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = Data_JsonObjectSetBoolean(Factory, Object, L"move_size_active", (Gui->flags & GUI_INMOVESIZE) != 0);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = CuaSetWindowHandle(Factory, Object, L"move_size_window",
                               Gui->flags & GUI_INMOVESIZE ? Gui->hwndMoveSize : NULL);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        if (ModalLoop == FALSE && Input->CaretRoot != NULL && Input->CaretRoot == Gui->hwndActive)
        {
            RECT CurrentBounds, Bounds = Input->CaretBounds;
            LOGICAL Aligned = Input->CaretMapped && Input->CaretGeometryKnown &&
                              SUCCEEDED(GetWindowCaptureBounds(Input->CaretRoot, &CurrentBounds)) &&
                              EqualRect(&CurrentBounds, &Input->CaretRootBounds) &&
                              (Observed->Window.Handle == NULL || (Input->CaretRoot == Observed->Window.Handle &&
                                                        EqualRect(&Input->CaretRootBounds,
                                                                  &Observed->Frame.WindowBounds)));
            Hr = Data_JsonCreateObject(&Caret);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Hr = CuaSetWindowHandle(Factory, Caret, L"window_handle", Input->CaretRoot);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Hr = Data_JsonObjectSetBoolean(Factory, Caret, L"image_geometry_consistent", Aligned);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            OffsetRect(&Bounds, -Observed->Frame.ScreenBounds.left, -Observed->Frame.ScreenBounds.top);
            if (Aligned != FALSE)
            {
                Hr = CuaSetRectangle(Factory, Caret, L"bounds_image_px", &Bounds);
                if (FAILED(Hr))
                {
                    goto Exit;
                }
            }
            Hr = Caret->lpVtbl->QueryInterface(Caret, &IID_IJsonValue, (PVOID*)&Child);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Hr = Data_JsonObjectSetValue(Object, L"caret_hint", Child);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Child->lpVtbl->Release(Child);
            Child = NULL;
        } else
        {
            Hr = Data_JsonObjectSetNull(Command->NullFactory, Object, L"caret_hint");
            if (FAILED(Hr))
            {
                goto Exit;
            }
        }
    } else
    {
        Hr = Data_JsonObjectSetNull(Command->NullFactory, Object, L"focus");
        if (FAILED(Hr))
        {
            goto Exit;
        }
    }
    Hr = Data_JsonObjectSetString(Factory, Object, L"state", State, (ULONG)wcslen(State));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Object->lpVtbl->QueryInterface(Object, &IID_IJsonValue, (PVOID*)Value);
Exit:
    if (Child != NULL)
    {
        Child->lpVtbl->Release(Child);
    }
    if (Caret != NULL)
    {
        Caret->lpVtbl->Release(Caret);
    }
    if (Focus != NULL)
    {
        Focus->lpVtbl->Release(Focus);
    }
    if (Object != NULL)
    {
        Object->lpVtbl->Release(Object);
    }
    return Hr;
}

static
HRESULT
DescribeWindows(
    _Inout_ CUA_COMMAND* Command,
    _In_ CUA_OBSERVATION* Observed,
    _In_ IJsonObject* Result)
{
    HWND* Handles = NULL;
    IJsonVector *Windows = NULL, *Related = NULL;
    IJsonValue* Value = NULL;
    PSTR Serialized = NULL;
    ULONG Bytes, Used = 0, Capacity = 1024, Count, Listed = 0;
    LOGICAL Truncated = FALSE;
    NTSTATUS Status;
    HRESULT Hr;
    for (;;)
    {
        if (Capacity > MAXULONG / sizeof(*Handles))
        {
            Hr = HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
            goto Exit;
        }
        Handles = (HWND*)Mem_Alloc((SIZE_T)Capacity * sizeof(*Handles));
        if (Handles == NULL)
        {
            Hr = E_OUTOFMEMORY;
            goto Exit;
        }
        Status = NtUserBuildHwndList(NULL, NULL, FALSE, FALSE, 0, Capacity, Handles, &Count);
        if (Status != STATUS_BUFFER_TOO_SMALL)
        {
            break;
        }
        Mem_Free(Handles);
        Handles = NULL;
        if (Count <= Capacity)
        {
            Hr = E_FAIL;
            goto Exit;
        }
        Capacity = Count;
    }
    if (!NT_SUCCESS(Status))
    {
        Hr = Err_NtStatusToHr(Status);
        goto Exit;
    }
    if (Count > Capacity)
    {
        Hr = E_FAIL;
        goto Exit;
    }
    Hr = Data_JsonCreateArray(&Windows);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Observed->Window.Handle != NULL)
    {
        Hr = CuaBuildWindowInfo(Command, &Observed->Window, &Observed->Frame, &Value);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = Windows->lpVtbl->Append(Windows, Value);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Value->lpVtbl->Release(Value);
        Value = NULL;
        Hr = Data_JsonCreateArray(&Related);
        if (FAILED(Hr))
        {
            goto Exit;
        }
    }
    for (ULONG Index = 0; Index < Count && Handles[Index] != HWND_BOTTOM; ++Index)
    {
        CUA_WINDOW Window;
        DWORD Cloaked = 0;
        RECT Bounds;
        if (IsWindowVisible(Handles[Index]) == FALSE)
        {
            continue;
        }
        if (SUCCEEDED(DwmGetWindowAttribute(Handles[Index], DWMWA_CLOAKED, &Cloaked, sizeof(Cloaked))) &&
            Cloaked != 0)
        {
            continue;
        }
        if (IsIconic(Handles[Index]) == FALSE &&
            (GetWindowRect(Handles[Index], &Bounds) == FALSE || IsRectEmpty(&Bounds) != FALSE))
        {
            continue;
        }
        if (++Listed > 1024)
        {
            Truncated = TRUE;
            break;
        }
        if (Related != NULL && GetWindow(Handles[Index], GW_OWNER) != Observed->Window.Handle)
        {
            continue;
        }
        if (FAILED(CuaQueryWindow(Handles[Index], &Window)) ||
            FAILED(CuaBuildWindowInfo(Command, &Window, &Observed->Frame, &Value)))
        {
            continue;
        }
        Hr = Data_JsonStringifyUtf8(Value, &Serialized, &Bytes);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Mem_Free(Serialized);
        Serialized = NULL;
        if (Used + Bytes > 512 * 1024)
        {
            Truncated = TRUE;
            break;
        }
        Used += Bytes;
        IJsonVector* Destination = Related != NULL ? Related : Windows;
        Hr = Destination->lpVtbl->Append(Destination, Value);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Value->lpVtbl->Release(Value);
        Value = NULL;
    }
    if (Value != NULL)
    {
        Value->lpVtbl->Release(Value);
    }
    Value = NULL;
    if (Related != NULL)
    {
        Hr = Related->lpVtbl->QueryInterface(Related, &IID_IJsonValue, (PVOID*)&Value);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = Data_JsonObjectSetValue(Result, L"related_windows", Value);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = Data_JsonObjectSetBoolean(Command->Factory, Result, L"related_windows_truncated", Truncated);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Value->lpVtbl->Release(Value);
        Value = NULL;
    } else
    {
        Hr = Data_JsonObjectSetBoolean(Command->Factory, Result, L"windows_truncated", Truncated);
        if (FAILED(Hr))
        {
            goto Exit;
        }
    }
    Hr = Windows->lpVtbl->QueryInterface(Windows, &IID_IJsonValue, (PVOID*)&Value);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetValue(Result, L"windows", Value);
Exit:
    Mem_Free(Serialized);
    Mem_Free(Handles);
    if (Value != NULL)
    {
        Value->lpVtbl->Release(Value);
    }
    if (Related != NULL)
    {
        Related->lpVtbl->Release(Related);
    }
    if (Windows != NULL)
    {
        Windows->lpVtbl->Release(Windows);
    }
    return Hr;
}

static
HRESULT
BuildElement(
    _In_ CUA_COMMAND* Command,
    _In_ CUA_OBSERVATION* Observed,
    _In_ ULONG Index,
    _Outptr_ IJsonValue** Value)
{
    const UIA_ELEMENT* Element = &Observed->Elements[Index];
    IJsonObject *Object = NULL, *Position = NULL, *Patterns = NULL;
    IJsonValue* Child = NULL;
    IJsonValueStatics* Factory = Command->Factory;
    WCHAR RuntimeId[WUA_UIA_MAX_RUNTIME_ID * 12];
    ULONG Length = 0;
    RECT Bounds;
    HRESULT Hr;
    *Value = NULL;
    Hr = Data_JsonCreateObject(&Object);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Element->RuntimeIdCount != 0)
    {
        for (ULONG Part = 0; Part < Element->RuntimeIdCount; Part++)
        {
            ULONG Written = Str_PrintfExW(RuntimeId + Length, ARRAYSIZE(RuntimeId) - Length,
                Part == 0 ? L"%ld" : L",%ld", Element->RuntimeId[Part]);
            if (Written == 0)
            {
                Hr = E_FAIL;
                goto Exit;
            }
            Length += Written;
        }
        Hr = Data_JsonObjectSetString(Factory, Object, L"runtime_id", RuntimeId, Length);
        if (FAILED(Hr))
        {
            goto Exit;
        }
    }
    Hr = Data_JsonObjectSetString(Factory, Object, L"name", Element->Name, (ULONG)wcslen(Element->Name));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetString(Factory, Object, L"role", Element->Role, (ULONG)wcslen(Element->Role));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetString(Factory, Object, L"automation_id", Element->AutomationId,
                                       (ULONG)wcslen(Element->AutomationId));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    PCWSTR Text = Element->Password ? L"" : Element->Text;
    Hr = Data_JsonObjectSetString(Factory, Object, L"text", Text, (ULONG)wcslen(Text));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Element->ParentIndex >= 0 && (ULONG)Element->ParentIndex < Observed->ElementCount)
    {
        Hr = Data_JsonObjectSetNumber(Factory, Object, L"parent", Element->ParentIndex);
        if (FAILED(Hr))
        {
            goto Exit;
        }
    }
    Hr = Data_JsonObjectSetBoolean(Factory, Object, L"enabled", Element->Enabled);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetBoolean(Factory, Object, L"offscreen", Element->Offscreen);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetBoolean(Factory, Object, L"password", Element->Password);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetBoolean(Factory, Object, L"focused", Element->Focused);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    LOGICAL InFrame = Element->BoundsValid && Observed->UiaImageAligned &&
                      IntersectRect(&Bounds, &Element->Bounds, &Observed->Frame.ScreenBounds);
    if (InFrame != FALSE)
    {
        OffsetRect(&Bounds, -Observed->Frame.ScreenBounds.left, -Observed->Frame.ScreenBounds.top);
        Hr = CuaSetRectangle(Factory, Object, L"bounds_image_px", &Bounds);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        POINT Point;
        if (Element->Enabled && Element->Offscreen == FALSE &&
            CuaElementPoint(Element, &Observed->Frame.ScreenBounds, Observed->Frame.Monitors,
                            Observed->Frame.MonitorCount, &Point))
        {
            Hr = Data_JsonCreateObject(&Position);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Hr = Data_JsonObjectSetNumber(Factory, Position, L"x", Point.x - Observed->Frame.ScreenBounds.left);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Hr = Data_JsonObjectSetNumber(Factory, Position, L"y", Point.y - Observed->Frame.ScreenBounds.top);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Hr = Position->lpVtbl->QueryInterface(Position, &IID_IJsonValue, (PVOID*)&Child);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Hr = Data_JsonObjectSetValue(Object, L"point_image_px", Child);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Child->lpVtbl->Release(Child);
            Child = NULL;
        }
    }
    Hr = Data_JsonCreateObject(&Patterns);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetBoolean(Factory, Patterns, L"invoke", (Element->Patterns & UiaPatternInvoke) != 0);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetBoolean(Factory, Patterns, L"set_value", (Element->Patterns & UiaPatternValue) != 0);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetBoolean(Factory, Patterns, L"toggle", (Element->Patterns & UiaPatternToggle) != 0);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetBoolean(Factory, Patterns, L"select", (Element->Patterns & UiaPatternSelectionItem) != 0);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Patterns->lpVtbl->QueryInterface(Patterns, &IID_IJsonValue, (PVOID*)&Child);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetValue(Object, L"patterns", Child);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Object->lpVtbl->QueryInterface(Object, &IID_IJsonValue, (PVOID*)Value);
Exit:
    if (Child != NULL)
    {
        Child->lpVtbl->Release(Child);
    }
    if (Patterns != NULL)
    {
        Patterns->lpVtbl->Release(Patterns);
    }
    if (Position != NULL)
    {
        Position->lpVtbl->Release(Position);
    }
    if (Object != NULL)
    {
        Object->lpVtbl->Release(Object);
    }
    return Hr;
}

static
HRESULT
DescribeElements(
    _Inout_ CUA_COMMAND* Command,
    _Inout_ CUA_OBSERVATION* Observed,
    _In_ const UIA_OPTIONS* Options,
    _In_ LOGICAL Requested,
    _In_ LOGICAL GeometryKnown,
    _In_ const RECT* PreviousBounds,
    _In_ IJsonObject* Result)
{
    IJsonVector* Elements = NULL;
    IJsonValue* Value = NULL;
    PSTR Serialized = NULL;
    ULONG Bytes, Used = 0;
    IJsonValueStatics* Factory = Command->Factory;
    HRESULT Hr;
    Hr = CuaSetWindowHandle(Factory, Result, L"uia_target", Observed->UiaWindow.Handle);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    PCWSTR State = Requested ? L"unavailable" : L"disabled";
    Hr = Data_JsonCreateArray(&Elements);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Requested && Observed->UiaWindow.Handle != NULL)
    {
        RECT Current;
        UIA_OBSERVATION Tree = { 0 };
        HRESULT UiaStatus = UiaObserve(Command->Server->Uia, Observed->UiaWindow.Handle, Options, &Tree);
        Observed->Elements = Tree.Elements;
        Observed->ElementCount = Tree.Count;
        Observed->UiaImageAligned = GeometryKnown &&
                                SUCCEEDED(GetWindowCaptureBounds(Observed->UiaWindow.Handle, &Current)) &&
                                EqualRect(&Current, PreviousBounds) &&
                                (Observed->Window.Handle != NULL ||
                                 NtUserGetForegroundWindow() == Observed->Foreground);
        if (Observed->UiaImageAligned && Observed->Window.Handle != NULL)
        {
            Observed->UiaImageAligned = SUCCEEDED(CuaClientBounds(Observed->Window.Handle, &Current)) &&
                                    EqualRect(&Current, &Observed->Frame.ClientBounds) &&
                                    GetDpiForWindow(Observed->Window.Handle) == Observed->Frame.WindowDpi;
        }
        Hr = Data_JsonObjectSetBoolean(Factory, Result, L"uia_image_geometry_consistent", Observed->UiaImageAligned);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        State = UiaStatus == HRESULT_FROM_WIN32(ERROR_TIMEOUT) || UiaStatus == COR_E_TIMEOUT ? L"timeout"
                : UiaStatus == HRESULT_FROM_WIN32(ERROR_BUSY)                               ? L"busy"
                : FAILED(UiaStatus)                                                        ? L"failed"
                : Tree.Truncated                                                           ? L"truncated"
                                                                                           : L"ok";
        Hr = Data_JsonObjectSetBoolean(Factory, Result, L"uia_truncated", Tree.Truncated);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        for (ULONG Index = 0; Index < Observed->ElementCount; ++Index)
        {
            Hr = BuildElement(Command, Observed, Index, &Value);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Hr = Data_JsonStringifyUtf8(Value, &Serialized, &Bytes);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Mem_Free(Serialized);
            Serialized = NULL;
            if (Used + Bytes > 2 * 1024 * 1024)
            {
                Hr = Data_JsonObjectSetBoolean(Factory, Result, L"uia_truncated", TRUE);
                if (FAILED(Hr))
                {
                    goto Exit;
                }
                Hr = Data_JsonObjectSetString(Factory, Result, L"uia_truncation_reason", L"json_byte_budget", 16);
                if (FAILED(Hr))
                {
                    goto Exit;
                }
                if (SUCCEEDED(UiaStatus))
                {
                    State = L"truncated";
                }
                Observed->ElementCount = Index;
                break;
            }
            Used += Bytes;
            Hr = Elements->lpVtbl->Append(Elements, Value);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Value->lpVtbl->Release(Value);
            Value = NULL;
        }
    }
    if (Value != NULL)
    {
        Value->lpVtbl->Release(Value);
    }
    Value = NULL;
    Hr = Data_JsonObjectSetString(Factory, Result, L"uia_state", State, (ULONG)wcslen(State));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Elements->lpVtbl->QueryInterface(Elements, &IID_IJsonValue, (PVOID*)&Value);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetValue(Result, L"elements", Value);
Exit:
    Mem_Free(Serialized);
    if (Value != NULL)
    {
        Value->lpVtbl->Release(Value);
    }
    if (Elements != NULL)
    {
        Elements->lpVtbl->Release(Elements);
    }
    return Hr;
}

static
HRESULT
SaveImage(
    _In_ PCWSTR Path,
    _In_reads_bytes_(Length) const BYTE* Data,
    _In_ ULONG Length)
{
    HANDLE File;
    ULONG Written = 0;
    NTSTATUS Status = IO_CreateWin32File(&File, Path, NULL, FILE_WRITE_DATA | SYNCHRONIZE, FILE_SHARE_READ,
                                         FILE_OVERWRITE_IF, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
    {
        return Err_NtStatusToHr(Status);
    }
    Status = IO_WriteFile(File, NULL, (PVOID)Data, Length, &Written);
    NtClose(File);
    return !NT_SUCCESS(Status) ? Err_NtStatusToHr(Status)
           : Written == Length ? S_OK
                               : HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
}

HRESULT
CuaObserve(
    _Inout_ CUA_COMMAND* Command,
    _In_ const CUA_INSPECT_OPTIONS* Options)
{
    CUA_OBSERVATION Observation = { 0 };
    CUA_OBSERVATION* Observed = &Observation;
    IJsonValueStatics* Factory = Command->Factory;
    IJsonObject *Result = NULL, *Geometry = NULL, *ImageError = NULL;
    IJsonValue* Value = NULL;
    INPUT_OBSERVATION Input = { 0 };
    PCWSTR Path = Options->OutFile;
    HWND UiaWindow;
    RECT UiaBounds = { 0 };
    LOGICAL GeometryKnown;
    HRESULT Hr, ImageStatus = S_OK;
    Observed->Foreground = NtUserGetForegroundWindow();
    UiaWindow = Observed->Foreground;
    if (Options->Handle != NULL)
    {
        Hr = CuaQueryWindow(Options->Handle, &Observed->Window);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        if (IsIconic(Observed->Window.Handle) != FALSE)
        {
            Hr = CuaFail(Command, E_FAIL, L"Activate the window before inspecting it.");
            goto Exit;
        }
        UiaWindow = Observed->Window.Handle;
    }
    Input.Gui.cbSize = sizeof(Input.Gui);
    Input.Thread = UiaWindow != NULL ? GetWindowThreadProcessId(UiaWindow, NULL) : 0;
    Input.Available = Input.Thread != 0 && NtUserGetGUIThreadInfo(Input.Thread, &Input.Gui);
    Input.CaretRoot = Input.Available && Input.Gui.hwndCaret != NULL
        ? NtUserGetAncestor(Input.Gui.hwndCaret, GA_ROOT) : NULL;
    Input.CaretMapped = Input.Available && CaretScreenBounds(&Input.Gui, &Input.CaretBounds);
    Input.CaretGeometryKnown =
        Input.CaretRoot != NULL && SUCCEEDED(GetWindowCaptureBounds(Input.CaretRoot, &Input.CaretRootBounds));
    GeometryKnown = UiaWindow != NULL && SUCCEEDED(GetWindowCaptureBounds(UiaWindow, &UiaBounds));
    if (*Path != UNICODE_NULL)
    {
        ImageStatus = Observed->Window.Handle != NULL
            ? CaptureWindow(Observed->Window.Handle, Options->Backend,
                            Options->UiaOptions.TimeoutMs, &Observed->Frame)
            : CaptureDesktop(Options->Backend, Options->UiaOptions.TimeoutMs, &Observed->Frame);
    }
    if (*Path == UNICODE_NULL || FAILED(ImageStatus))
    {
        Hr = MeasureFrame(Observed->Window.Handle, &Observed->Frame);
        if (FAILED(Hr))
        {
            CuaFail(Command, Hr, L"Target geometry is unavailable; inspect the desktop or activate the window.");
            goto Exit;
        }
    }
    if (Observed->Window.Handle != NULL)
    {
        UiaBounds = Observed->Frame.WindowBounds;
    }
    Hr = Data_JsonCreateObject(&Result);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (*Path != UNICODE_NULL)
    {
        if (SUCCEEDED(ImageStatus))
        {
            ImageStatus = SaveImage(Path, Observed->Frame.Png, Observed->Frame.PngLength);
        }
        if (FAILED(ImageStatus))
        {
            CuaFail(Command, ImageStatus, L"The requested PNG could not be captured or saved.");
            ImageError = BuildErrorOutput(ImageStatus, "The requested PNG could not be captured or saved.");
            if (ImageError == NULL)
            {
                Hr = E_OUTOFMEMORY;
                goto Exit;
            }
            Hr = ImageError->lpVtbl->QueryInterface(ImageError, &IID_IJsonValue, (PVOID*)&Value);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Hr = Data_JsonObjectSetValue(Result, L"image_error", Value);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Value->lpVtbl->Release(Value);
            Value = NULL;
        }
    }
    Hr = Data_JsonCreateObject(&Geometry);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetNumber(Factory, Geometry, L"width", Observed->Frame.Width);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetNumber(Factory, Geometry, L"height", Observed->Frame.Height);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Geometry->lpVtbl->QueryInterface(Geometry, &IID_IJsonValue, (PVOID*)&Value);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetValue(Result, L"geometry", Value);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Value->lpVtbl->Release(Value);
    Value = NULL;
    Hr = DescribeWindows(Command, Observed, Result);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = CuaSetWindowHandle(Factory, Result, L"foreground", Observed->Foreground);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (UiaWindow != NULL && IsWindow(UiaWindow) != FALSE)
    {
        CuaQueryWindow(UiaWindow, &Observed->UiaWindow);
    }
    Hr = DescribeElements(Command, Observed, &Options->UiaOptions, Options->Uia, GeometryKnown, &UiaBounds, Result);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = BuildInteraction(Command, Observed, &Input, &Value);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetValue(Result, L"interaction", Value);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Result->lpVtbl->QueryInterface(Result, &IID_IJsonValue, (PVOID*)&Command->Result);
    if (SUCCEEDED(Hr) && FAILED(ImageStatus))
    {
        Hr = ImageStatus;
    }
Exit:
    Mem_Free(Observed->Frame.Png);
    Mem_Free(Observed->Elements);
    if (Value != NULL)
    {
        Value->lpVtbl->Release(Value);
    }
    if (ImageError != NULL)
    {
        ImageError->lpVtbl->Release(ImageError);
    }
    if (Geometry != NULL)
    {
        Geometry->lpVtbl->Release(Geometry);
    }
    if (Result != NULL)
    {
        Result->lpVtbl->Release(Result);
    }
    return Hr;
}

HRESULT
CuaInspect(
    _Inout_ CUA_COMMAND* Command)
{
    const CUA_PARAMETERS* Parameters = &Command->Request->Parameters;
    CUA_INSPECT_OPTIONS Options = { 0 };
    CUA_WINDOW Window;
    PCWSTR Backend = CuaString(Parameters, CuaParamBackend, L"wgc");
    LONG Timeout, MaxNodes;
    HRESULT Hr;
    Hr = CuaOutputPath(Command, &Options.OutFile);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = CuaTargetWindow(Command, &Window);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (wcscmp(Backend, L"wgc") != 0 && wcscmp(Backend, L"gdi") != 0)
    {
        return CuaFail(Command, E_INVALIDARG, L"Unknown capture backend.");
    }
    Hr = CuaInteger(Command, CuaParamTimeout, WUA_UIA_DEFAULT_TIMEOUT_MS, 100, 10000, &Timeout);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = CuaInteger(Command, CuaParamMaxNodes, 256, 1, 1024, &MaxNodes);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Options.Handle = Window.Handle;
    Options.Backend = wcscmp(Backend, L"wgc") == 0 ? CaptureBackendWgc : CaptureBackendScreenGdi;
    Options.Uia = CuaBoolean(Parameters, CuaParamUia, TRUE);
    Options.UiaOptions.MaxNodes = MaxNodes;
    Options.UiaOptions.MaxDepth = 12;
    Options.UiaOptions.TimeoutMs = Timeout;
    Hr = CuaObserve(Command, &Options);
Exit:
    return Hr;
}
