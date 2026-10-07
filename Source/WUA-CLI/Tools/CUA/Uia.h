#pragma once

#define WUA_UIA_MAX_RUNTIME_ID 128
#define WUA_UIA_DEFAULT_TIMEOUT_MS 3000
#define WUA_UIA_MAX_TIMEOUT_MS 30000
#define WUA_UIA_CONNECTION_TIMEOUT_MS 1000
#define WUA_UIA_TRANSACTION_TIMEOUT_MS 2000

typedef enum _UIA_PATTERN
{
    UiaPatternInvoke = 1,
    UiaPatternValue = 2,
    UiaPatternToggle = 4,
    UiaPatternSelectionItem = 8
} UIA_PATTERN;

typedef struct _UIA_OPTIONS
{
    ULONG MaxNodes;
    ULONG MaxDepth;
    DWORD TimeoutMs;
} UIA_OPTIONS;

typedef struct _UIA_ELEMENT
{
    LONG RuntimeId[WUA_UIA_MAX_RUNTIME_ID];
    ULONG RuntimeIdCount;
    WCHAR Name[1025];
    WCHAR Role[257];
    WCHAR AutomationId[1025];
    WCHAR Text[2049];
    RECT Bounds;
    POINT ClickablePoint;
    LONG ParentIndex;
    ULONG Patterns;
    LOGICAL Enabled;
    LOGICAL Offscreen;
    LOGICAL Password;
    LOGICAL Focused;
    LOGICAL BoundsValid;
    LOGICAL ClickablePointValid;
} UIA_ELEMENT;

typedef struct _UIA_OBSERVATION
{
    UIA_ELEMENT* Elements;
    ULONG Count;
    HRESULT Status;
    LOGICAL Truncated;
} UIA_OBSERVATION;

typedef enum _UIA_ACTION
{
    UiaActionInvoke = 1,
    UiaActionSetValue,
    UiaActionToggle,
    UiaActionSelect
} UIA_ACTION;

typedef struct _UIA_CONTEXT UIA_CONTEXT;

EXTERN_C_START

HRESULT
UiaCreate(
    _Outptr_ UIA_CONTEXT** Context);

// Call after all callers have returned. Does not wait for an unresponsive provider.
VOID
UiaDestroy(
    _In_ UIA_CONTEXT* Context);

// Free Observation->Elements with Mem_Free, including when partial data accompanies a failure.
HRESULT
UiaObserve(
    _Inout_ UIA_CONTEXT* Context,
    _In_ HWND Window,
    _In_ const UIA_OPTIONS* Options,
    _Out_ UIA_OBSERVATION* Observation);

HRESULT
UiaAct(
    _Inout_ UIA_CONTEXT* Context,
    _In_ HWND Window,
    _In_reads_(RuntimeIdCount) const LONG* RuntimeId,
    _In_ ULONG RuntimeIdCount,
    _In_ UIA_ACTION Action,
    _In_ PCWSTR Value,
    _In_ DWORD TimeoutMs);

EXTERN_C_END
