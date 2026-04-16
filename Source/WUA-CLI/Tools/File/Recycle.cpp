#include "pch.h"

static PWSTR File;

static WUA_COMMAND_PARAMETER Parameters[] = {
    DEF_PARAMETER_ENTRY(File, String, TRUE)
};

WUA_COMMAND_FN Command;
WUA_COMMAND File_Recycle = { Parameters, ARRAYSIZE(Parameters), &Command };

static
_Function_class_(WUA_COMMAND_FN)
_Ret_notnull_
cJSON*
Command(VOID)
{
    SIZE_T cchFile;
    INT Result;
    PWSTR MultiSzFile;
    SHFILEOPSTRUCTW FileOp = { 0 };

    if (File == NULL || *File == UNICODE_NULL)
    {
        return BuildErrorOutput(E_INVALIDARG, "Parameter \"File\" is required.");
    }

    cchFile = wcslen(File);
    MultiSzFile = reinterpret_cast<PWSTR>(Mem_Alloc((cchFile + 2) * sizeof(WCHAR)));
    if (MultiSzFile == NULL)
    {
        return BuildErrorOutput(E_OUTOFMEMORY, "Failed to allocate file path buffer.");
    }
    RtlCopyMemory(MultiSzFile, File, cchFile * sizeof(WCHAR));
    MultiSzFile[cchFile] = MultiSzFile[cchFile + 1] = UNICODE_NULL;
    FileOp.wFunc = FO_DELETE;
    FileOp.pFrom = MultiSzFile;
    FileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    Result = SHFileOperationW(&FileOp);
    Mem_Free(MultiSzFile);

    if (Result != 0)
    {
        return BuildErrorOutput(E_FAIL, "SHFileOperationW failed with code %d", Result);
    } else if (FileOp.fAnyOperationsAborted)
    {
        return BuildErrorOutput(HRESULT_FROM_WIN32(ERROR_CANCELLED), "SHFileOperationW operation was aborted.");
    }
    return BuildSuccessOutput(NULL);
}
