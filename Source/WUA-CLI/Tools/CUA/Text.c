#include "pch.h"
#include "Server.h"

typedef struct _TEXT_INPUT
{
    CUA_COMMAND* Command;
    PCWSTR Text;
    HWND Expected, Focus;
    ULONG Length;
    HRESULT Status, ClipboardStatus;
} TEXT_INPUT;

HRESULT
CuaValidateText(
    _Inout_ CUA_COMMAND* Command,
    _In_ PCWSTR Text,
    _In_ PCWSTR Method)
{
    LOGICAL SetValue = Command->Request->Method == CuaMethodSetValue;
    SIZE_T Length = wcslen(Text), Limit = SetValue ? 16384 : 32768;
    if (SetValue == FALSE && wcscmp(Method, L"paste") != 0 && wcscmp(Method, L"message") != 0)
    {
        return CuaFail(Command, E_INVALIDARG, L"Method must be Message or Paste.");
    }
    if (CuaHas(&Command->Request->Parameters, CuaParamText) == FALSE || Length > Limit ||
        (SetValue == FALSE && Length == 0))
    {
        return CuaFail(Command, E_INVALIDARG,
                       L"Supply Text within the limit: Text 32768, SetValue 16384 UTF-16 units.");
    }
    for (SIZE_T Index = 0; Index < Length; ++Index)
    {
        if (IS_HIGH_SURROGATE(Text[Index]) != FALSE)
        {
            if (++Index < Length && IS_LOW_SURROGATE(Text[Index]) != FALSE)
            {
                continue;
            }
        } else if (IS_LOW_SURROGATE(Text[Index]) == FALSE)
        {
            continue;
        }
        return CuaFail(Command, E_INVALIDARG, L"Text contains an incomplete Unicode character.");
    }
    return S_OK;
}

static
HRESULT
RetainClipboard(
    _Outptr_result_maybenull_ IDataObject** Previous)
{
    IEnumFORMATETC* Formats = NULL;
    FORMATETC Format = { 0 };
    STGMEDIUM Medium = { 0 };
    *Previous = NULL;
    HRESULT Hr = OleGetClipboard(Previous);
    if (FAILED(Hr) || *Previous == NULL)
    {
        return Hr;
    }
    Hr = (*Previous)->lpVtbl->EnumFormatEtc(*Previous, DATADIR_GET, &Formats);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = Formats->lpVtbl->Next(Formats, 1, &Format, NULL);
    if (Hr == S_FALSE)
    {
        (*Previous)->lpVtbl->Release(*Previous);
        *Previous = NULL;
        Hr = S_OK;
    } else if (SUCCEEDED(Hr))
    {
        // Bind OLE's lazy wrapper to the original source before replacement; otherwise restoration can self-reference.
        Hr = (*Previous)->lpVtbl->GetData(*Previous, &Format, &Medium);
        if (SUCCEEDED(Hr))
        {
            ReleaseStgMedium(&Medium);
        }
    }
Exit:
    CoTaskMemFree(Format.ptd);
    if (Formats != NULL)
    {
        Formats->lpVtbl->Release(Formats);
    }
    return Hr;
}

static
HRESULT
SetClipboardText(
    _In_ PCWSTR Text,
    _In_ ULONG Length,
    _Out_ PLOGICAL Replaced)
{
    SIZE_T Bytes = ((SIZE_T)Length + 1) * sizeof(WCHAR);
    HGLOBAL Memory = GlobalAlloc(GMEM_MOVEABLE, Bytes);
    HRESULT Hr;
    *Replaced = FALSE;
    if (Memory == NULL)
    {
        return E_OUTOFMEMORY;
    }
    PVOID Buffer = GlobalLock(Memory);
    if (Buffer == NULL)
    {
        Hr = HRESULT_FROM_WIN32(Err_GetLastError());
        goto Failure;
    }
    RtlCopyMemory(Buffer, Text, Bytes);
    GlobalUnlock(Memory);
    if (EmptyClipboard() == FALSE)
    {
        Hr = HRESULT_FROM_WIN32(Err_GetLastError());
        goto Failure;
    }
    *Replaced = TRUE;
    if (SetClipboardData(CF_UNICODETEXT, Memory) == NULL)
    {
        Hr = HRESULT_FROM_WIN32(Err_GetLastError());
        goto Failure;
    }
    return S_OK;
Failure:
    GlobalFree(Memory);
    return Hr;
}

static
HRESULT
CheckTextFocus(
    _Inout_ TEXT_INPUT* Input)
{
    GUITHREADINFO Gui = { sizeof(Gui) };
    if (Input->Focus == NULL || NtUserGetForegroundWindow() != Input->Expected ||
        NtUserGetGUIThreadInfo(0, &Gui) == FALSE ||
        Gui.hwndActive != Input->Expected || Gui.hwndFocus != Input->Focus ||
        NtUserGetAncestor(Input->Focus, GA_ROOT) != Input->Expected)
    {
        return CuaFail(Input->Command, E_FAIL,
                       L"Focus changed before text input completed. Inspect before sending more text.");
    }
    if ((Gui.flags & (GUI_INMENUMODE | GUI_SYSTEMMENUMODE | GUI_POPUPMENUMODE | GUI_INMOVESIZE)) != 0)
    {
        return CuaFail(Input->Command, E_FAIL,
                       L"Finish or cancel the menu or move/size operation, then Inspect before sending text.");
    }
    return CuaInputDesktop(Input->Command);
}

static
_Function_class_(USER_THREAD_START_ROUTINE)
NTSTATUS
NTAPI
PasteThread(
    _In_ PVOID Context)
{
    TEXT_INPUT* Input = (TEXT_INPUT*)Context;
    IDataObject* Previous = NULL;
    HWND Owner = NULL;
    LOGICAL Initialized = FALSE, Replaced = FALSE, Opened = FALSE;
    DWORD Sequence;
    HRESULT Hr = OleInitialize(NULL);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Initialized = TRUE;
    Owner = CreateWindowExW(0, L"STATIC", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
    if (Owner == NULL)
    {
        Hr = HRESULT_FROM_WIN32(Err_GetLastError());
        goto Exit;
    }
    Sequence = GetClipboardSequenceNumber();
    Hr = RetainClipboard(&Previous);
    if (FAILED(Hr))
    {
        Hr = CuaFail(Input->Command, Hr,
                     L"The original OLE clipboard object could not be retained. Inspect before trying Message.");
        goto Exit;
    }
    Hr = CheckTextFocus(Input);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    if (OpenClipboard(Owner) == FALSE)
    {
        Hr = HRESULT_FROM_WIN32(Err_GetLastError());
        goto Exit;
    }
    Opened = TRUE;
    if (Sequence != GetClipboardSequenceNumber())
    {
        Hr = CuaFail(Input->Command, E_FAIL,
                     L"The clipboard changed before Paste started. Inspect before trying again.");
        goto Exit;
    }
    Hr = SetClipboardText(Input->Text, Input->Length, &Replaced);
    CloseClipboard();
    Opened = FALSE;
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Hr = CheckTextFocus(Input);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    const WORD Keys[] = { VK_CONTROL, L'V' };
    Hr = CuaPressKeys(Input->Command, Keys, ARRAYSIZE(Keys));
    if (FAILED(Hr))
    {
        goto Exit;
    }
    // Clipboard ownership changes send messages to this STA; sleeping would block the new owner.
    ULONGLONG Deadline = _Inline_GetTickCount64() + 1000;
    while (GetClipboardOwner() == Owner && _Inline_GetTickCount64() < Deadline)
    {
        MSG Message;
        while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Message);
            DispatchMessageW(&Message);
        }
        ULONGLONG Now = _Inline_GetTickCount64();
        if (Now >= Deadline)
        {
            break;
        }
        MsgWaitForMultipleObjectsEx(0, NULL, (DWORD)(Deadline - Now), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
Exit:
    if (Opened != FALSE)
    {
        CloseClipboard();
    }
    if (Replaced != FALSE && GetClipboardOwner() == Owner)
    {
        // Retaining IDataObject does not guarantee a materialized copy of every clipboard format.
        HRESULT Restored = OleSetClipboard(Previous);
        if (SUCCEEDED(Restored) && Previous != NULL)
        {
            Restored = OleFlushClipboard();
        }
        Input->ClipboardStatus = Restored;
    }
    if (Previous != NULL)
    {
        Previous->lpVtbl->Release(Previous);
    }
    if (Owner != NULL)
    {
        DestroyWindow(Owner);
    }
    if (Initialized != FALSE)
    {
        OleUninitialize();
    }
    Input->Status = Hr;
    return 0;
}

static
HRESULT
MessageText(
    _Inout_ TEXT_INPUT* Input)
{
    const UINT Flags = SMTO_ABORTIFHUNG | SMTO_BLOCK | SMTO_ERRORONEXIT;
    DWORD_PTR Supported = 0;
    W32ERROR Error;
    HRESULT Hr;
    Hr = CheckTextFocus(Input);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Error = UI_SendMessageTimeout(Input->Focus, WM_UNICHAR, UNICODE_NOCHAR, 0, Flags, 250, &Supported);
    if (Error != ERROR_SUCCESS)
    {
        return CuaFail(Input->Command, HRESULT_FROM_WIN32(Error),
                       L"The focused control did not answer the Unicode check. Inspect before trying Paste.");
    }
    if (Supported == 0 && IsWindowUnicode(Input->Focus) == FALSE)
    {
        return CuaFail(Input->Command, E_FAIL,
                       L"The focused control does not support Unicode messages. Inspect before trying Paste.");
    }
    UINT Message = Supported != 0 ? WM_UNICHAR : WM_CHAR;
    ULONGLONG Deadline = _Inline_GetTickCount64() + 5000;
    for (ULONG Index = 0; Index < Input->Length;)
    {
        Hr = CheckTextFocus(Input);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        if (_Inline_GetTickCount64() >= Deadline)
        {
            return CuaFail(Input->Command, HRESULT_FROM_WIN32(ERROR_TIMEOUT),
                           L"Text input reached its time limit. Some text may be present; inspect before retrying.");
        }
        WPARAM Character = Input->Text[Index];
        ULONG Units = 1;
        if (Supported != 0 && IS_HIGH_SURROGATE(Input->Text[Index]) != FALSE)
        {
            Character = 0x10000 + ((Character - 0xD800) << 10) + (Input->Text[Index + 1] - 0xDC00);
            Units = 2;
        }
        Error = UI_SendMessageTimeout(Input->Focus, Message, Character, 1, Flags, 250, NULL);
        if (Error != ERROR_SUCCESS)
        {
            return CuaFail(Input->Command, HRESULT_FROM_WIN32(Error),
                           L"Character delivery failed or timed out. "
                           L"Inspect before retrying; some text may be present.");
        }
        Index += Units;
    }
Exit:
    return Hr;
}

HRESULT
CuaTypeText(
    _Inout_ CUA_COMMAND* Command,
    _In_ PCWSTR Text,
    _In_ PCWSTR Method,
    _In_ HWND Expected,
    _In_ HWND ExpectedFocus)
{
    TEXT_INPUT Input = { 0 };
    IJsonObject* Error;
    Input.Command = Command;
    Input.Text = Text;
    Input.Length = (ULONG)wcslen(Text);
    Input.Expected = Expected;
    Input.Focus = ExpectedFocus;
    if (wcscmp(Method, L"paste") == 0)
    {
        HANDLE Thread;
        Input.Status = Err_NtStatusToHr(PS_CreateThread(NtCurrentProcess(), FALSE, PasteThread, &Input, &Thread, NULL));
        if (SUCCEEDED(Input.Status))
        {
            NtWaitForSingleObject(Thread, FALSE, NULL);
            NtClose(Thread);
        }
    } else
    {
        Input.Status = MessageText(&Input);
    }
    if (FAILED(Input.Status) && SUCCEEDED(Command->Error.Status))
    {
        CuaFail(Command, Input.Status, L"A text-input API failed. Inspect current state before sending more text.");
    }
    if (FAILED(Input.ClipboardStatus))
    {
        Error = BuildErrorOutput(Input.ClipboardStatus,
                                 "Restoring the OLE clipboard object failed.");
        if (Error != NULL)
        {
            Error->lpVtbl->QueryInterface(Error, &IID_IJsonValue, (PVOID*)&Command->ClipboardError);
            Error->lpVtbl->Release(Error);
        }
    }
    return Input.Status;
}
