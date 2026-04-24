#include "pch.h"

static ULONG_PTR StartupCookie;

_Success_(return == NULL)
_Ret_maybenull_
cJSON*
Util_Gdip_Startup(VOID)
{
    Gdiplus::Status Status;

    Status = UI_GdipStartup(1, &StartupCookie);
    if (Status != Gdiplus::Ok)
    {
        return BuildErrorOutput(UI_GdipStatusToHr(Status), "GDI+ initialization failed with status %ld", Status);
    }

    return NULL;
}

VOID
Util_Gdip_Shutdown(VOID)
{
    UI_GdipShutdown(StartupCookie);
}

_Success_(return == NULL)
_Ret_maybenull_
cJSON*
Util_Gdip_SaveSnapshot(
    _In_opt_ HWND hWnd,
    _In_ PCWSTR pszFile)
{
    cJSON *j;
    UI_SNAPSHOT Snapshot;
    Gdiplus::Status Status;
    Gdiplus::Bitmap* Bitmap;

    if (!UI_CreateSnapshot(hWnd, &Snapshot))
    {
        return BuildErrorOutput(E_FAIL, "Failed to create snapshot for specified window.");
    }
    j = Util_Gdip_Startup();
    if (j != NULL)
    {
        goto _Exit_0;
    }
    Bitmap = new Gdiplus::Bitmap(Snapshot.Bitmap, NULL);
    if (Bitmap == NULL)
    {
        goto _Exit_1;
    }
    Status = Bitmap->GetLastStatus();
    if (Status != Gdiplus::Ok)
    {
        j = BuildErrorOutput(UI_GdipStatusToHr(Status), "Create Gdiplus::Bitmap failed with GDI+ status %ld", Status);
        goto _Exit_2;
    }
    Status = UI_GdipSaveImageToFileEx(dynamic_cast<Gdiplus::Image*>(Bitmap), pszFile, Gdiplus::ImageFormatPNG, NULL);
    if (Status != Gdiplus::Ok)
    {
        j = BuildErrorOutput(UI_GdipStatusToHr(Status), "Save image failed with GDI+ status %ld", Status);
    }
_Exit_2:
    delete Bitmap;
_Exit_1:
    Util_Gdip_Shutdown();
_Exit_0:
    UI_DeleteSnapshot(&Snapshot);
    return j;
}
