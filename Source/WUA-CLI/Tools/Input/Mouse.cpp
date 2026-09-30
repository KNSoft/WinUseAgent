#include "pch.h"

static ULONG Handle;
static INT X;
static INT Y;
static PWSTR Button;
static PWSTR Operation;
static INT Delta = WHEEL_DELTA;
static PWSTR Axis;

static WUA_COMMAND_PARAMETER Parameters[] = {
    DEF_PARAMETER_ENTRY(Handle, HexU32, FALSE),
    DEF_PARAMETER_ENTRY(X, Int32, TRUE),
    DEF_PARAMETER_ENTRY(Y, Int32, TRUE),
    DEF_PARAMETER_ENTRY(Button, String, FALSE),
    DEF_PARAMETER_ENTRY(Operation, String, TRUE),
    DEF_PARAMETER_ENTRY(Delta, Int32, FALSE),
    DEF_PARAMETER_ENTRY(Axis, String, FALSE),
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
    dx = ((LONGLONG)ScreenX - ScreenPt.x) * 65535;
    dy = ((LONGLONG)ScreenY - ScreenPt.y) * 65535;
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
_Ret_maybenull_
IJsonObject*
Command(VOID)
{
    HWND hWnd;
    POINT Point;
    INPUT Inputs[5];
    UINT InputCount;
    DWORD DownFlag, UpFlag, MouseData;
    LOGICAL IsMove, IsWheel, DeltaSpecified;

    if (Operation == NULL || *Operation == UNICODE_NULL)
    {
        return BuildErrorOutput(E_INVALIDARG, "Parameter \"Operation\" is required.");
    }
    IsMove = _wcsicmp(Operation, L"Move") == 0;
    IsWheel = _wcsicmp(Operation, L"Wheel") == 0;
    if (!IsMove && !IsWheel &&
        _wcsicmp(Operation, L"Click") != 0 && _wcsicmp(Operation, L"DoubleClick") != 0 &&
        _wcsicmp(Operation, L"Down") != 0 && _wcsicmp(Operation, L"Up") != 0)
    {
        return BuildErrorOutput(E_INVALIDARG, "Parameter \"Operation\" is invalid.");
    }
    DeltaSpecified = FALSE;
    for (ULONG i = 0; i < ARRAYSIZE(Parameters); i++)
    {
        if (Parameters[i].Buffer == reinterpret_cast<PVOID*>(&Delta))
        {
            DeltaSpecified = Parameters[i].SizeOfBuffer == 0;
            break;
        }
    }
    if (!IsWheel && (DeltaSpecified || Axis != NULL))
    {
        return BuildErrorOutput(E_INVALIDARG, "Parameters \"Delta\" and \"Axis\" require Operation=Wheel.");
    }
    if (IsMove || IsWheel)
    {
        if (Button != NULL)
        {
            return BuildErrorOutput(E_INVALIDARG, "Parameter \"Button\" does not apply to Move or Wheel.");
        }
    } else if (Button == NULL || *Button == UNICODE_NULL)
    {
        return BuildErrorOutput(E_INVALIDARG, "Parameter \"Button\" is required.");
    }

    /* Button */
    DownFlag = UpFlag = MouseData = 0;
    if (IsMove || IsWheel)
    {
        // These operations do not press a mouse button.
    } else if (_wcsicmp(Button, L"Left") == 0)
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
        Inputs[1].type = INPUT_MOUSE;
        Inputs[1].mi.dwFlags = DownFlag;
        Inputs[1].mi.mouseData = MouseData;
        Inputs[2].type = INPUT_MOUSE;
        Inputs[2].mi.dwFlags = UpFlag;
        Inputs[2].mi.mouseData = MouseData;
        InputCount = 3;
    } else if (_wcsicmp(Operation, L"DoubleClick") == 0)
    {
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
        Inputs[1].type = INPUT_MOUSE;
        Inputs[1].mi.dwFlags = DownFlag;
        Inputs[1].mi.mouseData = MouseData;
        InputCount = 2;
    } else if (_wcsicmp(Operation, L"Up") == 0)
    {
        Inputs[1].type = INPUT_MOUSE;
        Inputs[1].mi.dwFlags = UpFlag;
        Inputs[1].mi.mouseData = MouseData;
        InputCount = 2;
    } else if (IsMove)
    {
        InputCount = 1;
    } else
    {
        if (Delta == 0)
        {
            return BuildErrorOutput(E_INVALIDARG, "Parameter \"Delta\" must be nonzero.");
        }
        Inputs[1].type = INPUT_MOUSE;
        Inputs[1].mi.mouseData = static_cast<DWORD>(Delta);
        if (Axis == NULL || _wcsicmp(Axis, L"Vertical") == 0)
        {
            Inputs[1].mi.dwFlags = MOUSEEVENTF_WHEEL;
        } else if (_wcsicmp(Axis, L"Horizontal") == 0)
        {
            Inputs[1].mi.dwFlags = MOUSEEVENTF_HWHEEL;
        } else
        {
            return BuildErrorOutput(E_INVALIDARG, "Parameter \"Axis\" must be Vertical or Horizontal.");
        }
        InputCount = 2;
    }

    /* Validate the complete request before activating a window or sending input. */
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
    BuildAbsoluteMouseMove(Point.x, Point.y, &Inputs[0]);

    SetLastError(ERROR_SUCCESS);
    if (SendInput(InputCount, Inputs, sizeof(INPUT)) != InputCount)
    {
        DWORD Error = GetLastError();
        return BuildErrorOutput(Error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(Error) : E_FAIL, "SendInput failed.");
    }
    return BuildSuccessOutput(NULL);
}
