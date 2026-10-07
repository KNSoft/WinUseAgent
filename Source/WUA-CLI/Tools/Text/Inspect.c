#include "pch.h"

static PWSTR File;

static WUA_COMMAND_PARAMETER Parameters[] = {
    DEF_PARAMETER_ENTRY(File, String, TRUE)
};

static WUA_COMMAND_FN Command;
WUA_COMMAND Text_Inspect = { Parameters, ARRAYSIZE(Parameters), &Command };

static
_Ret_maybenull_
PCWSTR
FinalLineEnding(
    _In_reads_bytes_opt_(Length) const BYTE* Buffer,
    _In_ ULONGLONG Length)
{
    if (Buffer == NULL || Length == 0)
    {
        return NULL;
    }
    if (Length >= 2 && Buffer[Length - 2] == '\r' && Buffer[Length - 1] == '\n')
    {
        return L"CRLF";
    }
    if (Buffer[Length - 1] == '\n')
    {
        return L"LF";
    }
    if (Buffer[Length - 1] == '\r')
    {
        return L"CR";
    }
    return NULL;
}

static
_Ret_maybenull_
PCWSTR
ByteOrderMark(
    _In_reads_bytes_opt_(Length) const BYTE* Buffer,
    _In_ ULONGLONG Length)
{
    if (Buffer == NULL)
    {
        return NULL;
    }
    if (Length >= 4 && Buffer[0] == 0xFF && Buffer[1] == 0xFE && Buffer[2] == 0 && Buffer[3] == 0)
    {
        return L"UTF-32LE";
    }
    if (Length >= 4 && Buffer[0] == 0 && Buffer[1] == 0 && Buffer[2] == 0xFE && Buffer[3] == 0xFF)
    {
        return L"UTF-32BE";
    }
    if (Length >= 3 && Buffer[0] == 0xEF && Buffer[1] == 0xBB && Buffer[2] == 0xBF)
    {
        return L"UTF-8";
    }
    if (Length >= 2 && Buffer[0] == 0xFF && Buffer[1] == 0xFE)
    {
        return L"UTF-16LE";
    }
    if (Length >= 2 && Buffer[0] == 0xFE && Buffer[1] == 0xFF)
    {
        return L"UTF-16BE";
    }
    return NULL;
}

static
_Function_class_(WUA_COMMAND_FN)
_Ret_maybenull_
IJsonObject*
Command(VOID)
{
    const BYTE* Data;
    BYTE PreviousByte;
    IJsonObject* j = NULL;
    IJsonValueStatics* Factory = NULL;
    IJsonValueStatics2* NullFactory = NULL;
    PCWSTR Bom, Ending;
    HRESULT Hr;
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
        Data = (const BYTE*)(MapInfo.BaseAddress);
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
        if (HasPreviousByte != FALSE)
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

    if (HasPreviousByte != FALSE)
    {
        if (PreviousByte == '\r')
        {
            CrOnly++;
        } else if (PreviousByte == '\n')
        {
            LfOnly++;
        }
    }

    Hr = Data_JsonCreateObject(&j);
    if (SUCCEEDED(Hr))
    {
        Hr = Data_JsonGetValueFactory(&Factory);
    }
    if (SUCCEEDED(Hr))
    {
        Hr = Data_JsonObjectSetNumber(Factory, j, L"size", (DOUBLE)BytesRead);
    }
    Bom = ByteOrderMark(Data, BytesRead);
    if (SUCCEEDED(Hr) && Bom != NULL)
    {
        Hr = Data_JsonObjectSetString(Factory, j, L"bom", Bom, (ULONG)wcslen(Bom));
    }
    if (SUCCEEDED(Hr))
    {
        Hr = Data_JsonObjectSetNumber(Factory, j, L"crlf", (DOUBLE)CrLf);
    }
    if (SUCCEEDED(Hr))
    {
        Hr = Data_JsonObjectSetNumber(Factory, j, L"lf_only", (DOUBLE)LfOnly);
    }
    if (SUCCEEDED(Hr))
    {
        Hr = Data_JsonObjectSetNumber(Factory, j, L"cr_only", (DOUBLE)CrOnly);
    }
    Ending = FinalLineEnding(Data, BytesRead);
    if (SUCCEEDED(Hr))
    {
        if (Ending != NULL)
        {
            Hr = Data_JsonObjectSetString(Factory, j, L"final_line_ending", Ending, (ULONG)wcslen(Ending));
        } else
        {
            Hr = Factory->lpVtbl->QueryInterface(Factory, &IID_IJsonValueStatics2, (PVOID*)&NullFactory);
            if (SUCCEEDED(Hr))
            {
                Hr = Data_JsonObjectSetNull(NullFactory, j, L"final_line_ending");
            }
        }
    }
    if (NullFactory != NULL)
    {
        NullFactory->lpVtbl->Release(NullFactory);
    }
    if (Factory != NULL)
    {
        Factory->lpVtbl->Release(Factory);
    }
    if (Data != NULL)
    {
        IO_UnmapFile(&MapInfo);
    }
    NtClose(hFile);
    if (FAILED(Hr))
    {
        if (j != NULL)
        {
            j->lpVtbl->Release(j);
        }
        return BuildErrorOutput(Hr, "Unable to format the text inspection result.");
    }
    return BuildSuccessOutput((IUnknown*)j);
}
