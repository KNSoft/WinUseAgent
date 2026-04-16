#include "pch.h"

static ULONG_PTR StartupCookie;

_Success_(return == NULL)
_Ret_maybenull_
cJSON*
Util_GdipStartup(VOID)
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
Util_GdipShutdown(VOID)
{
    UI_GdipShutdown(StartupCookie);
}
