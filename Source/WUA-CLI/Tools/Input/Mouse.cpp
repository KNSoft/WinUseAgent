#include "pch.h"

static ULONG Handle;
static INT X;
static INT Y;
static PWSTR Button;
static PWSTR Operation;

static WUA_COMMAND_PARAMETER Parameters[] = {
    DEF_PARAMETER_ENTRY(Handle, HexU32, FALSE),
    DEF_PARAMETER_ENTRY(X, Int32, TRUE),
    DEF_PARAMETER_ENTRY(Y, Int32, TRUE),
    DEF_PARAMETER_ENTRY(Button, String, TRUE),
    DEF_PARAMETER_ENTRY(Operation, String, TRUE),
};

WUA_COMMAND_FN Command;
WUA_COMMAND Input_Mouse = { Parameters, ARRAYSIZE(Parameters), &Command };

static
VOID
BuildAbsoluteMouseMove(
    _In_ INT ScreenX,
    _In_ INT ScreenY,
    _Out_ PINPUT Input)
{
    POINT ScreenPt;
    SIZE ScreenSize;
    LONGLONG dx, dy;

    UI_GetScreenPos(&ScreenPt, &ScreenSize);
    dx = (LONGLONG)(ScreenX - ScreenPt.x) * 65535;
    dy = (LONGLONG)(ScreenY - ScreenPt.y) * 65535;
    if (ScreenSize.cx > 1)
    {
        dx /= (LONGLONG)(ScreenSize.cx - 1);
    } else
    {
        dx = 0;
    }
    if (ScreenSize.cy > 1)
    {
        dy /= (LONGLONG)(ScreenSize.cy - 1);
    } else
    {
        dy = 0;
    }

    Input->type = INPUT_MOUSE;
    Input->mi.dx = (LONG)dx;
    Input->mi.dy = (LONG)dy;
    Input->mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
}

static
_Function_class_(WUA_COMMAND_FN)
_Ret_notnull_
cJSON*
Command(VOID)
{
    HWND hWnd;
    POINT Point;
    INPUT Inputs[5];
    UINT InputCount;
    DWORD DownFlag, UpFlag, MouseData;

    if (Button == NULL || *Button == UNICODE_NULL)
    {
        return BuildErrorOutput(E_INVALIDARG, "Parameter \"Button\" is required.");
    }
    if (Operation == NULL || *Operation == UNICODE_NULL)
    {
        return BuildErrorOutput(E_INVALIDARG, "Parameter \"Operation\" is required.");
    }
    
    /* Window Handle */
    Point.x = X;
    Point.y = Y;
    hWnd = reinterpret_cast<HWND>(UI_32ToHandle(Handle));
    if (hWnd != NULL)
    {
        if (!IsWindow(hWnd))
        {
            return BuildErrorOutput(E_INVALIDARG, "Parameter \"Handle\" is not a valid window handle.");
        }
        if (GetForegroundWindow() != hWnd && !Util_Window_Active(hWnd))
        {
            return BuildErrorOutput(E_FAIL, "Failed to activate specified window.");
        }
        if (!ClientToScreen(hWnd, &Point))
        {
            return BuildErrorOutput(HRESULT_FROM_WIN32(Err_GetLastError()), "ClientToScreen failed.");
        }
    }

    /* Button */
    MouseData = 0;
    if (_wcsicmp(Button, L"Left") == 0)
    {
        DownFlag = MOUSEEVENTF_LEFTDOWN;
        UpFlag = MOUSEEVENTF_LEFTUP;
    } else if (_wcsicmp(Button, L"Right") == 0)
    {
        DownFlag = MOUSEEVENTF_RIGHTDOWN;
        UpFlag = MOUSEEVENTF_RIGHTUP;
    } else if (_wcsicmp(Button, L"Middle") == 0)
    {
        DownFlag = MOUSEEVENTF_MIDDLEDOWN;
        UpFlag = MOUSEEVENTF_MIDDLEUP;
    } else if (_wcsicmp(Button, L"X1") == 0)
    {
        DownFlag = MOUSEEVENTF_XDOWN;
        UpFlag = MOUSEEVENTF_XUP;
        MouseData = XBUTTON1;
    } else if (_wcsicmp(Button, L"X2") == 0)
    {
        DownFlag = MOUSEEVENTF_XDOWN;
        UpFlag = MOUSEEVENTF_XUP;
        MouseData = XBUTTON2;
    } else
    {
        return BuildErrorOutput(E_INVALIDARG, "Parameter \"Button\" is invalid.");
    }

    /* Build Inputs */
    RtlZeroMemory(Inputs, sizeof(Inputs));
    if (_wcsicmp(Operation, L"Click") == 0)
    {
        BuildAbsoluteMouseMove(Point.x, Point.y, &Inputs[0]);
        Inputs[1].type = INPUT_MOUSE;
        Inputs[1].mi.dwFlags = DownFlag;
        Inputs[1].mi.mouseData = MouseData;
        Inputs[2].type = INPUT_MOUSE;
        Inputs[2].mi.dwFlags = UpFlag;
        Inputs[2].mi.mouseData = MouseData;
        InputCount = 3;
    } else if (_wcsicmp(Operation, L"DoubleClick") == 0)
    {
        BuildAbsoluteMouseMove(Point.x, Point.y, &Inputs[0]);
        Inputs[1].type = INPUT_MOUSE;
        Inputs[1].mi.dwFlags = DownFlag;
        Inputs[1].mi.mouseData = MouseData;
        Inputs[2].type = INPUT_MOUSE;
        Inputs[2].mi.dwFlags = UpFlag;
        Inputs[2].mi.mouseData = MouseData;
        Inputs[3].type = INPUT_MOUSE;
        Inputs[3].mi.dwFlags = DownFlag;
        Inputs[3].mi.mouseData = MouseData;
        Inputs[4].type = INPUT_MOUSE;
        Inputs[4].mi.dwFlags = UpFlag;
        Inputs[4].mi.mouseData = MouseData;
        InputCount = 5;
    } else if (_wcsicmp(Operation, L"Down") == 0)
    {
        BuildAbsoluteMouseMove(Point.x, Point.y, &Inputs[0]);
        Inputs[1].type = INPUT_MOUSE;
        Inputs[1].mi.dwFlags = DownFlag;
        Inputs[1].mi.mouseData = MouseData;
        InputCount = 2;
    } else if (_wcsicmp(Operation, L"Up") == 0)
    {
        BuildAbsoluteMouseMove(Point.x, Point.y, &Inputs[0]);
        Inputs[1].type = INPUT_MOUSE;
        Inputs[1].mi.dwFlags = UpFlag;
        Inputs[1].mi.mouseData = MouseData;
        InputCount = 2;
    } else
    {
        return BuildErrorOutput(E_INVALIDARG, "Parameter \"Operation\" is invalid.");
    }

    if (SendInput(InputCount, Inputs, sizeof(INPUT)) != InputCount)
    {
        return BuildErrorOutput(HRESULT_FROM_WIN32(Err_GetLastError()), "SendInput failed.");
    }
    return BuildSuccessOutput(NULL);
}
