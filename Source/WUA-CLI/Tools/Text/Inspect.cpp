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
    } else
    {
        cJSON_AddNullToObject(j, "bom");
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
    DWORD Error;
    HANDLE hFile;
    HANDLE hMapping;
    LARGE_INTEGER FileSize;
    LOGICAL HasPreviousByte;
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

    hFile = CreateFileW(File,
                        GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        Error = GetLastError();
        return BuildErrorOutput(HRESULT_FROM_WIN32(Error), "CreateFileW failed with error %lu.", Error);
    }

    if (!GetFileSizeEx(hFile, &FileSize))
    {
        Error = GetLastError();
        CloseHandle(hFile);
        return BuildErrorOutput(HRESULT_FROM_WIN32(Error), "GetFileSizeEx failed with error %lu.", Error);
    }

    hMapping = NULL;
    Data = NULL;
    if (FileSize.QuadPart != 0)
    {
        hMapping = CreateFileMappingW(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
        if (hMapping == NULL)
        {
            Error = GetLastError();
            CloseHandle(hFile);
            return BuildErrorOutput(HRESULT_FROM_WIN32(Error), "CreateFileMappingW failed with error %lu.", Error);
        }

        Data = reinterpret_cast<const BYTE*>(MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0));
        if (Data == NULL)
        {
            Error = GetLastError();
            CloseHandle(hMapping);
            CloseHandle(hFile);
            return BuildErrorOutput(HRESULT_FROM_WIN32(Error), "MapViewOfFile failed with error %lu.", Error);
        }
    }

    HasPreviousByte = FALSE;
    PreviousByte = 0;
    BytesRead = 0;
    CrLf = 0;
    CrOnly = 0;
    LfOnly = 0;
    for (LONGLONG i = 0; i < FileSize.QuadPart; i++)
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
    BytesRead = FileSize.QuadPart;

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

    CloseHandle(hFile);

    j = cJSON_CreateObject();
    Util_Json_AddUnicodeString(j, "file", File, 0);
    cJSON_AddNumberToObject(j, "size", (DOUBLE)FileSize.QuadPart);
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
        UnmapViewOfFile(Data);
    }
    if (hMapping != NULL)
    {
        CloseHandle(hMapping);
    }
    return BuildSuccessOutput(j);
}
