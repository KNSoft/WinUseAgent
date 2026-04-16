#include "pch.h"

static ULONG Handle;
static PWSTR OutFile;

static WUA_COMMAND_PARAMETER Parameters[] = {
    DEF_PARAMETER_ENTRY(Handle, HexU32, FALSE),
    DEF_PARAMETER_ENTRY(OutFile, String, TRUE),
};

WUA_COMMAND_FN Command;
WUA_COMMAND Window_Capture = { Parameters, ARRAYSIZE(Parameters), &Command };

static
VOID
CaptureChildren(
    _In_ IUIAutomation* UIA,
    _In_ IUIAutomationElement* Element,
    _In_ cJSON* j)
{
    IUIAutomationTreeWalker* Walker;
    IUIAutomationElement *Child, *NextChild;
    cJSON* jChildren;

    if (FAILED(UIA->get_ControlViewWalker(&Walker)))
    {
        return;
    }

    Child = NULL;
    if (FAILED(Walker->GetFirstChildElement(Element, &Child)) || Child == NULL)
    {
        Walker->Release();
        return;
    }

    jChildren = cJSON_CreateArray();
    do
    {
        cJSON* jChild = Utils_UIA_GetInfoJson(Child);
        if (jChild != NULL)
        {
            CaptureChildren(UIA, Child, jChild);
            cJSON_AddItemToArray(jChildren, jChild);
        }

        NextChild = NULL;
        Walker->GetNextSiblingElement(Child, &NextChild);
        Child->Release();
        Child = NextChild;
    } while (Child != NULL);

    if (cJSON_GetArraySize(jChildren) > 0)
    {
        cJSON_AddItemToObject(j, "children", jChildren);
    } else
    {
        cJSON_Delete(jChildren);
    }

    Walker->Release();
}

static
_Function_class_(WUA_COMMAND_FN)
_Ret_notnull_
cJSON*
Command(VOID)
{
    cJSON* j;
    cJSON* jResult;
    HRESULT Hr;
    HWND hWnd;
    UI_SNAPSHOT Snapshot;
    Gdiplus::Status Status;
    Gdiplus::Bitmap* Bitmap;
    IUIAutomation* UIA;
    IUIAutomationElement* Element;

    /* Verify parameters */
    hWnd = reinterpret_cast<HWND>(UI_32ToHandle(Handle));
    if (hWnd != NULL && !IsWindow(hWnd))
    {
        return BuildErrorOutput(E_INVALIDARG, "Parameter \"Handle\" is not a valid window handle.");
    }
    if (OutFile == NULL || *OutFile == UNICODE_NULL)
    {
        return BuildErrorOutput(E_INVALIDARG, "Parameter \"OutFile\" is required.");
    }

    /* Capture snapshot */
    if (!UI_CreateSnapshot(hWnd, &Snapshot))
    {
        return BuildErrorOutput(E_FAIL, "Failed to create snapshot for specified window.");
    }
    j = Util_GdipStartup();
    if (j != NULL)
    {
        goto _Exit_0;
    }
    Bitmap = new Gdiplus::Bitmap(Snapshot.Bitmap, NULL);
    Status = Bitmap->GetLastStatus();
    if (Status != Gdiplus::Ok)
    {
        j = BuildErrorOutput(UI_GdipStatusToHr(Status), "Create Gdiplus::Bitmap failed with GDI+ status %ld", Status);
        goto _Exit_1;
    }
    Status = UI_GdipSaveImageToFileEx(dynamic_cast<Gdiplus::Image*>(Bitmap), OutFile, Gdiplus::ImageFormatPNG, NULL);
    if (Status != Gdiplus::Ok)
    {
        j = BuildErrorOutput(UI_GdipStatusToHr(Status), "Save image failed with GDI+ status %ld", Status);
        goto _Exit_2;
    }
    jResult = NULL;
    if (hWnd == NULL)
    {
        goto _Exit_3;
    }

    /* Get UIA information if target is a top-level window */
    Hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if ((SUCCEEDED(Hr) || Hr == RPC_E_CHANGED_MODE) &&
        (UIA = Util_UIA_CreateInstance()) != NULL)
    {
        if (SUCCEEDED(UIA->ElementFromHandle(hWnd, &Element)))
        {
            jResult = Utils_UIA_GetInfoJson(Element);
            if (jResult != NULL)
            {
                CaptureChildren(UIA, Element, jResult);
            }
            Element->Release();
        }
        UIA->Release();
    }
    if (SUCCEEDED(Hr))
    {
        CoUninitialize();
    }

_Exit_3:
    j = BuildSuccessOutput(jResult);
_Exit_2:
    delete Bitmap;
_Exit_1:
    Util_GdipShutdown();
_Exit_0:
    UI_DeleteSnapshot(&Snapshot);
    return j;
}
