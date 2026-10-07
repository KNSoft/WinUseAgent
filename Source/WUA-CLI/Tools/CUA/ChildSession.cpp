#include "pch.h"
#include "ChildSession.h"
#include "Worker.h"
#include <wtsapi32.h>
#include <rpc.h>

#import "libid:8C11EFA1-92C3-11D1-BC1E-00C04FA31489" version("1.0") \
    raw_interfaces_only named_guids rename_namespace("MSTSCLib") \
    exclude("wireHWND", "_RemotableHandle", "__MIDL_IWinTypes_0009")

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "winsta.lib")
#pragma comment(lib, "comctl32.lib")

#define WM_CUA_PREVIEW (WM_APP + 1)
#define CHILD_STARTUP_TIMEOUT 30000
#define CHILD_WORKER_ATTEMPTS 3

typedef enum _CHILD_PHASE
{
    ChildConnecting,
    ChildAwaitingUser,
    ChildStarting,
    ChildReady,
    ChildWorkerFailed,
    ChildClosed,
    ChildFailed,
    ChildDestroyFailed
} CHILD_PHASE;

static const PCWSTR ChildPhaseNames[] = {
    L"connecting", L"awaiting_user", L"starting", L"ready", L"worker_failed",
    L"closed", L"failed", L"destroy_failed"
};

typedef struct _CHILD_STATE
{
    LIST_ENTRY Entry;
    RTL_SRWLOCK Lock;
    LONG References;
    CUA_SESSION_INFO Info;
    CUA_SESSION_MANAGER* Manager;
    DWORD ParentId, ShutdownTimeout;
    HANDLE Ready, Stop, Recover, Thread;
    PUI_RDP_CONTEXT Dialog;
    HWND Preview;
    LOGICAL Show, LoggedIn, NotificationsRegistered, ConnectionStarted;
    LOGICAL DesktopReady;
    CHILD_PHASE Phase;
    HRESULT ConnectionError;
    LONG RdpCode;
} CHILD_STATE;

struct _CUA_SESSION_MANAGER
{
    CUA_SESSION_PROBE Probe;
    PVOID ProbeContext;
    LIST_ENTRY Children;
    RTL_SRWLOCK Lock;
};

static
HRESULT
Failure(
    _In_ HRESULT Result,
    _Out_writes_(512) PWSTR Detail,
    _In_ PCWSTR Text)
{
    Str_CopyExW(Detail, 512, Text);
    return Result;
}

static
VOID
ReadChild(
    _In_ CHILD_STATE* Child,
    _Out_ CUA_SESSION_INFO* Info)
{
    RtlAcquireSRWLockShared(&Child->Lock);
    *Info = Child->Info;
    RtlReleaseSRWLockShared(&Child->Lock);
}

static
CHILD_PHASE
ReadPhase(
    _In_ CHILD_STATE* Child)
{
    RtlAcquireSRWLockShared(&Child->Lock);
    CHILD_PHASE Phase = Child->Phase;
    RtlReleaseSRWLockShared(&Child->Lock);
    return Phase;
}

static
VOID
SetState(
    _Inout_ CHILD_STATE* Child,
    _In_ CHILD_PHASE Phase,
    _In_ HRESULT Error,
    _In_ PCWSTR Detail)
{
    RtlAcquireSRWLockExclusive(&Child->Lock);
    Child->Phase = Phase;
    Str_CopyW(Child->Info.State, ChildPhaseNames[Phase]);
    Str_CopyW(Child->Info.Detail, Detail);
    Child->Info.LastError = Error;
    if (Phase != ChildReady)
    {
        Child->Info.Instance[0] = UNICODE_NULL;
    }
    RtlReleaseSRWLockExclusive(&Child->Lock);
}

static
HRESULT
ChildId(
    _Out_ PDWORD SessionId)
{
    ULONG Id = MAXULONG;
    *SessionId = MAXDWORD;
    if (WinStationGetChildSessionId(&Id) == FALSE)
    {
        DWORD Error = Err_GetLastError();
        return HRESULT_FROM_WIN32(Error == ERROR_FILE_NOT_FOUND ? ERROR_NOT_FOUND : Error);
    }
    if (Id == MAXULONG)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    *SessionId = Id;
    return S_OK;
}

static
HRESULT
AdoptChild(
    _Inout_ CHILD_STATE* Child)
{
    DWORD Id;
    ULONG ParentId;
    HRESULT Hr = ChildId(&Id);
    if (FAILED(Hr))
    {
        return Hr;
    }
    if (WinStationGetParentSessionId(Id, &ParentId) == FALSE)
    {
        return HRESULT_FROM_WIN32(Err_GetLastError());
    }
    if (ParentId != Child->ParentId)
    {
        return E_ACCESSDENIED;
    }
    RtlAcquireSRWLockExclusive(&Child->Lock);
    if (Child->Info.Id != MAXDWORD && Child->Info.Id != Id)
    {
        Hr = HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    } else
    {
        Child->Info.Id = Id;
        Child->Info.Owned = TRUE;
    }
    RtlReleaseSRWLockExclusive(&Child->Lock);
    return Hr;
}

static
HRESULT
SetExtendedBoolean(
    _In_ MSTSCLib::IMsRdpExtendedSettings* Settings,
    _In_ PCWSTR Name,
    _In_ VARIANT_BOOL Value)
{
    VARIANT Property = { 0 };
    BSTR PropertyName = SysAllocString(Name);
    if (PropertyName == NULL)
    {
        return E_OUTOFMEMORY;
    }
    Property.vt = VT_BOOL;
    Property.boolVal = Value;
    HRESULT Hr = Settings->put_Property(PropertyName, &Property);
    SysFreeString(PropertyName);
    return Hr;
}

static
VOID
CALLBACK
RdpEvent(
    _Inout_ PUI_RDP_CONTEXT Dialog,
    _In_ DISPID Id,
    _In_ DISPPARAMS* Parameters,
    _In_ PVOID Context)
{
    CHILD_STATE* Child = (CHILD_STATE*)Context;
    UNREFERENCED_PARAMETER(Dialog);
    if (Id == MSTSCAXEVENT_DISPID_LOGINCOMPLETE)
    {
        Child->LoggedIn = TRUE;
        if (ReadPhase(Child) == ChildConnecting || ReadPhase(Child) == ChildAwaitingUser)
        {
            SetState(Child, ChildStarting, S_OK, L"Logged on; waiting for the child desktop.");
        }
    } else if (Id == MSTSCAXEVENT_DISPID_INTERNALDIALOGDISPLAYED && Child->LoggedIn == FALSE)
    {
        SetState(Child, ChildAwaitingUser, S_OK,
                 L"Ask the user to enter their Windows password in the system credential prompt; "
                 L"do not request it in chat.");
    } else if (Id == MSTSCAXEVENT_DISPID_INTERNALDIALOGDISMISSED && Child->LoggedIn == FALSE)
    {
        SetState(Child, ChildConnecting, S_OK, L"Waiting for the child login.");
    } else if (Id == MSTSCAXEVENT_DISPID_DISCONNECTED || Id == MSTSCAXEVENT_DISPID_FATALERROR)
    {
        Child->ConnectionError = HRESULT_FROM_WIN32(ERROR_CONNECTION_ABORTED);
    }
    if ((Id == MSTSCAXEVENT_DISPID_DISCONNECTED || Id == MSTSCAXEVENT_DISPID_FATALERROR ||
         Id == MSTSCAXEVENT_DISPID_LOGONERROR) &&
        Parameters != NULL && Parameters->cArgs != 0)
    {
        Child->RdpCode = Parameters->rgvarg[0].lVal;
    }
}

static
LRESULT
CALLBACK
ChildWindowProc(
    _In_ HWND Window,
    _In_ UINT Message,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam,
    _In_ UINT_PTR SubclassId,
    _In_ DWORD_PTR ReferenceData)
{
    CHILD_STATE* Child = (CHILD_STATE*)ReferenceData;
    if (Message == WM_CUA_PREVIEW && (DWORD_PTR)LParam == ReferenceData)
    {
        if (PS_WaitForObject(Child->Stop, 0) == STATUS_WAIT_0)
        {
            return 0;
        }
        ShowWindow(Window, WParam != 0 ? SW_SHOWNOACTIVATE : SW_HIDE);
        RtlAcquireSRWLockExclusive(&Child->Lock);
        Child->Info.PreviewVisible = IsWindowVisible(Window) != FALSE;
        LOGICAL Applied = Child->Info.PreviewVisible == (WParam != 0);
        RtlReleaseSRWLockExclusive(&Child->Lock);
        return Applied;
    }
    if (Message == WM_SHOWWINDOW)
    {
        RtlAcquireSRWLockExclusive(&Child->Lock);
        Child->Info.PreviewVisible = WParam != 0;
        RtlReleaseSRWLockExclusive(&Child->Lock);
    }
    if (Message == WM_WTSSESSION_CHANGE && WParam == WTS_SESSION_DESKTOP_READY)
    {
        if (SUCCEEDED(AdoptChild(Child)) && Child->Info.Id == (DWORD)LParam)
        {
            Child->DesktopReady = TRUE;
            if (Child->LoggedIn != FALSE && ReadPhase(Child) == ChildStarting)
            {
                SetState(Child, ChildStarting, S_OK, L"The child desktop is ready; starting its Worker.");
            }
        }
    }
    if (Message == WM_CLOSE)
    {
        ShowWindow(Window, SW_HIDE);
        NtSetEvent(Child->Stop, NULL);
        return 0;
    }
    if (Message == WM_NCDESTROY)
    {
        RtlAcquireSRWLockExclusive(&Child->Lock);
        Child->Preview = NULL;
        Child->Info.PreviewVisible = FALSE;
        RtlReleaseSRWLockExclusive(&Child->Lock);
        if (Child->NotificationsRegistered != FALSE)
        {
            WTSUnRegisterSessionNotificationEx(WTS_CURRENT_SERVER_HANDLE, Window);
            Child->NotificationsRegistered = FALSE;
        }
        Child->Dialog = NULL;
        NtSetEvent(Child->Stop, NULL);
        RemoveWindowSubclass(Window, ChildWindowProc, SubclassId);
    }
    return DefSubclassProc(Window, Message, WParam, LParam);
}

static
HRESULT
ConnectChild(
    _Inout_ CHILD_STATE* State)
{
    UI_RDP_OPTIONS Options = { 0 };
    MSTSCLib::IMsRdpClient9* Client;
    MSTSCLib::IMsRdpExtendedSettings* Extended = NULL;
    MSTSCLib::IMsRdpClientAdvancedSettings8* Advanced = NULL;
    MSTSCLib::IMsRdpClientNonScriptable5* NonScriptable = NULL;
    BSTR Server = NULL;
    HRESULT Result;

    Options.Title = L"WUA CUA Child Session";
    Options.Style = WS_OVERLAPPEDWINDOW;
    Options.ExStyle = WS_EX_APPWINDOW;
    SetRect(&Options.Rect, 0, 0, (LONG)State->Info.Width, (LONG)State->Info.Height);
    if (AdjustWindowRectExForDpi(&Options.Rect, Options.Style, FALSE, Options.ExStyle, GetDpiForSystem()) == FALSE)
    {
        return HRESULT_FROM_WIN32(Err_GetLastError());
    }
    OffsetRect(&Options.Rect, 40 - Options.Rect.left, 40 - Options.Rect.top);
    Options.Callback = RdpEvent;
    Options.Context = State;
    Result = UI_CreateRdpDialog(&Options, &State->Dialog);
    if (FAILED(Result))
    {
        return Result;
    }
    if (SetWindowSubclass(State->Dialog->Window, ChildWindowProc, 1, (DWORD_PTR)State) == FALSE)
    {
        return E_FAIL;
    }
    RtlAcquireSRWLockExclusive(&State->Lock);
    State->Preview = State->Dialog->Window;
    RtlReleaseSRWLockExclusive(&State->Lock);
    if (WTSRegisterSessionNotificationEx(WTS_CURRENT_SERVER_HANDLE, State->Dialog->Window, NOTIFY_FOR_ALL_SESSIONS) ==
        FALSE)
    {
        return HRESULT_FROM_WIN32(Err_GetLastError());
    }
    State->NotificationsRegistered = TRUE;
    Client = State->Dialog->Client;
    Server = SysAllocString(L"localhost");
    if (Server == NULL)
    {
        Result = E_OUTOFMEMORY;
        goto Exit;
    }
    Result = Client->QueryInterface(IID_PPV_ARGS(&Extended));
    if (FAILED(Result))
    {
        goto Exit;
    }
    Result = Client->QueryInterface(IID_PPV_ARGS(&NonScriptable));
    if (FAILED(Result))
    {
        goto Exit;
    }
    Result = NonScriptable->put_PromptForCredentials(VARIANT_FALSE);
    if (FAILED(Result))
    {
        goto Exit;
    }
    Result = NonScriptable->put_AllowPromptingForCredentials(VARIANT_TRUE);
    if (FAILED(Result))
    {
        goto Exit;
    }
    Result = NonScriptable->put_AllowCredentialSaving(VARIANT_TRUE);
    if (FAILED(Result))
    {
        goto Exit;
    }
    Result = SetExtendedBoolean(Extended, L"ConnectToChildSession", VARIANT_TRUE);
    if (FAILED(Result))
    {
        goto Exit;
    }
    Result = SetExtendedBoolean(Extended, L"EnableFrameBufferRedirection", VARIANT_TRUE);
    if (FAILED(Result))
    {
        goto Exit;
    }
    Result = Client->put_Server(Server);
    if (FAILED(Result))
    {
        goto Exit;
    }
    Result = Client->put_DesktopWidth((LONG)State->Info.Width);
    if (FAILED(Result))
    {
        goto Exit;
    }
    Result = Client->put_DesktopHeight((LONG)State->Info.Height);
    if (FAILED(Result))
    {
        goto Exit;
    }
    Result = Client->get_AdvancedSettings9(&Advanced);
    if (FAILED(Result))
    {
        goto Exit;
    }
    Result = Advanced->put_RedirectClipboard(VARIANT_FALSE);
    if (FAILED(Result))
    {
        goto Exit;
    }
    Result = Advanced->put_RedirectDrives(VARIANT_FALSE);
    if (FAILED(Result))
    {
        goto Exit;
    }
    Result = Advanced->put_GrabFocusOnConnect(VARIANT_FALSE);
    if (FAILED(Result))
    {
        goto Exit;
    }
    Result = Advanced->put_EnableCredSspSupport(VARIANT_TRUE);
    if (FAILED(Result))
    {
        goto Exit;
    }
    Result = Advanced->put_AuthenticationLevel(MSTSCAX_AUTHENTICATION_LEVEL_NONE);
    if (FAILED(Result))
    {
        goto Exit;
    }
    Result = Advanced->put_SmartSizing(VARIANT_TRUE);
    if (FAILED(Result))
    {
        goto Exit;
    }
    // SmartSizing only scales the preview; AutoResize stays disabled.
    Result = UI_RdpDialogConnect(State->Dialog);
    State->ConnectionStarted = SUCCEEDED(Result);
    if (SUCCEEDED(Result) && State->Show != FALSE)
    {
        ShowWindow(State->Dialog->Window, SW_SHOWNOACTIVATE);
    }
Exit:
    if (Advanced != NULL)
    {
        Advanced->Release();
    }
    if (NonScriptable != NULL)
    {
        NonScriptable->Release();
    }
    if (Extended != NULL)
    {
        Extended->Release();
    }
    SysFreeString(Server);
    return Result;
}

static
HRESULT
LogoffOwnedChild(
    _In_ DWORD Id,
    _In_ DWORD ParentId,
    _In_ DWORD TimeoutMs)
{
    if (Id == MAXDWORD)
    {
        return S_OK;
    }
    DWORD CurrentId;
    HRESULT Result = ChildId(&CurrentId);
    if (Result == HRESULT_FROM_WIN32(ERROR_NOT_FOUND))
    {
        return S_OK;
    }
    if (FAILED(Result))
    {
        return Result;
    }
    if (CurrentId != Id)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    }
    ULONG ActualParent;
    if (WinStationGetParentSessionId(Id, &ActualParent) == FALSE)
    {
        return HRESULT_FROM_WIN32(Err_GetLastError());
    }
    if (ActualParent != ParentId)
    {
        return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
    }
    if (WinStationReset(WINSTATION_CURRENT_SERVER, Id, FALSE) == FALSE)
    {
        return HRESULT_FROM_WIN32(Err_GetLastError());
    }
    ULONGLONG Deadline = _Inline_GetTickCount64() + TimeoutMs;
    do
    {
        Result = ChildId(&CurrentId);
        if (Result == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) || (SUCCEEDED(Result) && CurrentId != Id))
        {
            return S_OK;
        }
        if (FAILED(Result))
        {
            return Result;
        }
        PS_DelayExec(50);
    } while (_Inline_GetTickCount64() < Deadline);
    return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
}

_Function_class_(USER_THREAD_START_ROUTINE)
static
NTSTATUS
NTAPI
ChildThread(
    _In_ PVOID Parameter)
{
    CHILD_STATE* State = (CHILD_STATE*)Parameter;
    HANDLE Slot = NULL;
    WCHAR SlotName[80];
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Attributes;
    LOGICAL SlotOwned = FALSE, Initialized = FALSE;
    Str_PrintfW(SlotName, L"\\Sessions\\%lu\\BaseNamedObjects\\KNSoft.WUA.CUA.ChildSession", State->ParentId);
    RtlInitUnicodeString(&Name, SlotName);
    NT_InitObject(&Attributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_OPENIF, NULL);
    NTSTATUS Status = NtCreateMutant(&Slot, MUTANT_ALL_ACCESS, &Attributes, FALSE);
    HRESULT Result = Err_NtStatusToHr(Status);
    PCWSTR FailureText = L"Unable to acquire the child-session host slot.";
    HANDLE Server = NULL;
    DWORD WorkerId = 0;
    ULONGLONG Deadline = 0, NextAttempt = 0;
    ULONG Attempts = 0;
    LOGICAL LogonObserved = FALSE;
    if (SUCCEEDED(Result))
    {
        NTSTATUS Wait = PS_WaitForObject(Slot, 0);
        SlotOwned = Wait == STATUS_WAIT_0 || Wait == STATUS_ABANDONED;
        if (SlotOwned == FALSE)
        {
            Result = NT_SUCCESS(Wait) ? HRESULT_FROM_WIN32(ERROR_BUSY) : Err_NtStatusToHr(Wait);
            FailureText = L"Another WUA host already owns the child-session slot.";
        }
    }
    DWORD Existing;
    if (SUCCEEDED(Result))
    {
        Result = ChildId(&Existing);
        if (SUCCEEDED(Result))
        {
            Result = HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
            FailureText = L"A child session already exists; use its Windows Session ID.";
        } else if (Result == HRESULT_FROM_WIN32(ERROR_NOT_FOUND))
        {
            Result = S_OK;
        }
    }
    if (SUCCEEDED(Result))
    {
        Result = OleInitialize(NULL);
        Initialized = SUCCEEDED(Result);
        FailureText = L"Unable to connect Remote Desktop. Guide the user through any Windows credential prompt; "
                      L"if Windows blocks child sessions, optionally call CUA Session EnableChildSession.";
        if (Initialized != FALSE)
        {
            Result = ConnectChild(State);
        }
    }
    while (SUCCEEDED(Result))
    {
        if (PS_WaitForObject(State->Stop, 0) == STATUS_WAIT_0)
        {
            break;
        }
        MSG Message;
        while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE) != FALSE)
        {
            if (Message.message == WM_QUIT)
            {
                Result = HRESULT_FROM_WIN32(ERROR_CANCELLED);
                FailureText = L"The child-session host message loop was stopped.";
                break;
            }
            if (State->Dialog != NULL && UI_RdpDialogTranslateMessage(State->Dialog, &Message) != FALSE)
            {
                continue;
            }
            TranslateMessage(&Message);
            DispatchMessageW(&Message);
        }
        if (FAILED(Result) || PS_WaitForObject(State->Stop, 0) == STATUS_WAIT_0)
        {
            break;
        }
        if (FAILED(State->ConnectionError))
        {
            Result = State->ConnectionError;
            FailureText =
                L"The Remote Desktop connection ended. Guide the user through the Windows credential "
                L"prompt on the next CreateChild; optionally enable child-session settings if Windows blocks it.";
            break;
        }
        HRESULT Adopted = AdoptChild(State);
        if (FAILED(Adopted) && Adopted != HRESULT_FROM_WIN32(ERROR_NOT_FOUND))
        {
            Result = Adopted;
            FailureText = L"The connected child session does not belong to this parent.";
            break;
        }
        ULONGLONG Now = _Inline_GetTickCount64();
        if (ReadPhase(State) == ChildAwaitingUser)
        {
            Deadline = 0;
        } else if (State->LoggedIn == FALSE && Deadline == 0)
        {
            Deadline = Now + CHILD_STARTUP_TIMEOUT;
        }
        if (State->LoggedIn != FALSE && LogonObserved == FALSE)
        {
            LogonObserved = TRUE;
            Deadline = Now + CHILD_STARTUP_TIMEOUT;
        }
        if (Server != NULL && PS_WaitForObject(Server, 0) == STATUS_WAIT_0)
        {
            NtClose(Server);
            Server = NULL;
            WorkerId = 0;
            if (ReadPhase(State) == ChildReady)
            {
                Deadline = Now + CHILD_STARTUP_TIMEOUT;
                Attempts = 0;
                NtResetEvent(State->Ready, NULL);
                SetState(State, ChildStarting, S_OK, L"Recovering the exited Worker in the retained child.");
            }
        }
        if (PS_WaitForObject(State->Recover, 0) == STATUS_WAIT_0)
        {
            Deadline = Now + CHILD_STARTUP_TIMEOUT;
            Attempts = 0;
            NextAttempt = 0;
            SetState(State, ChildStarting, S_OK, L"Checking the retained child Worker.");
            NtResetEvent(State->Recover, NULL);
        }
        if (State->LoggedIn != FALSE && State->DesktopReady != FALSE && SUCCEEDED(Adopted) &&
            ReadPhase(State) != ChildWorkerFailed && Now >= NextAttempt)
        {
            WCHAR Instance[CUA_GUID_CCH] = { 0 };
            DWORD ServerProcess = 0;
            HRESULT ProbeResult =
                State->Manager->Probe(State->Manager->ProbeContext, State->Info.Id, 1000, Instance, &ServerProcess);
            if (SUCCEEDED(ProbeResult) && WorkerId != ServerProcess)
            {
                HANDLE Worker = NULL;
                Status = PS_OpenProcess(&Worker, SYNCHRONIZE, ServerProcess);
                if (!NT_SUCCESS(Status))
                {
                    ProbeResult = Err_NtStatusToHr(Status);
                } else
                {
                    if (Server != NULL)
                    {
                        NtClose(Server);
                    }
                    Server = Worker;
                    WorkerId = ServerProcess;
                }
            }
            if (ProbeResult == S_OK && Instance[0] != UNICODE_NULL)
            {
                if (State->Dialog->State.DesktopWidth != 0 &&
                    (State->Dialog->State.DesktopWidth != State->Info.Width ||
                     State->Dialog->State.DesktopHeight != State->Info.Height))
                {
                    WCHAR Detail[512];
                    Str_PrintfW(Detail,
                                L"Requested %lu x %lu; Remote Desktop negotiated %lu x %lu. "
                                L"The child remains logged on; inspect the preview before recreating it.",
                                State->Info.Width, State->Info.Height, State->Dialog->State.DesktopWidth,
                                State->Dialog->State.DesktopHeight);
                    SetState(State, ChildWorkerFailed, HRESULT_FROM_WIN32(ERROR_INVALID_DATA), Detail);
                    NtSetEvent(State->Ready, NULL);
                } else
                {
                    RtlAcquireSRWLockExclusive(&State->Lock);
                    State->Phase = ChildReady;
                    Str_CopyW(State->Info.Instance, Instance);
                    Str_CopyW(State->Info.State, ChildPhaseNames[ChildReady]);
                    State->Info.LastError = S_OK;
                    State->Info.Detail[0] = UNICODE_NULL;
                    RtlReleaseSRWLockExclusive(&State->Lock);
                    Deadline = 0;
                    NtSetEvent(State->Ready, NULL);
                }
            } else if (ReadPhase(State) == ChildReady)
            {
                // A slow probe does not establish that the Worker exited.
                if (Deadline == 0)
                {
                    Deadline = Now + CHILD_STARTUP_TIMEOUT;
                }
                if (Now >= Deadline)
                {
                    SetState(State, ChildWorkerFailed,
                             FAILED(ProbeResult) ? ProbeResult : HRESULT_FROM_WIN32(ERROR_TIMEOUT),
                             L"The Worker is unavailable. The child remains logged on.");
                    NtSetEvent(State->Ready, NULL);
                }
            } else if (FAILED(ProbeResult) && Server == NULL &&
                       ProbeResult == HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE))
            {
                HRESULT LaunchResult;
                if (Attempts >= CHILD_WORKER_ATTEMPTS)
                {
                    LaunchResult = HRESULT_FROM_WIN32(ERROR_PROCESS_ABORTED);
                    FailureText = L"The Worker repeatedly exited during startup. The child remains logged on.";
                } else
                {
                    LaunchResult = LaunchServer(State->Info.Id, &Server, &FailureText);
                    ++Attempts;
                }
                if (FAILED(LaunchResult) &&
                    (LaunchResult != HRESULT_FROM_WIN32(ERROR_NO_TOKEN) || Attempts >= CHILD_WORKER_ATTEMPTS))
                {
                    SetState(State, ChildWorkerFailed, LaunchResult, FailureText);
                    NtSetEvent(State->Ready, NULL);
                }
            }
            NextAttempt = Now + (SUCCEEDED(ProbeResult) || ReadPhase(State) == ChildWorkerFailed ? 1000 : 250);
        }
        if (ReadPhase(State) != ChildReady && ReadPhase(State) != ChildWorkerFailed &&
            ReadPhase(State) != ChildAwaitingUser && Deadline != 0 && Now >= Deadline)
        {
            if (State->LoggedIn == FALSE)
            {
                Result = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
                FailureText = L"The Remote Desktop connection did not complete.";
                break;
            }
            SetState(State, ChildWorkerFailed, HRESULT_FROM_WIN32(ERROR_TIMEOUT),
                     L"The child desktop or Worker did not become ready. The child remains logged on.");
            NtSetEvent(State->Ready, NULL);
        }
        HANDLE Events[] = { State->Stop, State->Recover };
        MsgWaitForMultipleObjects(ARRAYSIZE(Events), Events, FALSE, 100, QS_ALLINPUT);
    }
    if (State->ConnectionStarted != FALSE)
    {
        AdoptChild(State);
    }
    DWORD ShutdownTimeout;
    RtlAcquireSRWLockShared(&State->Lock);
    ShutdownTimeout = State->ShutdownTimeout;
    RtlReleaseSRWLockShared(&State->Lock);
    HRESULT Cleanup = LogoffOwnedChild(State->Info.Owned != FALSE ? State->Info.Id : MAXDWORD,
        State->ParentId, ShutdownTimeout);
    if (State->Dialog != NULL)
    {
        DestroyWindow(State->Dialog->Window);
        State->Dialog = NULL;
    }
    if (Initialized != FALSE)
    {
        OleUninitialize();
    }
    if (SlotOwned != FALSE)
    {
        NtReleaseMutant(Slot, NULL);
    }
    if (SUCCEEDED(Result) && FAILED(Cleanup))
    {
        Result = Cleanup;
        FailureText = L"The child-session logoff could not be confirmed.";
    }
    if (FAILED(Result))
    {
        WCHAR Detail[512];
        Str_PrintfW(Detail, L"%ls RDP code: %ld", FailureText, State->RdpCode);
        SetState(State, FAILED(Cleanup) ? ChildDestroyFailed : ChildFailed, Result, Detail);
    } else
    {
        SetState(State, ChildClosed, S_OK, L"");
    }
    if (Server != NULL)
    {
        NtClose(Server);
    }
    if (Slot != NULL)
    {
        NtClose(Slot);
    }
    NtSetEvent(State->Ready, NULL);
    return STATUS_SUCCESS;
}

static
VOID
ReleaseChild(
    _In_ CHILD_STATE* Child)
{
    if (_InterlockedDecrement(&Child->References) != 0)
    {
        return;
    }
    if (Child->Thread != NULL)
    {
        NtClose(Child->Thread);
    }
    if (Child->Ready != NULL)
    {
        NtClose(Child->Ready);
    }
    if (Child->Stop != NULL)
    {
        NtClose(Child->Stop);
    }
    if (Child->Recover != NULL)
    {
        NtClose(Child->Recover);
    }
    Mem_Free(Child);
}

static
VOID
RemoveEndedChildren(
    _Inout_ CUA_SESSION_MANAGER* Manager)
{
    CHILD_STATE* Child;
    DWORD Id;
    HRESULT Hr;

    for (PLIST_ENTRY Entry = Manager->Children.Flink; Entry != &Manager->Children;)
    {
        Child = CONTAINING_RECORD(Entry, CHILD_STATE, Entry);
        Entry = Entry->Flink;
        if (PS_WaitForObject(Child->Thread, 0) != STATUS_WAIT_0)
        {
            continue;
        }
        if (ReadPhase(Child) == ChildDestroyFailed)
        {
            Hr = ChildId(&Id);
            if ((FAILED(Hr) && Hr != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) ||
                (SUCCEEDED(Hr) && Id == Child->Info.Id))
            {
                continue;
            }
        }
        RemoveEntryList(&Child->Entry);
        ReleaseChild(Child);
    }
}

static
CHILD_STATE*
FindChild(
    _In_ CUA_SESSION_MANAGER* Manager,
    _In_ DWORD Id,
    _In_ LOGICAL Live)
{
    for (PLIST_ENTRY Entry = Manager->Children.Flink; Entry != &Manager->Children; Entry = Entry->Flink)
    {
        CHILD_STATE* Child = CONTAINING_RECORD(Entry, CHILD_STATE, Entry);
        RtlAcquireSRWLockShared(&Child->Lock);
        LOGICAL Found = Child->Info.Owned != FALSE && Child->Info.Id == Id;
        RtlReleaseSRWLockShared(&Child->Lock);
        if (Found != FALSE && (Live == FALSE || PS_WaitForObject(Child->Thread, 0) != STATUS_WAIT_0))
        {
            _InterlockedIncrement(&Child->References);
            return Child;
        }
    }
    return NULL;
}

HRESULT
CuaSessionsCreate(
    _In_ CUA_SESSION_PROBE Probe,
    _In_ PVOID Context,
    _Outptr_ CUA_SESSION_MANAGER** Manager)
{
    *Manager = (CUA_SESSION_MANAGER*)Mem_Alloc(sizeof(CUA_SESSION_MANAGER));
    if (*Manager == NULL)
    {
        return E_OUTOFMEMORY;
    }
    RtlZeroMemory(*Manager, sizeof(**Manager));
    (*Manager)->Probe = Probe;
    (*Manager)->ProbeContext = Context;
    InitializeListHead(&(*Manager)->Children);
    RtlInitializeSRWLock(&(*Manager)->Lock);
    return S_OK;
}

VOID
CuaSessionsDestroy(
    _In_opt_ CUA_SESSION_MANAGER* Manager)
{
    if (Manager == NULL)
    {
        return;
    }
    for (PLIST_ENTRY Entry = Manager->Children.Flink; Entry != &Manager->Children; Entry = Entry->Flink)
    {
        CHILD_STATE* Child = CONTAINING_RECORD(Entry, CHILD_STATE, Entry);
        RtlAcquireSRWLockExclusive(&Child->Lock);
        Child->ShutdownTimeout = 3000;
        RtlReleaseSRWLockExclusive(&Child->Lock);
        NtSetEvent(Child->Stop, NULL);
    }
    while (IsListEmpty(&Manager->Children) == FALSE)
    {
        CHILD_STATE* Child = CONTAINING_RECORD(RemoveHeadList(&Manager->Children), CHILD_STATE, Entry);
        PS_WaitForObject(Child->Thread, INFINITE);
        ReleaseChild(Child);
    }
    Mem_Free(Manager);
}

_Success_(return == S_OK)
HRESULT
CuaSessionsList(
    _In_ CUA_SESSION_MANAGER* Manager,
    _Outptr_result_buffer_(*Count) CUA_SESSION_INFO** Items,
    _Out_ PULONG Count,
    _Out_writes_(512) PWSTR Detail)
{
    PWTS_SESSION_INFOW Sessions = NULL;
    DWORD SessionCount = 0;
    const ULONG MaximumCapacity = (ULONG)(MAXULONG / sizeof(CUA_SESSION_INFO));
    ULONG Capacity, Used = 0;
    CUA_SESSION_INFO* Results = NULL;
    HRESULT Hr = S_OK;
    *Items = NULL;
    *Count = 0;
    Detail[0] = UNICODE_NULL;
    if (WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &Sessions, &SessionCount) == FALSE)
    {
        return Failure(HRESULT_FROM_WIN32(Err_GetLastError()), Detail, L"Unable to enumerate Windows sessions.");
    }
    RtlAcquireSRWLockExclusive(&Manager->Lock);
    RemoveEndedChildren(Manager);
    Capacity = SessionCount;
    if (Capacity > MaximumCapacity)
    {
        Hr = E_OUTOFMEMORY;
    }
    for (PLIST_ENTRY Entry = Manager->Children.Flink;
         SUCCEEDED(Hr) && Entry != &Manager->Children;
         Entry = Entry->Flink)
    {
        if (Capacity == MaximumCapacity)
        {
            Hr = E_OUTOFMEMORY;
            break;
        }
        ++Capacity;
    }
    if (Capacity == 0)
    {
        Capacity = 1;
    }
    if (SUCCEEDED(Hr))
    {
        const SIZE_T Bytes = (SIZE_T)Capacity * sizeof(CUA_SESSION_INFO);
        Results = (CUA_SESSION_INFO*)Mem_Alloc(Bytes);
        if (Results == NULL)
        {
            Hr = E_OUTOFMEMORY;
        } else
        {
            RtlZeroMemory(Results, Bytes);
            for (PLIST_ENTRY Entry = Manager->Children.Flink; Entry != &Manager->Children; Entry = Entry->Flink)
            {
                if (Used >= Capacity)
                {
                    Hr = E_UNEXPECTED;
                    break;
                }
                CHILD_STATE* Child = CONTAINING_RECORD(Entry, CHILD_STATE, Entry);
                ReadChild(Child, &Results[Used]);
                ++Used;
            }
        }
    }
    RtlReleaseSRWLockExclusive(&Manager->Lock);
    for (DWORD Index = 0; SUCCEEDED(Hr) && Index < SessionCount; ++Index)
    {
        if (Sessions[Index].SessionId == 0 || Sessions[Index].State == WTSListen)
        {
            continue;
        }
        ULONG ChildIndex;
        for (ChildIndex = 0; ChildIndex < Used && ChildIndex < Capacity; ++ChildIndex)
        {
            if (Results[ChildIndex].Id == Sessions[Index].SessionId)
            {
                break;
            }
        }
        if (ChildIndex != Used)
        {
            continue;
        }
        if (Used >= Capacity)
        {
            Hr = E_UNEXPECTED;
            break;
        }
        CUA_SESSION_INFO* Info = &Results[Used];
        ++Used;
        Info->Id = Sessions[Index].SessionId;
        Str_CopyW(Info->State, Sessions[Index].State == WTSActive ? L"available" : L"disconnected");
        Info->LastError = Manager->Probe(Manager->ProbeContext, Info->Id, 25, Info->Instance, NULL);
        if (Info->LastError == S_OK && Info->Instance[0] != UNICODE_NULL)
        {
            Str_CopyW(Info->State, L"ready");
        } else if (Info->LastError == S_FALSE)
        {
            Info->Instance[0] = UNICODE_NULL;
            Str_CopyW(Info->State, L"starting");
        }
    }
    WTSFreeMemory(Sessions);
    if (FAILED(Hr))
    {
        Mem_Free(Results);
    } else
    {
        *Items = Results;
        *Count = Used;
    }
    return Hr;
}

HRESULT
CuaSessionCreateChild(
    _In_ CUA_SESSION_MANAGER* Manager,
    _In_ DWORD Width,
    _In_ DWORD Height,
    _In_ LOGICAL Show,
    _Out_ CUA_SESSION_INFO* Info,
    _Out_writes_(512) PWSTR Detail)
{
    BOOLEAN Enabled;
    WINSTATIONINFORMATION Parent = { 0 };
    ULONG Returned;
    CHILD_STATE* Child = NULL;
    HRESULT Hr = S_OK;
    RtlZeroMemory(Info, sizeof(*Info));
    Info->Id = MAXDWORD;
    Detail[0] = UNICODE_NULL;
    if (Width < 640 || Height < 480 || Width > 7680 || Height > 4320)
    {
        return Failure(E_INVALIDARG, Detail, L"Use width 640..7680 and height 480..4320.");
    }
    RtlAcquireSRWLockExclusive(&Manager->Lock);
    RemoveEndedChildren(Manager);
    for (PLIST_ENTRY Entry = Manager->Children.Flink; Entry != &Manager->Children; Entry = Entry->Flink)
    {
        CHILD_STATE* Existing = CONTAINING_RECORD(Entry, CHILD_STATE, Entry);
        ReadChild(Existing, Info);
        if (PS_WaitForObject(Existing->Thread, 0) == STATUS_WAIT_0)
        {
            if (ReadPhase(Existing) == ChildDestroyFailed)
            {
                Hr = Failure(FAILED(Info->LastError) ? Info->LastError : HRESULT_FROM_WIN32(ERROR_INVALID_STATE),
                             Detail, L"The retained child logoff is unconfirmed. Use CUA Session List and Destroy.");
                goto Unlock;
            }
            continue;
        }
        if (Info->Width != Width || Info->Height != Height)
        {
            Hr = Failure(HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS), Detail,
                         L"A child is retained at a different resolution. Use CUA Session List and reuse it.");
            goto Unlock;
        }
        Child = Existing;
        _InterlockedIncrement(&Child->References);
        RtlAcquireSRWLockExclusive(&Child->Lock);
        if (Child->Phase == ChildWorkerFailed || Child->Phase == ChildReady)
        {
            Child->Phase = ChildStarting;
            Str_CopyW(Child->Info.State, ChildPhaseNames[ChildStarting]);
            Child->Info.LastError = S_OK;
            Str_CopyW(Child->Info.Detail, L"Checking the retained child Worker.");
            NtResetEvent(Child->Ready, NULL);
            NtSetEvent(Child->Recover, NULL);
        }
        RtlReleaseSRWLockExclusive(&Child->Lock);
        goto Unlock;
    }
    if (WinStationIsChildSessionsEnabled(&Enabled) == FALSE)
    {
        Hr = Failure(HRESULT_FROM_WIN32(Err_GetLastError()), Detail, L"Unable to query child-session support.");
        goto Unlock;
    }
    if (Enabled == FALSE)
    {
        Hr = Failure(
            HRESULT_FROM_WIN32(ERROR_SERVICE_DISABLED), Detail,
            L"Child sessions are disabled. Optionally call CUA Session EnableChildSession, then retry CreateChild.");
        goto Unlock;
    }
    Child = (CHILD_STATE*)Mem_Alloc(sizeof(*Child));
    if (Child == NULL)
    {
        Hr = E_OUTOFMEMORY;
        goto Unlock;
    }
    RtlZeroMemory(Child, sizeof(*Child));
    RtlInitializeSRWLock(&Child->Lock);
    Child->References = 1;
    Child->Manager = Manager;
    Child->Info.Id = MAXDWORD;
    Child->Info.Width = Width;
    Child->Info.Height = Height;
    Child->Info.Owned = TRUE;
    Child->Show = Show;
    Child->ShutdownTimeout = 10000;
    SetState(Child, ChildConnecting, S_OK,
             L"Connecting; ask the user to complete any Windows credential prompt in the parent session.");
    Child->ParentId = NtCurrentPeb()->SessionId;
    if (WinStationQueryInformationW(WINSTATION_CURRENT_SERVER, Child->ParentId, WinStationInformation, &Parent,
                                    sizeof(Parent), &Returned) == FALSE)
    {
        Hr = Failure(HRESULT_FROM_WIN32(Err_GetLastError()), Detail, L"Unable to inspect the parent session.");
        goto Unlock;
    }
    if (Parent.ConnectState != State_Active || Parent.UserName[0] == UNICODE_NULL)
    {
        Hr = Failure(HRESULT_FROM_WIN32(ERROR_NOT_LOGGED_ON), Detail, L"The parent must be active and logged on.");
        goto Unlock;
    }
    Hr = Err_NtStatusToHr(NtCreateEvent(&Child->Ready, EVENT_ALL_ACCESS,
                                     (POBJECT_ATTRIBUTES)&NT_EmptyObjectAttribute, NotificationEvent, FALSE));
    if (FAILED(Hr))
    {
        goto Unlock;
    }
    Hr = Err_NtStatusToHr(NtCreateEvent(&Child->Stop, EVENT_ALL_ACCESS,
                                     (POBJECT_ATTRIBUTES)&NT_EmptyObjectAttribute, NotificationEvent, FALSE));
    if (FAILED(Hr))
    {
        goto Unlock;
    }
    Hr = Err_NtStatusToHr(NtCreateEvent(&Child->Recover, EVENT_ALL_ACCESS,
                                     (POBJECT_ATTRIBUTES)&NT_EmptyObjectAttribute, NotificationEvent, FALSE));
    if (FAILED(Hr))
    {
        goto Unlock;
    }
    Hr = Err_NtStatusToHr(PS_CreateThread(NtCurrentProcess(), FALSE, ChildThread, Child, &Child->Thread, NULL));
    if (FAILED(Hr))
    {
        goto Unlock;
    }
    _InterlockedIncrement(&Child->References);
    InsertTailList(&Manager->Children, &Child->Entry);
Unlock:
    RtlReleaseSRWLockExclusive(&Manager->Lock);
    if (SUCCEEDED(Hr) && Child != NULL)
    {
        for (;;)
        {
            if (RpcServerTestCancel(NULL) == RPC_S_OK)
            {
                Hr = Failure(HRESULT_FROM_WIN32(ERROR_CANCELLED), Detail,
                             L"Creation wait was cancelled; the child connection is retained.");
                break;
            }
            NTSTATUS Wait = PS_WaitForObject(Child->Ready, 250);
            if (Wait != STATUS_WAIT_0 && Wait != STATUS_TIMEOUT)
            {
                Hr = Failure(NT_SUCCESS(Wait) ? E_UNEXPECTED : Err_NtStatusToHr(Wait),
                             Detail, L"Unable to wait for child startup.");
                break;
            }
            RtlAcquireSRWLockExclusive(&Child->Lock);
            *Info = Child->Info;
            CHILD_PHASE Phase = Child->Phase;
            LOGICAL RecoverPending = PS_WaitForObject(Child->Recover, 0) == STATUS_WAIT_0;
            if (RecoverPending != FALSE ||
                Phase == ChildConnecting || Phase == ChildAwaitingUser || Phase == ChildStarting)
            {
                NtResetEvent(Child->Ready, NULL);
            }
            RtlReleaseSRWLockExclusive(&Child->Lock);
            if (RecoverPending == FALSE && Phase == ChildReady &&
                Info->Id != MAXDWORD && Info->Instance[0] != UNICODE_NULL)
            {
                Hr = S_OK;
                break;
            }
            if ((Phase == ChildWorkerFailed && RecoverPending == FALSE) ||
                Phase == ChildFailed || Phase == ChildClosed || Phase == ChildDestroyFailed)
            {
                Hr = Failure(FAILED(Info->LastError) ? Info->LastError : HRESULT_FROM_WIN32(ERROR_CANCELLED),
                             Detail, Info->Detail);
                break;
            }
        }
    }
    if (Child != NULL)
    {
        ReleaseChild(Child);
    }
    return Hr;
}

HRESULT
CuaSessionPreview(
    _In_ CUA_SESSION_MANAGER* Manager,
    _In_ DWORD Id,
    _In_ LOGICAL Show,
    _Out_writes_(512) PWSTR Detail)
{
    HWND Window;
    DWORD_PTR Applied = 0;
    HRESULT Hr;
    Detail[0] = UNICODE_NULL;
    if (Id == 0 || Id == MAXDWORD)
    {
        return E_INVALIDARG;
    }
    RtlAcquireSRWLockShared(&Manager->Lock);
    CHILD_STATE* Child = FindChild(Manager, Id, TRUE);
    RtlReleaseSRWLockShared(&Manager->Lock);
    if (Child == NULL)
    {
        return Failure(E_ACCESSDENIED, Detail, L"Preview requires a child session managed by this Server.");
    }
    RtlAcquireSRWLockShared(&Child->Lock);
    Window = Child->Preview;
    RtlReleaseSRWLockShared(&Child->Lock);
    if (Window == NULL)
    {
        Hr = Failure(E_PENDING, Detail, L"The preview is not ready; use CUA Session List.");
    } else
    {
        W32ERROR Error = UI_SendMessageTimeout(Window, WM_CUA_PREVIEW, Show, (LPARAM)Child,
            SMTO_ABORTIFHUNG | SMTO_BLOCK | SMTO_ERRORONEXIT, 1000, &Applied);
        if (Error != ERROR_SUCCESS)
        {
            Hr = Failure(HRESULT_FROM_WIN32(Error), Detail,
                         L"Preview update was not confirmed; use CUA Session List before retrying.");
        } else if (Applied == 0)
        {
            Hr = Failure(HRESULT_FROM_WIN32(ERROR_INVALID_STATE), Detail, L"The preview is closing.");
        } else
        {
            Hr = S_OK;
        }
    }
    ReleaseChild(Child);
    return Hr;
}

HRESULT
CuaSessionDestroy(
    _In_ CUA_SESSION_MANAGER* Manager,
    _In_ DWORD Id,
    _In_ DWORD Timeout,
    _Out_writes_(512) PWSTR Detail)
{
    HRESULT Hr;
    Detail[0] = UNICODE_NULL;
    if (Id == 0 || Id == MAXDWORD || Timeout < 100 || Timeout > 10000)
    {
        return E_INVALIDARG;
    }
    RtlAcquireSRWLockShared(&Manager->Lock);
    CHILD_STATE* Child = FindChild(Manager, Id, FALSE);
    RtlReleaseSRWLockShared(&Manager->Lock);
    if (Child == NULL)
    {
        return Failure(E_ACCESSDENIED, Detail, L"Destroy requires a child created by this Server.");
    }
    RtlAcquireSRWLockExclusive(&Child->Lock);
    Child->ShutdownTimeout = Timeout;
    RtlReleaseSRWLockExclusive(&Child->Lock);
    NtSetEvent(Child->Stop, NULL);
    NTSTATUS Wait = PS_WaitForObject(Child->Thread, Timeout);
    if (Wait != STATUS_WAIT_0)
    {
        Hr = Failure(Wait == STATUS_TIMEOUT ? HRESULT_FROM_WIN32(ERROR_TIMEOUT) :
                     NT_SUCCESS(Wait) ? E_UNEXPECTED : Err_NtStatusToHr(Wait),
                     Detail, L"Session destruction is pending; do not assume the session has ended.");
    } else
    {
        CUA_SESSION_INFO Info;
        ReadChild(Child, &Info);
        Hr = S_OK;
        if (ReadPhase(Child) == ChildDestroyFailed)
        {
            Hr = LogoffOwnedChild(Info.Id, Child->ParentId, Timeout);
            if (FAILED(Hr))
            {
                Str_CopyExW(Detail, 512, L"Unable to confirm child-session logoff.");
            }
        }
        if (SUCCEEDED(Hr))
        {
            SetState(Child, ChildClosed, S_OK, L"");
        }
    }
    ReleaseChild(Child);
    return Hr;
}
