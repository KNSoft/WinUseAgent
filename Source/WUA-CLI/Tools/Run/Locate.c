#include "pch.h"
#include "../CUA/Server.h"

typedef struct _LOCATE_REQUEST
{
    PCWSTR Path;
    HRESULT Status;
} LOCATE_REQUEST;

static
_Function_class_(USER_THREAD_START_ROUTINE)
NTSTATUS
NTAPI
LocateThread(_In_ PVOID Context)
{
    LOCATE_REQUEST* Request = (LOCATE_REQUEST*)Context;
    Request->Status = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(Request->Status))
    {
        Request->Status = Shell_LocateItem(Request->Path);
        CoUninitialize();
    }
    return 0;
}

HRESULT
CuaLocate(
    _Inout_ CUA_COMMAND* Command)
{
    LOCATE_REQUEST Request;
    HRESULT Hr;
    HANDLE Thread;

    Request.Path = CuaString(&Command->Request->Parameters, CuaParamPath, L"");
    if (*Request.Path == UNICODE_NULL)
    {
        return CuaFail(Command, E_INVALIDARG, L"Path is required.");
    }
    Hr = Err_NtStatusToHr(PS_CreateThread(NtCurrentProcess(), FALSE, LocateThread, &Request, &Thread, NULL));
    if (FAILED(Hr))
    {
        return Hr;
    }
    NtWaitForSingleObject(Thread, FALSE, NULL);
    NtClose(Thread);
    return Request.Status;
}
