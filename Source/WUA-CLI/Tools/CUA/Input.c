#include "pch.h"
#include "Server.h"

HRESULT
CuaMouseMove(
    _Inout_ CUA_COMMAND* Command,
    _In_ POINT Point,
    _Out_ INPUT* Input)
{
    POINT Origin;
    SIZE Size;
    RtlZeroMemory(Input, sizeof(*Input));
    UI_GetScreenPos(&Origin, &Size);
    if (Size.cx <= 0 || Size.cy <= 0)
    {
        return CuaFail(Command, E_FAIL, L"The virtual desktop is unavailable.");
    }
    Input->type = INPUT_MOUSE;
    // Pixel centers avoid truncating absolute movement into the preceding physical pixel.
    Input->mi.dx = (LONG)((((LONGLONG)Point.x - Origin.x) * 65536 + 32768) / Size.cx);
    Input->mi.dy = (LONG)((((LONGLONG)Point.y - Origin.y) * 65536 + 32768) / Size.cy);
    Input->mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | MOUSEEVENTF_MOVE;
    return S_OK;
}

static
HRESULT
CheckPosition(
    _Inout_ CUA_COMMAND* Command,
    _In_ const CUA_WINDOW* Window,
    _In_ const CAPTURED_FRAME* Frame,
    _In_ POINT Point)
{
    CAPTURED_FRAME Desktop;
    RECT Current;
    GUITHREADINFO Gui = { sizeof(Gui) };
    LOGICAL OnMonitor = FALSE;
    if (Window->Handle != NULL)
    {
        ULONGLONG Deadline = _Inline_GetTickCount64() + 1000;
        while (CuaHitTarget(Window->Handle, Point) == FALSE && _Inline_GetTickCount64() < Deadline)
        {
            PS_DelayExec(15);
        }
        if (CuaSameWindow(Window) == FALSE || IsWindowEnabled(Window->Handle) == FALSE)
        {
            return CuaFail(Command, E_FAIL, L"The target changed or became disabled; no input was sent.");
        }
    }
    HRESULT Hr = MeasureFrame(NULL, &Desktop);
    if (FAILED(Hr))
    {
        return Hr;
    }
    if (Frame->MonitorCount != Desktop.MonitorCount ||
        RtlEqualMemory(Frame->Monitors, Desktop.Monitors, Frame->MonitorCount * sizeof(RECT)) == FALSE)
    {
        return CuaFail(Command, E_FAIL, L"The display layout changed while preparing input; inspect again.");
    }
    for (ULONG Index = 0; Index < Desktop.MonitorCount; ++Index)
    {
        if (UI_PtInRect(&Desktop.Monitors[Index], &Point) != FALSE)
        {
            OnMonitor = TRUE;
            break;
        }
    }
    if (OnMonitor == FALSE)
    {
        return CuaFail(Command, E_INVALIDARG, L"The point is outside the physical displays.");
    }
    if (Window->Handle == NULL)
    {
        return S_OK;
    }
    Hr = GetWindowCaptureBounds(Window->Handle, &Current);
    if (FAILED(Hr))
    {
        return Hr;
    }
    if (EqualRect(&Current, &Frame->ScreenBounds) == FALSE || GetDpiForWindow(Window->Handle) != Frame->WindowDpi)
    {
        return CuaFail(Command, E_FAIL, L"Window geometry changed while preparing input; inspect again.");
    }
    if (NtUserGetGUIThreadInfo(0, &Gui) == FALSE)
    {
        return CuaFail(Command, HRESULT_FROM_WIN32(Err_GetLastError()),
                       L"Current mouse routing could not be verified.");
    }
    if (Gui.hwndCapture != NULL && NtUserGetAncestor(Gui.hwndCapture, GA_ROOT) != Window->Handle)
    {
        return CuaFail(Command, E_FAIL, L"Another window currently captures mouse input.");
    }
    if (CuaHitTarget(Window->Handle, Point) == FALSE)
    {
        return CuaFail(Command, E_FAIL, L"Another window receives input at this point; no input was sent.");
    }
    return S_OK;
}

static
HRESULT
ImagePoint(
    _Inout_ CUA_COMMAND* Command,
    _In_ CUA_PARAMETER X,
    _In_ CUA_PARAMETER Y,
    _In_ const CAPTURED_FRAME* Frame,
    _Out_ POINT* Point)
{
    const CUA_PARAMETERS* Parameters = &Command->Request->Parameters;
    LONG LocalX = CuaNumber(Parameters, X, -1), LocalY = CuaNumber(Parameters, Y, -1);
    if (LocalX < 0 || LocalY < 0 || (ULONG)LocalX >= Frame->Width || (ULONG)LocalY >= Frame->Height)
    {
        return CuaFail(Command, E_INVALIDARG, L"Supply X/Y within the target screenshot.");
    }
    Point->x = Frame->ScreenBounds.left + LocalX;
    Point->y = Frame->ScreenBounds.top + LocalY;
    return S_OK;
}

static
HRESULT
ParseRuntimeId(
    _Inout_ CUA_COMMAND* Command,
    _Out_writes_to_(WUA_UIA_MAX_RUNTIME_ID, *Count) LONG* RuntimeId,
    _Out_ PULONG Count)
{
    PCWSTR Part = CuaString(&Command->Request->Parameters, CuaParamRuntimeId, L"");
    *Count = 0;
    while (*Part != UNICODE_NULL && *Count < WUA_UIA_MAX_RUNTIME_ID)
    {
        WCHAR Number[12];
        LONGLONG Value;
        PCWSTR End = wcschr(Part, L',');
        SIZE_T Length = End != NULL ? (SIZE_T)(End - Part) : wcslen(Part);
        if (Length == 0 || Length >= ARRAYSIZE(Number) ||
            (Length == 1 && (*Part == L'-' || *Part == L'+')))
        {
            break;
        }
        RtlCopyMemory(Number, Part, Length * sizeof(WCHAR));
        Number[Length] = UNICODE_NULL;
        if (Str_DecToIntW(Number, &Value) == FALSE || Value < MINLONG32 || Value > MAXLONG32)
        {
            break;
        }
        RuntimeId[(*Count)++] = (LONG)Value;
        if (End == NULL)
        {
            return S_OK;
        }
        Part = End + 1;
    }
    return CuaFail(Command, E_INVALIDARG, L"RuntimeId must contain comma-separated signed 32-bit integers.");
}

static
HRESULT
MouseSequence(
    _Inout_ CUA_COMMAND* Command,
    _In_opt_ HWND Window,
    _In_ POINT Point,
    _In_ POINT End,
    _In_ DWORD Down,
    _In_ DWORD Up,
    _In_ DWORD Data,
    _In_ DWORD Mask,
    _In_ LOGICAL Horizontal,
    _In_ LONG Delta,
    _In_ LONG Duration)
{
    CUA_METHOD Method = Command->Request->Method;
    LOGICAL Sequence = Method == CuaMethodMove || Method == CuaMethodDown || Method == CuaMethodUp;
    const WORD Buttons[] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 };
    const WORD Modifiers[] = { VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN, VK_RWIN };
    INPUT Inputs[5] = { 0 }, Event = { 0 }, Release = { 0 };
    ULONG Count = 1;
    LOGICAL Dragging = FALSE;
    HRESULT Hr;
    for (ULONG Index = 0; Index < ARRAYSIZE(Buttons); ++Index)
    {
        if (GetAsyncKeyState(Buttons[Index]) < 0 && Method != CuaMethodUp &&
            (Sequence == FALSE || (Command->Server->HeldButtons & (1u << Index)) == 0))
        {
            return CuaFail(Command, E_FAIL, L"A mouse button is held outside this input sequence.");
        }
    }
    for (ULONG Index = 0; Index < ARRAYSIZE(Modifiers); ++Index)
    {
        if (GetAsyncKeyState(Modifiers[Index]) < 0)
        {
            return CuaFail(Command, E_FAIL, L"A modifier is held and would change this mouse action.");
        }
    }
    Hr = CuaInputDesktop(Command);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Window != NULL &&
        (Method == CuaMethodMove || Method == CuaMethodScroll || Method == CuaMethodDrag || Method == CuaMethodUp) &&
        NtUserGetForegroundWindow() != Window)
    {
        return CuaFail(Command, E_FAIL, L"Foreground changed while preparing input; no input was sent.");
    }
    Hr = CuaMouseMove(Command, Point, &Inputs[0]);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Event.type = INPUT_MOUSE;
    Event.mi.mouseData = Data;
    if (Method == CuaMethodScroll)
    {
        Event.mi.dwFlags = Horizontal != FALSE ? MOUSEEVENTF_HWHEEL : MOUSEEVENTF_WHEEL;
        Event.mi.mouseData = (DWORD)Delta;
        Inputs[Count++] = Event;
    } else if (Method == CuaMethodDown || Method == CuaMethodUp)
    {
        Event.mi.dwFlags = Method == CuaMethodDown ? Down : Up;
        Inputs[Count++] = Event;
    } else if (Method != CuaMethodMove)
    {
        Event.mi.dwFlags = Down;
        Inputs[Count++] = Event;
        if (Method == CuaMethodDrag)
        {
            Release.type = INPUT_MOUSE;
            Release.mi.dwFlags = Up;
            Release.mi.mouseData = Data;
            Dragging = TRUE;
            Hr = CuaSend(Command, Inputs, Count);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            for (LONG Step = 1; Step <= 20; ++Step)
            {
                PS_DelayExec(Duration / 20);
                Hr = CuaInputDesktop(Command);
                if (FAILED(Hr))
                {
                    goto Exit;
                }
                if (Window != NULL && NtUserGetForegroundWindow() != Window)
                {
                    Hr = CuaFail(Command, E_FAIL, L"Foreground changed during dragging; inspect before retrying.");
                    goto Exit;
                }
                POINT Next = { Point.x + (LONG)(((LONGLONG)End.x - Point.x) * Step / 20),
                               Point.y + (LONG)(((LONGLONG)End.y - Point.y) * Step / 20) };
                Hr = CuaMouseMove(Command, Next, &Inputs[0]);
                if (FAILED(Hr))
                {
                    goto Exit;
                }
                Hr = CuaSend(Command, Inputs, 1);
                if (FAILED(Hr))
                {
                    goto Exit;
                }
            }
            Hr = CuaSend(Command, &Release, 1);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            Dragging = FALSE;
            Count = 0;
        } else
        {
            Event.mi.dwFlags = Up;
            Inputs[Count++] = Event;
            if (Method == CuaMethodDoubleClick)
            {
                Event.mi.dwFlags = Down;
                Inputs[Count++] = Event;
                Event.mi.dwFlags = Up;
                Inputs[Count++] = Event;
            }
        }
    }
    if (Count != 0)
    {
        Hr = CuaSend(Command, Inputs, Count);
        if (FAILED(Hr))
        {
            goto Exit;
        }
    }
    if (Method == CuaMethodDown)
    {
        Command->Server->HeldButtons |= Mask;
    } else if (Method == CuaMethodUp)
    {
        Command->Server->HeldButtons &= ~Mask;
    }
Exit:
    if (Dragging != FALSE)
    {
        NtUserSendInput(1, &Release, sizeof(Release));
    }
    return Hr;
}

HRESULT
CuaAction(
    _Inout_ CUA_COMMAND* Command)
{
    CUA_METHOD Method = Command->Request->Method;
    const CUA_PARAMETERS* Parameters = &Command->Request->Parameters;
    CUA_WINDOW Window = { 0 };
    CAPTURED_FRAME Frame;
    LONG RuntimeId[WUA_UIA_MAX_RUNTIME_ID], Delta = 120, Duration = 300;
    ULONG RuntimeIdCount = 0;
    WORD Keys[CUA_MAX_KEYS] = { 0 };
    DWORD Down = MOUSEEVENTF_LEFTDOWN, Up = MOUSEEVENTF_LEFTUP, Data = 0, Mask = 1;
    LOGICAL Mouse = Method >= CuaMethodClick && Method <= CuaMethodUp;
    LOGICAL Semantic = Method >= CuaMethodInvoke && Method <= CuaMethodSelect;
    LOGICAL Resolved = FALSE, Horizontal = FALSE;
    PCWSTR Path, Text = CuaString(Parameters, CuaParamText, L"");
    PCWSTR TextMethod = CuaString(Parameters, CuaParamTextMethod, L"message");
    HRESULT Hr;
    Hr = CuaOutputPath(Command, &Path);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = CuaTargetWindow(Command, &Window);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Mouse != FALSE)
    {
        PCWSTR Button = CuaString(Parameters, CuaParamButton, L"left");
        if (_wcsicmp(Button, L"right") == 0)
        {
            Down = MOUSEEVENTF_RIGHTDOWN;
            Up = MOUSEEVENTF_RIGHTUP;
            Mask = 2;
        } else if (_wcsicmp(Button, L"middle") == 0)
        {
            Down = MOUSEEVENTF_MIDDLEDOWN;
            Up = MOUSEEVENTF_MIDDLEUP;
            Mask = 4;
        } else if (_wcsicmp(Button, L"x1") == 0 || _wcsicmp(Button, L"x2") == 0)
        {
            Down = MOUSEEVENTF_XDOWN;
            Up = MOUSEEVENTF_XUP;
            Data = _wcsicmp(Button, L"x1") == 0 ? XBUTTON1 : XBUTTON2;
            Mask = Data == XBUTTON1 ? 8 : 16;
        } else if (_wcsicmp(Button, L"left") != 0)
        {
            return CuaFail(Command, E_INVALIDARG, L"Button must be Left, Right, Middle, X1 or X2.");
        }
        if (Method == CuaMethodScroll)
        {
            PCWSTR Axis = CuaString(Parameters, CuaParamAxis, L"vertical");
            if (_wcsicmp(Axis, L"vertical") != 0 && _wcsicmp(Axis, L"horizontal") != 0)
            {
                return CuaFail(Command, E_INVALIDARG, L"Axis must be Vertical or Horizontal.");
            }
            Hr = CuaInteger(Command, CuaParamDelta, 120, -12000, 12000, &Delta);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            if (Delta == 0)
            {
                return CuaFail(Command, E_INVALIDARG, L"Wheel Delta must be nonzero.");
            }
            Horizontal = _wcsicmp(Axis, L"horizontal") == 0;
        }
        if (CuaHas(Parameters, CuaParamX) == FALSE || CuaHas(Parameters, CuaParamY) == FALSE)
        {
            return CuaFail(Command, E_INVALIDARG, L"Mouse requires X and Y.");
        }
        if (Method == CuaMethodDrag)
        {
            Hr = CuaInteger(Command, CuaParamDuration, 300, 50, 3000, &Duration);
            if (FAILED(Hr))
            {
                goto Exit;
            }
            if (CuaHas(Parameters, CuaParamToX) == FALSE || CuaHas(Parameters, CuaParamToY) == FALSE)
            {
                return CuaFail(Command, E_INVALIDARG, L"Drag requires ToX and ToY.");
            }
        }
    }
    if (Method == CuaMethodType || Method == CuaMethodSetValue)
    {
        if (_wcsicmp(TextMethod, L"message") == 0)
        {
            TextMethod = L"message";
        } else if (_wcsicmp(TextMethod, L"paste") == 0)
        {
            TextMethod = L"paste";
        }
        Hr = CuaValidateText(Command, Text, TextMethod);
        if (FAILED(Hr))
        {
            goto Exit;
        }
    }
    if (Method == CuaMethodKeys)
    {
        Hr = CuaParseKeys(Command, Keys);
        if (FAILED(Hr))
        {
            goto Exit;
        }
    }
    if (Semantic != FALSE)
    {
        if (Window.Handle == NULL)
        {
            return CuaFail(Command, E_INVALIDARG, L"Element requires a window Handle.");
        }
        Hr = ParseRuntimeId(Command, RuntimeId, &RuntimeIdCount);
        if (FAILED(Hr))
        {
            goto Exit;
        }
    }
    Hr = CuaInputDesktop(Command);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Resolved = TRUE;
    if (Semantic != FALSE)
    {
        UIA_ACTION Operation = Method == CuaMethodInvoke     ? UiaActionInvoke
                               : Method == CuaMethodSetValue ? UiaActionSetValue
                               : Method == CuaMethodToggle   ? UiaActionToggle
                                                             : UiaActionSelect;
        Hr = UiaAct(Command->Server->Uia, Window.Handle, RuntimeId, RuntimeIdCount, Operation, Text,
            WUA_UIA_DEFAULT_TIMEOUT_MS);
        if (FAILED(Hr))
        {
            CuaFail(Command, Hr, L"The UIA action failed or timed out; inspect before retrying.");
        }
        goto Exit;
    }
    if (Window.Handle != NULL)
    {
        LOGICAL RequireForeground = Method != CuaMethodClick && Method != CuaMethodDoubleClick &&
                                    Method != CuaMethodDown;
        Hr = CuaPrepareWindow(Command, &Window, RequireForeground);
        if (FAILED(Hr))
        {
            goto Exit;
        }
    }
    if (Mouse != FALSE)
    {
        POINT Point, End;
        Hr = MeasureFrame(Window.Handle, &Frame);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        Hr = ImagePoint(Command, CuaParamX, CuaParamY, &Frame, &Point);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        End = Point;
        if (Method == CuaMethodDrag)
        {
            Hr = ImagePoint(Command, CuaParamToX, CuaParamToY, &Frame, &End);
            if (FAILED(Hr))
            {
                goto Exit;
            }
        }
        Hr = CheckPosition(Command, &Window, &Frame, Point);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        if (Method == CuaMethodDrag)
        {
            Hr = CheckPosition(Command, &Window, &Frame, End);
            if (FAILED(Hr))
            {
                goto Exit;
            }
        }
        Hr = MouseSequence(Command, Window.Handle, Point, End, Down, Up, Data, Mask, Horizontal, Delta, Duration);
    } else
    {
        HWND Expected = Window.Handle != NULL ? Window.Handle : NtUserGetForegroundWindow();
        GUITHREADINFO Gui = { sizeof(Gui) };
        const WORD Modifiers[] = { VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN, VK_RWIN };
        if (Expected == NULL || NtUserGetGUIThreadInfo(0, &Gui) == FALSE || Gui.hwndActive != Expected ||
            Gui.hwndFocus == NULL || NtUserGetForegroundWindow() != Expected ||
            NtUserGetAncestor(Gui.hwndFocus, GA_ROOT) != Expected)
        {
            Hr = CuaFail(Command, E_FAIL, L"The current focus is outside the target; inspect or click the control.");
            goto Exit;
        }
        for (ULONG Index = 0; Index < ARRAYSIZE(Modifiers); ++Index)
        {
            if (GetAsyncKeyState(Modifiers[Index]) < 0)
            {
                Hr = CuaFail(Command, E_FAIL, L"A modifier is already held; release it before sending keys.");
                goto Exit;
            }
        }
        if (Method == CuaMethodKeys)
        {
            Hr = CuaPressKeys(Command, Keys, Parameters->KeyCount);
        } else
        {
            Hr = CuaTypeText(Command, Text, TextMethod, Expected, Gui.hwndFocus);
        }
    }
Exit:
    if (Resolved != FALSE)
    {
        if (FAILED(Hr) && SUCCEEDED(Command->Error.Status))
        {
            CuaFail(Command, Hr, L"A Windows API failed while performing input; inspect before retrying.");
        }
        HWND Handle = Window.Handle;
        if (Handle != NULL && (CuaSameWindow(&Window) == FALSE || IsIconic(Handle) || IsWindowEnabled(Handle) == FALSE))
        {
            Handle = NULL;
        }
        CuaAfterAction(Command, Handle);
    }
    return Hr;
}
