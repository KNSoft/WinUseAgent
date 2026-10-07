#include "pch.h"
#include "TestHost.h"
#include "../Tools/CUA/Server.h"

#define EditId 101
#define CounterId 102
#define CheckId 103
#define PasswordId 104
#define ModalId 105
#define HangId 106
#define SelectionId 107

typedef struct _FIXTURE
{
    PCWSTR Report;
    PWSTR Temporary, Text;
    IJsonValueStatics* Factory;
    HWND Window, Edit, Counter, Check, Scroll, Selection;
    LONG Clicks, ScrollClicks, X1Clicks, X2Clicks, Vertical, Horizontal;
    LONG DoubleClicks, RightClicks, MiddleClicks;
    LOGICAL HangUia, MouseDown, UnicodeMessages, DelayCharacter, ReplaceClipboard, ClipboardChanged;
    WNDPROC EditProc;
    UINT CharMessages, UnicharMessages;
} FIXTURE;

static
HRESULT
SetBounds(
    _In_ FIXTURE* State,
    _In_ IJsonObject* Object,
    _In_ PCWSTR Name,
    _In_ HWND Window)
{
    RECT Bounds;
    if (!GetWindowRect(Window, &Bounds))
    {
        return HRESULT_FROM_WIN32(Err_GetLastError());
    }
    return CuaSetRectangle(State->Factory, Object, Name, &Bounds);
}

static
VOID
WriteReport(
    _Inout_ FIXTURE* State)
{
    IJsonObject *Report = NULL, *Controls = NULL;
    IJsonValue* Value = NULL;
    IJsonValueStatics* Factory = State->Factory;
    HANDLE File = NULL;
    PSTR Bytes = NULL;
    ULONG Length;
    ULONG Written;
    NTSTATUS Status;
    UNICODE_STRING Path;
    PFILE_RENAME_INFORMATION Rename = NULL;
    IO_STATUS_BLOCK IoStatus;
    WCHAR Handle[32];
    HRESULT Hr;
    if (!State->Edit || !State->Scroll)
    {
        return;
    }
    State->Text[0] = 0;
    GetWindowTextW(State->Edit, State->Text, 16385);
    Hr = Data_JsonCreateObject(&Report);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetNumber(Factory, Report, L"pid", HandleToULong(NtCurrentProcessId()));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Str_PrintfExW(Handle, ARRAYSIZE(Handle), L"%p", State->Window);
    Hr = Data_JsonObjectSetString(Factory, Report, L"hwnd", Handle, (ULONG)wcslen(Handle));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetNumber(Factory, Report, L"clicks", State->Clicks);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetNumber(Factory, Report, L"scroll_clicks", State->ScrollClicks);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetNumber(Factory, Report, L"x1_clicks", State->X1Clicks);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetNumber(Factory, Report, L"x2_clicks", State->X2Clicks);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetNumber(Factory, Report, L"double_clicks", State->DoubleClicks);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetNumber(Factory, Report, L"right_clicks", State->RightClicks);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetNumber(Factory, Report, L"middle_clicks", State->MiddleClicks);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetNumber(Factory, Report, L"selection",
                                (DOUBLE)SendMessageW(State->Selection, LB_GETCURSEL, 0, 0));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetNumber(Factory, Report, L"wheel_vertical", State->Vertical);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetNumber(Factory, Report, L"wheel_horizontal", State->Horizontal);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetString(Factory, Report, L"text", State->Text, (ULONG)wcslen(State->Text));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetBoolean(Factory, Report, L"checked",
                                        SendMessageW(State->Check, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetBoolean(Factory, Report, L"uia_hang", State->HangUia);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetBoolean(Factory, Report, L"mouse_down", State->MouseDown);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetNumber(Factory, Report, L"char_messages", State->CharMessages);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetNumber(Factory, Report, L"unichar_messages", State->UnicharMessages);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetBoolean(Factory, Report, L"unicode_messages", State->UnicodeMessages);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetBoolean(Factory, Report, L"clipboard_changed", State->ClipboardChanged);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = SetBounds(State, Report, L"window", State->Window);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonCreateObject(&Controls);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = SetBounds(State, Controls, L"edit", State->Edit);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = SetBounds(State, Controls, L"button", State->Counter);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = SetBounds(State, Controls, L"checkbox", State->Check);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = SetBounds(State, Controls, L"scroll", State->Scroll);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Controls->lpVtbl->QueryInterface(Controls, &IID_IJsonValue, (PVOID*)&Value);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonObjectSetValue(Report, L"controls", Value);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Value->lpVtbl->Release(Value);
    Value = NULL;
    Hr = Report->lpVtbl->QueryInterface(Report, &IID_IJsonValue, (PVOID*)&Value);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Data_JsonStringifyUtf8(Value, &Bytes, &Length);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Err_NtStatusToHr(IO_CreateWin32File(&File, State->Temporary, NULL, FILE_WRITE_DATA | DELETE | SYNCHRONIZE,
                                                  FILE_SHARE_READ, FILE_OVERWRITE_IF,
                                                  FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Err_NtStatusToHr(IO_WriteFile(File, NULL, Bytes, Length, &Written));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (Written != Length)
    {
        goto Exit;
    }
    Hr = Err_NtStatusToHr(NT_Win32PathToNtPath(State->Report, NULL, &Path));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Length = FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) + Path.Length;
    Rename = (PFILE_RENAME_INFORMATION)Mem_Alloc(Length);
    if (Rename != NULL)
    {
        RtlZeroMemory(Rename, Length);
        Rename->ReplaceIfExists = TRUE;
        Rename->FileNameLength = Path.Length;
        RtlCopyMemory(Rename->FileName, Path.Buffer, Path.Length);
        Status = NtSetInformationFile(File, &IoStatus, Rename, Length, FileRenameInformation);
        UNREFERENCED_PARAMETER(Status);
    }
    NT_FreeNtPath(&Path);
Exit:
    if (File != NULL)
    {
        NtClose(File);
    }
    Mem_Free(Rename);
    Mem_Free(Bytes);
    if (Value != NULL)
    {
        Value->lpVtbl->Release(Value);
    }
    if (Controls != NULL)
    {
        Controls->lpVtbl->Release(Controls);
    }
    if (Report != NULL)
    {
        Report->lpVtbl->Release(Report);
    }
}

static
LRESULT
CALLBACK
EditProc(
    _In_ HWND Window,
    _In_ UINT Message,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    FIXTURE* State = (FIXTURE*)(GetWindowLongPtrW(Window, GWLP_USERDATA));
    if (State->ReplaceClipboard && (Message == WM_PASTE || (Message == WM_CHAR && WParam == 0x16)))
    {
        LRESULT Result = CallWindowProcW(State->EditProc, Window, Message, WParam, LParam);
        const WCHAR Text[] = L"WUA newer clipboard content";
        HGLOBAL Memory = GlobalAlloc(GMEM_MOVEABLE, sizeof(Text));
        PVOID Buffer = Memory ? GlobalLock(Memory) : NULL;
        State->ReplaceClipboard = FALSE;
        if (Buffer != FALSE)
        {
            RtlCopyMemory(Buffer, Text, sizeof(Text));
            GlobalUnlock(Memory);
            if (OpenClipboard(Window))
            {
                if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, Memory))
                {
                    State->ClipboardChanged = TRUE;
                    Memory = NULL;
                }
                CloseClipboard();
            }
        }
        if (Memory != FALSE)
        {
            GlobalFree(Memory);
        }
        WriteReport(State);
        return Result;
    }
    if (Message == WM_UNICHAR && WParam == UNICODE_NOCHAR)
    {
        return State->UnicodeMessages;
    }
    if (Message == WM_CHAR || (Message == WM_UNICHAR && State->UnicodeMessages))
    {
        if (State->DelayCharacter != FALSE)
        {
            State->DelayCharacter = FALSE;
            PS_DelayExec(1000);
        }
        LRESULT Result;
        if (Message == WM_CHAR)
        {
            ++State->CharMessages;
            Result = CallWindowProcW(State->EditProc, Window, Message, WParam, LParam);
        } else
        {
            ++State->UnicharMessages;
            if (WParam >= 0x10000)
            {
                WParam -= 0x10000;
                CallWindowProcW(State->EditProc, Window, WM_CHAR, 0xD800 + (WParam >> 10), LParam);
                WParam = 0xDC00 + (WParam & 0x3FF);
            }
            Result = CallWindowProcW(State->EditProc, Window, WM_CHAR, WParam, LParam);
        }
        WriteReport(State);
        return Result;
    }
    return CallWindowProcW(State->EditProc, Window, Message, WParam, LParam);
}

static
LRESULT
CALLBACK
ScrollProc(
    _In_ HWND Window,
    _In_ UINT Message,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    FIXTURE* State = (FIXTURE*)(GetWindowLongPtrW(Window, GWLP_USERDATA));
    if (Message == WM_NCCREATE)
    {
        State = (FIXTURE*)(((CREATESTRUCTW*)LParam)->lpCreateParams);
        SetWindowLongPtrW(Window, GWLP_USERDATA, (LONG_PTR)(State));
    }
    if (State != NULL)
    {
        if (Message == WM_MOUSEWHEEL || Message == WM_MOUSEHWHEEL)
        {
            int Delta = GET_WHEEL_DELTA_WPARAM(WParam);
            if (Message == WM_MOUSEWHEEL)
            {
                State->Vertical += Delta;
            } else
            {
                State->Horizontal += Delta;
            }
            WriteReport(State);
            InvalidateRect(Window, NULL, FALSE);
            return 0;
        }
        if (Message == WM_XBUTTONUP)
        {
            if (GET_XBUTTON_WPARAM(WParam) == XBUTTON1)
            {
                State->X1Clicks++;
            } else
            {
                State->X2Clicks++;
            }
            WriteReport(State);
            return TRUE;
        }
        if (Message == WM_RBUTTONUP || Message == WM_MBUTTONUP)
        {
            if (Message == WM_RBUTTONUP)
            {
                ++State->RightClicks;
            } else
            {
                ++State->MiddleClicks;
            }
            WriteReport(State);
            return 0;
        }
        if (Message == WM_LBUTTONDOWN || Message == WM_LBUTTONDBLCLK)
        {
            if (Message == WM_LBUTTONDBLCLK)
            {
                ++State->DoubleClicks;
            }
            SetFocus(Window);
            SetCapture(Window);
            State->MouseDown = TRUE;
            WriteReport(State);
            return 0;
        }
        if (Message == WM_LBUTTONUP)
        {
            ReleaseCapture();
            State->MouseDown = FALSE;
            State->ScrollClicks++;
            WriteReport(State);
            return 0;
        }
        if (Message == WM_PAINT)
        {
            PAINTSTRUCT Paint;
            HDC Dc = BeginPaint(Window, &Paint);
            RECT Rect;
            GetClientRect(Window, &Rect);
            FillRect(Dc, &Rect, (HBRUSH)(COLOR_WINDOW + 1));
            SetBkMode(Dc, TRANSPARENT);
            TextOutW(Dc, 16, 16, L"Scroll / drag test area", 23);
            WCHAR Counts[100];
            Str_PrintfW(Counts, L"Vertical: %d   Horizontal: %d", State->Vertical, State->Horizontal);
            TextOutW(Dc, 16, 44, Counts, (int)(wcslen(Counts)));
            HBRUSH Brush = CreateSolidBrush(RGB(20, 110, 220));
            RECT Marker = { 16, 90, 48, 122 };
            FillRect(Dc, &Marker, Brush);
            DeleteObject(Brush);
            EndPaint(Window, &Paint);
            return 0;
        }
    }
    return DefWindowProcW(Window, Message, WParam, LParam);
}

static
LRESULT
CALLBACK
WindowProc(
    _In_ HWND Window,
    _In_ UINT Message,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    FIXTURE* State = (FIXTURE*)(GetWindowLongPtrW(Window, GWLP_USERDATA));
    if (Message == WM_NCCREATE)
    {
        State = (FIXTURE*)(((CREATESTRUCTW*)LParam)->lpCreateParams);
        State->Window = Window;
        SetWindowLongPtrW(Window, GWLP_USERDATA, (LONG_PTR)(State));
    }
    if (State == NULL)
    {
        return DefWindowProcW(Window, Message, WParam, LParam);
    }
    if (Message == WM_CREATE)
    {
        HINSTANCE Instance = GetModuleHandleW(NULL);
        CreateWindowW(L"STATIC", L"Editable text", WS_CHILD | WS_VISIBLE, 24, 16, 460, 20, Window, NULL, Instance,
                      NULL);
        State->Edit =
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"initial", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                            24, 40, 460, 34, Window, (HMENU)((ULONG_PTR)(EditId)), Instance, NULL);
        SetWindowLongPtrW(State->Edit, GWLP_USERDATA, (LONG_PTR)(State));
        State->EditProc = (WNDPROC)(SetWindowLongPtrW(State->Edit, GWLP_WNDPROC, (LONG_PTR)(EditProc)));
        State->Counter = CreateWindowW(L"BUTTON", L"Increment counter", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 24, 92, 190,
                                       38, Window, (HMENU)((ULONG_PTR)(CounterId)), Instance, NULL);
        State->Check = CreateWindowW(L"BUTTON", L"Test checkbox", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                     244, 92, 200, 38, Window, (HMENU)((ULONG_PTR)(CheckId)), Instance, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"fixture-password-secret",
                        WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL, 24, 150, 460, 32, Window,
                        (HMENU)((ULONG_PTR)(PasswordId)), Instance, NULL);
        State->Scroll = CreateWindowExW(WS_EX_CLIENTEDGE, L"WuaCuaFixtureScroll", L"Scroll area", WS_CHILD | WS_VISIBLE,
                                        24, 212, 460, 154, Window, NULL, Instance, State);
        CreateWindowW(L"BUTTON", L"Open modal dialog", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 24, 384, 190, 32, Window,
                      (HMENU)((ULONG_PTR)(ModalId)), Instance, NULL);
        CreateWindowW(L"BUTTON", L"Hang UIA provider", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 244, 384, 200, 32, Window,
                      (HMENU)((ULONG_PTR)(HangId)), Instance, NULL);
        State->Selection = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"Test selection",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY, 24, 426, 460, 50,
            Window, (HMENU)((ULONG_PTR)SelectionId), Instance, NULL);
        SendMessageW(State->Selection, LB_ADDSTRING, 0, (LPARAM)L"Choice one");
        SendMessageW(State->Selection, LB_ADDSTRING, 0, (LPARAM)L"Choice two");
        SetTimer(Window, 1, 100, NULL);
        WriteReport(State);
        return 0;
    }
    if (Message == WM_APP + 11)
    {
        State->UnicodeMessages = WParam == 1;
        State->DelayCharacter = WParam == 2;
        State->ReplaceClipboard = WParam == 4;
        State->ClipboardChanged = FALSE;
        WriteReport(State);
        return TRUE;
    }
    if (Message == WM_GETOBJECT && State->HangUia)
    {
        PS_DelayExec(10000);
    }
    if (Message == WM_COMMAND)
    {
        if (LOWORD(WParam) == CounterId && HIWORD(WParam) == BN_CLICKED)
        {
            State->Clicks++;
        }
        if (LOWORD(WParam) == HangId && HIWORD(WParam) == BN_CLICKED)
        {
            State->HangUia = !State->HangUia;
        }
        WriteReport(State);
        if (LOWORD(WParam) == ModalId && HIWORD(WParam) == BN_CLICKED)
        {
            MessageBoxW(Window, L"Close this test dialog to continue.", L"WUA CUA Test Modal", MB_OK);
        }
        return 0;
    }
    if (Message == WM_TIMER || Message == WM_MOVE || Message == WM_SIZE)
    {
        WriteReport(State);
        return 0;
    }
    if (Message == WM_DESTROY)
    {
        KillTimer(Window, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(Window, Message, WParam, LParam);
}
int
TestHostMain(
    _In_ int Count,
    _In_reads_(Count) wchar_t** Arguments)
{
    FIXTURE State = { 0 };
    WNDCLASSW Class = { 0 };
    int Result = 1;
    HRESULT Hr;
    for (int Index = 0; Index < Count; ++Index)
    {
        if (_wcsnicmp(Arguments[Index], L"Report=", 7) || State.Report)
        {
            return ERROR_INVALID_PARAMETER;
        }
        State.Report = Arguments[Index] + 7;
    }
    if (!State.Report || !*State.Report)
    {
        return ERROR_INVALID_PARAMETER;
    }
    Hr = RoInitialize(RO_INIT_SINGLETHREADED);
    if (FAILED(Hr))
    {
        return 1;
    }
    Hr = Data_JsonGetValueFactory(&State.Factory);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    SIZE_T Length = wcslen(State.Report) + 5;
    State.Temporary = (PWSTR)Mem_Alloc(Length * sizeof(WCHAR));
    if (State.Temporary == FALSE)
    {
        goto Exit;
    }
    State.Text = (PWSTR)Mem_Alloc(16385 * sizeof(WCHAR));
    if (State.Text == FALSE)
    {
        goto Exit;
    }
    Str_PrintfExW(State.Temporary, Length, L"%ls.new", State.Report);
    Class.hInstance = GetModuleHandleW(NULL);
    Class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    Class.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    Class.style = CS_DBLCLKS;
    Class.lpfnWndProc = ScrollProc;
    Class.lpszClassName = L"WuaCuaFixtureScroll";
    RegisterClassW(&Class);
    Class.lpfnWndProc = WindowProc;
    Class.style = 0;
    Class.lpszClassName = L"WuaCuaTestFixture";
    RegisterClassW(&Class);
    HWND Window = CreateWindowW(Class.lpszClassName, L"WUA CUA Test Fixture", WS_OVERLAPPEDWINDOW, 160, 140, 540, 550,
                                NULL, NULL, Class.hInstance, &State);
    if (Window != NULL)
    {
        SetWindowPos(Window, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        MSG Message;
        while (GetMessageW(&Message, NULL, 0, 0) > 0)
        {
            if (State.Edit && Message.hwnd == State.Edit && Message.message == WM_KEYDOWN && Message.wParam == L'A' &&
                GetKeyState(VK_CONTROL) < 0)
            {
                SendMessageW(State.Edit, EM_SETSEL, 0, -1);
                continue;
            }
            TranslateMessage(&Message);
            DispatchMessageW(&Message);
        }
        Result = 0;
    }
Exit:
    Mem_Free(State.Temporary);
    Mem_Free(State.Text);
    if (State.Factory != NULL)
    {
        State.Factory->lpVtbl->Release(State.Factory);
    }
    RoUninitialize();
    return Result;
}
