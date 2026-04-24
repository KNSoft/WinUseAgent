#include "pch.h"

#define IBM_CCSID_UTF16 1200

static
_Success_(return != FALSE)
LOGICAL
QueryProductName(
    _In_ PVOID VersionInfo,
    _In_ WORD Language,
    _In_ WORD CodePage,
    _Out_writes_(BufferCch) PWSTR Buffer,
    _In_ ULONG BufferCch)
{
    UINT u;
    WCHAR szSubBlock[64];
    PWSTR pszProductName;

    if (Str_PrintfW(szSubBlock,
                    L"\\StringFileInfo\\%04x%04x\\ProductName",
                    Language,
                    CodePage) == 0)
    {
        return FALSE;
    }
    return VerQueryValueW(VersionInfo, szSubBlock, reinterpret_cast<PVOID*>(&pszProductName), &u) &&
        pszProductName != NULL &&
        u != 0 &&
        wcscpy_s(Buffer, BufferCch, pszProductName) == 0;
}

_Success_(return != FALSE)
LOGICAL
Util_Proc_GetProductName(
    _In_ PCWSTR File,
    _Out_writes_(BufferCch) PWSTR Buffer,
    _In_ ULONG BufferCch)
{
    DWORD dw;
    UINT u;
    PVOID pInfo;
    struct
    {
        WORD Language;
        WORD CodePage;
    } *pTranslation;
    ULONG i, uTranslationCount;
    WORD wLanguage;

    if (BufferCch == 0)
    {
        return FALSE;
    }
    /* lpdwHandle can be NULL, wrong SAL annotation in Windows SDK */
#pragma warning(disable: 6387)
    dw = GetFileVersionInfoSizeExW(FILE_VER_GET_LOCALISED, File, NULL);
#pragma warning(default: 6387)
    if (dw == 0)
    {
        return FALSE;
    }
    pInfo = Mem_Alloc(dw);
    if (pInfo == NULL)
    {
        return FALSE;
    }
    if (!GetFileVersionInfoExW(FILE_VER_GET_LOCALISED | FILE_VER_GET_PREFETCHED, File, 0, dw, pInfo))
    {
        Mem_Free(pInfo);
        return FALSE;
    }
    if (!VerQueryValueW(pInfo, L"\\VarFileInfo\\Translation", reinterpret_cast<PVOID*>(&pTranslation), &u))
    {
        Mem_Free(pInfo);
        return FALSE;
    }
    uTranslationCount = u / sizeof(*pTranslation);
    wLanguage = GetUserDefaultUILanguage();
    for (i = 0; i < uTranslationCount; i++)
    {
        if (pTranslation[i].Language == wLanguage &&
            QueryProductName(pInfo, pTranslation[i].Language, pTranslation[i].CodePage, Buffer, BufferCch))
        {
            Mem_Free(pInfo);
            return TRUE;
        }
    }
    if (QueryProductName(pInfo,
                         MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                         IBM_CCSID_UTF16,
                         Buffer,
                         BufferCch) ||
        QueryProductName(pInfo,
                         MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                         IBM_CCSID_UTF16,
                         Buffer,
                         BufferCch) ||
        (uTranslationCount != 0 &&
         QueryProductName(pInfo, pTranslation[0].Language, pTranslation[0].CodePage, Buffer, BufferCch)))
    {
        Mem_Free(pInfo);
        return TRUE;
    }

    Mem_Free(pInfo);
    return FALSE;
}
