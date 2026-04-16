#include "pch.h"

static PWSTR Text;

static WUA_COMMAND_PARAMETER Parameters[] = {
    DEF_PARAMETER_ENTRY(Text, String, TRUE),
};

WUA_COMMAND_FN Command;
WUA_COMMAND Input_Text = { Parameters, ARRAYSIZE(Parameters), &Command };

static
HRESULT
SetClipboardUnicodeText(
    _In_ PCWSTR pszText)
{
    SIZE_T cbText;
    PWSTR pText;
    HGLOBAL hText;

    cbText = (wcslen(pszText) + 1) * sizeof(WCHAR);
    hText = GlobalAlloc(GMEM_MOVEABLE, cbText);
    if (hText == NULL)
    {
        return E_OUTOFMEMORY;
    }

    pText = reinterpret_cast<PWSTR>(GlobalLock(hText));
    if (pText == NULL)
    {
        GlobalFree(hText);
        return HRESULT_FROM_WIN32(Err_GetLastError());
    }
    RtlCopyMemory(pText, pszText, cbText);
    GlobalUnlock(hText);

    if (!EmptyClipboard())
    {
        GlobalFree(hText);
        return HRESULT_FROM_WIN32(Err_GetLastError());
    }
    if (SetClipboardData(CF_UNICODETEXT, hText) == NULL)
    {
        HRESULT Hr = HRESULT_FROM_WIN32(Err_GetLastError());
        GlobalFree(hText);
        return Hr;
    }
    return S_OK;
}

static
HRESULT
SendPasteShortcut(VOID)
{
    INPUT Inputs[4] = { 0 };
    UINT Sent;

    Inputs[0].type = INPUT_KEYBOARD;
    Inputs[0].ki.wVk = VK_CONTROL;
    Inputs[1].type = INPUT_KEYBOARD;
    Inputs[1].ki.wVk = 'V';
    Inputs[2].type = INPUT_KEYBOARD;
    Inputs[2].ki.wVk = 'V';
    Inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    Inputs[3].type = INPUT_KEYBOARD;
    Inputs[3].ki.wVk = VK_CONTROL;
    Inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

    Sent = SendInput(ARRAYSIZE(Inputs), Inputs, sizeof(INPUT));
    if (Sent != ARRAYSIZE(Inputs))
    {
        return HRESULT_FROM_WIN32(Err_GetLastError());
    }
    return S_OK;
}

static
_Function_class_(WUA_COMMAND_FN)
_Ret_notnull_
cJSON*
Command(VOID)
{
    cJSON* j;
    IDataObject* pOldClipboard;
    HRESULT Hr;

    if (Text == NULL || *Text == UNICODE_NULL)
    {
        return BuildErrorOutput(E_INVALIDARG, "Parameter \"Text\" is required.");
    }

    if (FAILED(OleInitialize(NULL)))
    {
        return BuildErrorOutput(E_FAIL, "OleInitialize failed.");
    }

    pOldClipboard = NULL;
    OleGetClipboard(&pOldClipboard);

    if (!OpenClipboard(NULL))
    {
        j = BuildErrorOutput(HRESULT_FROM_WIN32(Err_GetLastError()), "OpenClipboard failed.");
        goto _Exit;
    }

    Hr = SetClipboardUnicodeText(Text);
    CloseClipboard();
    if (FAILED(Hr))
    {
        j = BuildErrorOutput(Hr, "Failed to write text to clipboard.");
        goto _Exit;
    }

    Hr = SendPasteShortcut();
    if (FAILED(Hr))
    {
        j = BuildErrorOutput(Hr, "SendInput failed while sending Ctrl+V.");
        goto _Exit;
    }

    //
    // Keep the temporary clipboard data alive briefly so the target window can
    // read it after the synthesized paste shortcut is delivered.
    //
    Sleep(100);
    j = BuildSuccessOutput(NULL);

_Exit:
    if (pOldClipboard != NULL)
    {
        OleSetClipboard(pOldClipboard);
        pOldClipboard->Release();
    }
    OleUninitialize();
    return j;
}
