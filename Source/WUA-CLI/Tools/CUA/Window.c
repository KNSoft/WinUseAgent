#include "pch.h"
#include "Server.h"

HRESULT
CuaOperate(
    _Inout_ CUA_COMMAND* Command)
{
    CUA_WINDOW Window = { 0 };
    PCWSTR Path;
    CUA_METHOD Method = Command->Request->Method;
    LOGICAL Resolved = FALSE;
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
    if (Window.Handle == NULL)
    {
        return CuaFail(Command, E_INVALIDARG, L"Window requires a nonzero Handle.");
    }
    Hr = CuaInputDesktop(Command);
    if (FAILED(Hr))
    {
        goto Exit;
    }
    Resolved = TRUE;
    if (Method == CuaMethodActivate)
    {
        Hr = CuaPrepareWindow(Command, &Window, TRUE);
    } else if (Method == CuaMethodMinimize || Method == CuaMethodMaximize)
    {
        if (NtUserShowWindowAsync(Window.Handle, Method == CuaMethodMinimize ? SW_MINIMIZE : SW_MAXIMIZE) == FALSE)
        {
            Hr = CuaFail(Command, E_FAIL, L"Windows did not start the requested window operation.");
            goto Exit;
        }
    } else
    {
        if (PostMessageW(Window.Handle, WM_CLOSE, 0, 0) == FALSE)
        {
            Hr = CuaFail(Command, HRESULT_FROM_WIN32(Err_GetLastError()),
                         L"Windows did not accept the close request.");
        }
    }
Exit:
    if (Resolved != FALSE)
    {
        if (FAILED(Hr) && SUCCEEDED(Command->Error.Status))
        {
            CuaFail(Command, Hr, L"A Windows API failed while performing the window operation.");
        }
        HWND Handle = Window.Handle;
        if (Method == CuaMethodMinimize || Method == CuaMethodCloseWindow || CuaSameWindow(&Window) == FALSE ||
            IsWindowEnabled(Handle) == FALSE)
        {
            Handle = NULL;
        }
        CuaAfterAction(Command, Handle);
    }
    return Hr;
}
