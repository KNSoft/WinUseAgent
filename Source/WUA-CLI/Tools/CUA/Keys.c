#include "pch.h"
#include "Server.h"

HRESULT
CuaKeyCode(
    _Inout_ CUA_COMMAND* Command,
    _In_ PCWSTR Name,
    _Out_ PWORD Key)
{
    static const struct
    {
        PCWSTR Name;
        WORD Code;
    } Keys[] = { { L"CTRL", VK_CONTROL },  { L"CONTROL", VK_CONTROL }, { L"SHIFT", VK_SHIFT },
                 { L"ALT", VK_MENU },      { L"WIN", VK_LWIN },        { L"ENTER", VK_RETURN },
                 { L"TAB", VK_TAB },       { L"ESC", VK_ESCAPE },      { L"ESCAPE", VK_ESCAPE },
                 { L"SPACE", VK_SPACE },   { L"BACKSPACE", VK_BACK },  { L"DELETE", VK_DELETE },
                 { L"INSERT", VK_INSERT }, { L"HOME", VK_HOME },       { L"END", VK_END },
                 { L"PAGEUP", VK_PRIOR },  { L"PAGEDOWN", VK_NEXT },   { L"LEFT", VK_LEFT },
                 { L"RIGHT", VK_RIGHT },   { L"UP", VK_UP },           { L"DOWN", VK_DOWN } };
    *Key = 0;
    for (ULONG Index = 0; Index < ARRAYSIZE(Keys); ++Index)
    {
        if (_wcsicmp(Name, Keys[Index].Name) == 0)
        {
            *Key = Keys[Index].Code;
            return S_OK;
        }
    }
    WCHAR First = Str_UpperCharW(Name[0]);
    if (*Name != UNICODE_NULL && Name[1] == UNICODE_NULL &&
        ((First >= L'A' && First <= L'Z') || (First >= L'0' && First <= L'9')))
    {
        *Key = First;
        return S_OK;
    }
    for (ULONG Index = 1; Index <= 24; ++Index)
    {
        WCHAR Function[4];
        Str_PrintfExW(Function, ARRAYSIZE(Function), L"F%lu", Index);
        if (_wcsicmp(Name, Function) == 0)
        {
            *Key = (WORD)(VK_F1 + Index - 1);
            return S_OK;
        }
    }
    return CuaFail(Command, E_INVALIDARG, L"Unknown key name; use CUA Text for literal text.");
}

HRESULT
CuaParseKeys(
    _Inout_ CUA_COMMAND* Command,
    _Out_writes_(CUA_MAX_KEYS) WORD* Keys)
{
    const CUA_PARAMETERS* Parameters = &Command->Request->Parameters;
    HRESULT Hr;
    RtlZeroMemory(Keys, CUA_MAX_KEYS * sizeof(*Keys));
    if (Parameters->KeyCount == 0 || Parameters->KeyCount > CUA_MAX_KEYS)
    {
        return CuaFail(Command, E_INVALIDARG, L"Supply a chord of 1..8 unique key names.");
    }
    for (ULONG Index = 0; Index < Parameters->KeyCount; ++Index)
    {
        Hr = CuaKeyCode(Command, _Inline_WindowsGetStringRawBuffer(Parameters->Keys[Index], NULL), &Keys[Index]);
        if (FAILED(Hr))
        {
            goto Exit;
        }
        for (ULONG Previous = 0; Previous < Index; ++Previous)
        {
            if (Keys[Previous] == Keys[Index])
            {
                return CuaFail(Command, E_INVALIDARG, L"A chord cannot contain duplicate keys.");
            }
        }
    }
    Hr = S_OK;
Exit:
    return Hr;
}

HRESULT
CuaPressKeys(
    _Inout_ CUA_COMMAND* Command,
    _In_reads_(Count) const WORD* Keys,
    _In_ ULONG Count)
{
    INPUT Inputs[CUA_MAX_KEYS * 2] = { 0 };
    if (Count == 0 || Count > CUA_MAX_KEYS)
    {
        return E_INVALIDARG;
    }
    for (ULONG Index = 0; Index < Count; ++Index)
    {
        WORD Key = Keys[Index];
        if (GetAsyncKeyState(Key) < 0)
        {
            return CuaFail(Command, E_FAIL, L"A requested key is already held; release it before sending keys.");
        }
        Inputs[Index].type = INPUT_KEYBOARD;
        Inputs[Index].ki.wVk = Key;
        if (Key == VK_LWIN || Key == VK_RWIN || (Key >= VK_PRIOR && Key <= VK_DOWN) || Key == VK_INSERT ||
            Key == VK_DELETE)
        {
            Inputs[Index].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
        }
    }
    for (ULONG Index = 0; Index < Count; ++Index)
    {
        Inputs[Count + Index] = Inputs[Count - Index - 1];
        Inputs[Count + Index].ki.dwFlags |= KEYEVENTF_KEYUP;
    }
    return CuaSend(Command, Inputs, Count * 2);
}
