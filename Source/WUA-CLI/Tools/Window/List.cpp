#include "pch.h"

WUA_COMMAND_FN Command;
WUA_COMMAND Window_List = { NULL, 0, &Command };

static
VOID
AddWindowHandleToJson(
    _In_ cJSON* j,
    _In_ PCSTR Key,
    _In_opt_ HWND hWnd)
{
    CHAR sz[32];
    if (hWnd != NULL && Utils_Window_GetHandleString(hWnd, sz, ARRAYSIZE(sz)) > 0)
    {
        cJSON_AddStringToObject(j, Key, sz);
    } else
    {
        cJSON_AddNullToObject(j, Key);
    }
}

static
_Function_class_(WUA_COMMAND_FN)
_Ret_notnull_
cJSON*
Command(VOID)
{
    GUITHREADINFO gti;
    cJSON *j, *j_Active_Info, *j_Windows, *j_Window;

    j = cJSON_CreateObject();
    gti.cbSize = sizeof(gti);
    if (GetGUIThreadInfo(0, &gti))
    {
        j_Active_Info = cJSON_CreateObject();
        AddWindowHandleToJson(j_Active_Info, "active", gti.hwndActive);
        AddWindowHandleToJson(j_Active_Info, "focus", gti.hwndFocus);
        AddWindowHandleToJson(j_Active_Info, "menu_owner", gti.hwndMenuOwner);
        AddWindowHandleToJson(j_Active_Info, "move_size", gti.hwndMoveSize);
        AddWindowHandleToJson(j_Active_Info, "caret", gti.hwndCaret);
    } else
    {
        j_Active_Info = cJSON_CreateNull();
    }
    cJSON_AddItemToObject(j, "active_info", j_Active_Info);

    j_Windows = cJSON_CreateArray();
    HWND hWnd = GetWindow(GetDesktopWindow(), GW_CHILD);
    while (hWnd != NULL)
    {
        j_Window = Utils_Window_GetInfoJson(hWnd);
        if (j_Window != NULL)
        {
            cJSON_AddItemToArray(j_Windows, j_Window);
        }
        hWnd = GetWindow(hWnd, GW_HWNDNEXT);
    }
    cJSON_AddItemToObject(j, "windows", j_Windows);

    return BuildSuccessOutput(j);
}
