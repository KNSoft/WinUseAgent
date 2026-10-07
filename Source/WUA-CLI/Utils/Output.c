#include "pch.h"

HRESULT
Util_WriteJson(
    _In_ IJsonValue* Value,
    _In_ HANDLE File)
{
    PSTR Text = NULL;
    ULONG Length, Written, Offset = 0;
    NTSTATUS Status;
    HRESULT Hr;

    Hr = Data_JsonStringifyUtf8(Value, &Text, &Length);
    if (FAILED(Hr))
    {
        return Hr;
    }
    while (Offset < Length)
    {
        Status = IO_WriteFile(File, NULL, Text + Offset, Length - Offset, &Written);
        if (!NT_SUCCESS(Status))
        {
            Hr = Err_NtStatusToHr(Status);
            goto Exit;
        }
        if (Written == 0 || Written > Length - Offset)
        {
            Hr = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
            goto Exit;
        }
        Offset += Written;
    }
    Status = IO_WriteFile(File, NULL, "\n", 1, &Written);
    Hr = !NT_SUCCESS(Status) ? Err_NtStatusToHr(Status) :
        Written == 1 ? S_OK : HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
Exit:
    Mem_Free(Text);
    return Hr;
}
