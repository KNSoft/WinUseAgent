#include "pch.h"

static PWSTR File;

static WUA_COMMAND_PARAMETER Parameters[] = {
    DEF_PARAMETER_ENTRY(File, String, TRUE)
};

WUA_COMMAND_FN Command;
WUA_COMMAND Text_Inspect = { Parameters, ARRAYSIZE(Parameters), &Command };

static
VOID
AddLineEndingName(
    _In_ cJSON* j,
    _In_ PCSTR Key,
    _In_ ULONGLONG CrLf,
    _In_ ULONGLONG LfOnly,
    _In_ ULONGLONG CrOnly)
{
    ULONG Types;

    Types = (CrLf != 0) + (LfOnly != 0) + (CrOnly != 0);
    if (Types != 1)
    {
        cJSON_AddNullToObject(j, Key);
    } else if (CrLf != 0)
    {
        cJSON_AddStringToObject(j, Key, "CRLF");
    } else if (LfOnly != 0)
    {
        cJSON_AddStringToObject(j, Key, "LF");
    } else
    {
        cJSON_AddStringToObject(j, Key, "CR");
    }
}

static
VOID
AddFinalLineEnding(
    _In_ cJSON* j,
    _In_reads_bytes_(Length) const BYTE* Buffer,
    _In_ ULONG Length)
{
    if (Length >= 2 && Buffer[Length - 2] == '\r' && Buffer[Length - 1] == '\n')
    {
        cJSON_AddStringToObject(j, "final_line_ending", "CRLF");
    } else if (Length >= 1 && Buffer[Length - 1] == '\n')
    {
        cJSON_AddStringToObject(j, "final_line_ending", "LF");
    } else if (Length >= 1 && Buffer[Length - 1] == '\r')
    {
        cJSON_AddStringToObject(j, "final_line_ending", "CR");
    } else
    {
        cJSON_AddNullToObject(j, "final_line_ending");
    }
}

static
VOID
AddBom(
    _In_ cJSON* j,
    _In_reads_bytes_(Length) const BYTE* Buffer,
    _In_ ULONG Length)
{
    if (Length >= 4 &&
        Buffer[0] == 0xFF && Buffer[1] == 0xFE && Buffer[2] == 0x00 && Buffer[3] == 0x00)
    {
        cJSON_AddStringToObject(j, "bom", "UTF-32LE");
    } else if (Length >= 4 &&
               Buffer[0] == 0x00 && Buffer[1] == 0x00 && Buffer[2] == 0xFE && Buffer[3] == 0xFF)
    {
        cJSON_AddStringToObject(j, "bom", "UTF-32BE");
    } else if (Length >= 3 &&
               Buffer[0] == 0xEF && Buffer[1] == 0xBB && Buffer[2] == 0xBF)
    {
        cJSON_AddStringToObject(j, "bom", "UTF-8");
    } else if (Length >= 2 && Buffer[0] == 0xFF && Buffer[1] == 0xFE)
    {
        cJSON_AddStringToObject(j, "bom", "UTF-16LE");
    } else if (Length >= 2 && Buffer[0] == 0xFE && Buffer[1] == 0xFF)
    {
        cJSON_AddStringToObject(j, "bom", "UTF-16BE");
    }
}

static
_Function_class_(WUA_COMMAND_FN)
_Ret_notnull_
cJSON*
Command(VOID)
{
    const BYTE* Data;
    BYTE PreviousByte;
    cJSON* j;
    IO_FILE_MAP MapInfo;
    HANDLE hFile;
    LOGICAL HasPreviousByte;
    NTSTATUS Status;
    ULONGLONG BytesRead;
    ULONGLONG CrLf;
    ULONGLONG CrOnly;
    ULONGLONG FirstBytesLength;
    ULONGLONG LastBytesLength;
    ULONGLONG LfOnly;

    if (File == NULL || *File == UNICODE_NULL)
    {
        return BuildErrorOutput(E_INVALIDARG, "Parameter \"File\" is required.");
    }

    Status = IO_CreateWin32File(&hFile,
                                File,
                                NULL,
                                FILE_READ_DATA | SYNCHRONIZE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                FILE_OPEN,
                                FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_SEQUENTIAL_ONLY);
    if (!NT_SUCCESS(Status))
    {
        return BuildErrorOutput(HRESULT_FROM_NT(Status), "IO_CreateWin32File failed with status 0x%08X.", Status);
    }

    Status = IO_GetFileSize(hFile, &BytesRead);
    if (!NT_SUCCESS(Status))
    {
        NtClose(hFile);
        return BuildErrorOutput(HRESULT_FROM_NT(Status), "IO_GetFileSize failed with status 0x%08X.", Status);
    }

    Data = NULL;
    RtlZeroMemory(&MapInfo, sizeof(MapInfo));
    if (BytesRead != 0)
    {
        Status = IO_MapReadOnlyFile(hFile, &MapInfo);
        if (!NT_SUCCESS(Status))
        {
            NtClose(hFile);
            return BuildErrorOutput(HRESULT_FROM_NT(Status), "IO_MapReadOnlyFile failed with status 0x%08X.", Status);
        }
        Data = reinterpret_cast<const BYTE*>(MapInfo.BaseAddress);
        BytesRead = MapInfo.FileSize;
    }

    HasPreviousByte = FALSE;
    PreviousByte = 0;
    CrLf = 0;
    CrOnly = 0;
    LfOnly = 0;
    for (SIZE_T i = 0; i < BytesRead; i++)
    {
        if (HasPreviousByte)
        {
            if (PreviousByte == '\r')
            {
                if (Data[i] == '\n')
                {
                    CrLf++;
                    HasPreviousByte = FALSE;
                    continue;
                } else
                {
                    CrOnly++;
                }
            } else if (PreviousByte == '\n')
            {
                LfOnly++;
            }
        }

        PreviousByte = Data[i];
        HasPreviousByte = TRUE;
    }

    if (HasPreviousByte)
    {
        if (PreviousByte == '\r')
        {
            CrOnly++;
        } else if (PreviousByte == '\n')
        {
            LfOnly++;
        }
    }

    FirstBytesLength = min((ULONGLONG)4, BytesRead);
    LastBytesLength = min((ULONGLONG)4, BytesRead);

    j = cJSON_CreateObject();
    Util_Json_AddUnicodeString(j, "file", File, 0);
    cJSON_AddNumberToObject(j, "size", (DOUBLE)BytesRead);
    cJSON_AddNumberToObject(j, "bytes_read", (DOUBLE)BytesRead);
    AddBom(j, Data, (ULONG)FirstBytesLength);
    cJSON_AddNumberToObject(j, "crlf", (DOUBLE)CrLf);
    cJSON_AddNumberToObject(j, "lf_only", (DOUBLE)LfOnly);
    cJSON_AddNumberToObject(j, "cr_only", (DOUBLE)CrOnly);
    cJSON_AddBoolToObject(j, "mixed", ((CrLf != 0) + (LfOnly != 0) + (CrOnly != 0)) > 1);
    AddLineEndingName(j, "line_ending", CrLf, LfOnly, CrOnly);
    AddFinalLineEnding(j, BytesRead == 0 ? NULL : Data + BytesRead - LastBytesLength, (ULONG)LastBytesLength);
    if (Data != NULL)
    {
        IO_UnmapFile(&MapInfo);
    }
    NtClose(hFile);
    return BuildSuccessOutput(j);
}
