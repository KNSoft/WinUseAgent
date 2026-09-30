#include "pch.h"

static PWSTR File;

static WUA_COMMAND_PARAMETER Parameters[] = {
    DEF_PARAMETER_ENTRY(File, String, TRUE)
};

WUA_COMMAND_FN Command;
WUA_COMMAND Text_Inspect = { Parameters, ARRAYSIZE(Parameters), &Command };

static
VOID
AddFinalLineEnding(
    _In_opt_ IJsonObject* j,
    _In_reads_bytes_opt_(Length) const BYTE* Buffer,
    _In_ ULONGLONG Length)
{
    if (Buffer == NULL || Length == 0)
    {
        Util_Json_AddNullToObject(j, L"final_line_ending");
    } else if (Length >= 2 && Buffer[Length - 2] == '\r' && Buffer[Length - 1] == '\n')
    {
        Util_Json_AddStringToObject(j, L"final_line_ending", "CRLF");
    } else if (Length >= 1 && Buffer[Length - 1] == '\n')
    {
        Util_Json_AddStringToObject(j, L"final_line_ending", "LF");
    } else if (Length >= 1 && Buffer[Length - 1] == '\r')
    {
        Util_Json_AddStringToObject(j, L"final_line_ending", "CR");
    } else
    {
        Util_Json_AddNullToObject(j, L"final_line_ending");
    }
}

static
VOID
AddBom(
    _In_opt_ IJsonObject* j,
    _In_reads_bytes_opt_(Length) const BYTE* Buffer,
    _In_ ULONGLONG Length)
{
    if (Buffer == NULL)
    {
        return;
    }

    if (Length >= 4 &&
        Buffer[0] == 0xFF && Buffer[1] == 0xFE && Buffer[2] == 0x00 && Buffer[3] == 0x00)
    {
        Util_Json_AddStringToObject(j, L"bom", "UTF-32LE");
    } else if (Length >= 4 &&
               Buffer[0] == 0x00 && Buffer[1] == 0x00 && Buffer[2] == 0xFE && Buffer[3] == 0xFF)
    {
        Util_Json_AddStringToObject(j, L"bom", "UTF-32BE");
    } else if (Length >= 3 &&
               Buffer[0] == 0xEF && Buffer[1] == 0xBB && Buffer[2] == 0xBF)
    {
        Util_Json_AddStringToObject(j, L"bom", "UTF-8");
    } else if (Length >= 2 && Buffer[0] == 0xFF && Buffer[1] == 0xFE)
    {
        Util_Json_AddStringToObject(j, L"bom", "UTF-16LE");
    } else if (Length >= 2 && Buffer[0] == 0xFE && Buffer[1] == 0xFF)
    {
        Util_Json_AddStringToObject(j, L"bom", "UTF-16BE");
    }
}

static
_Function_class_(WUA_COMMAND_FN)
_Ret_maybenull_
IJsonObject*
Command(VOID)
{
    const BYTE* Data;
    BYTE PreviousByte;
    IJsonObject* j;
    IO_FILE_MAP MapInfo;
    HANDLE hFile;
    LOGICAL HasPreviousByte;
    NTSTATUS Status;
    ULONGLONG BytesRead, CrLf, CrOnly, LfOnly;

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

    Data = NULL;
    BytesRead = 0;
    Status = IO_MapReadOnlyFile(hFile, &MapInfo);
    if (NT_SUCCESS(Status))
    {
        Data = reinterpret_cast<const BYTE*>(MapInfo.BaseAddress);
        BytesRead = MapInfo.FileSize;
    } else if (Status != STATUS_MAPPED_FILE_SIZE_ZERO)
    {
        NtClose(hFile);
        return BuildErrorOutput(HRESULT_FROM_NT(Status), "IO_MapReadOnlyFile failed with status 0x%08X.", Status);
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

    j = Util_Json_CreateObject();
    Util_Json_AddNumberToObject(j, L"size", (DOUBLE)BytesRead);
    AddBom(j, Data, BytesRead);
    Util_Json_AddNumberToObject(j, L"crlf", (DOUBLE)CrLf);
    Util_Json_AddNumberToObject(j, L"lf_only", (DOUBLE)LfOnly);
    Util_Json_AddNumberToObject(j, L"cr_only", (DOUBLE)CrOnly);
    AddFinalLineEnding(j, Data, BytesRead);
    if (Data != NULL)
    {
        IO_UnmapFile(&MapInfo);
    }
    NtClose(hFile);
    return BuildSuccessOutput(j);
}
