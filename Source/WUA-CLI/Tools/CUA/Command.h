#pragma once

#define CUA_GUID_CCH 37
#define CUA_MAX_KEYS 8
#define CUA_PARAM_BIT(Name) (1ull << CuaParam##Name)

typedef enum _CUA_PARAMETER
{
    CuaParamHandle,
    CuaParamBackend,
    CuaParamUia,
    CuaParamTimeout,
    CuaParamMaxNodes,
    CuaParamOutFile,
    CuaParamRuntimeId,
    CuaParamX,
    CuaParamY,
    CuaParamToX,
    CuaParamToY,
    CuaParamButton,
    CuaParamAxis,
    CuaParamDelta,
    CuaParamDuration,
    CuaParamKeys,
    CuaParamText,
    CuaParamTextMethod,
    CuaParamPath,
    CuaParamSession,
    CuaParamWidth,
    CuaParamHeight,
    CuaParamShow,
    CuaParamCount
} CUA_PARAMETER;

typedef enum _CUA_PARAMETER_TYPE
{
    CuaStringParameter,
    CuaNumberParameter,
    CuaBooleanParameter,
    CuaKeysParameter
} CUA_PARAMETER_TYPE;

typedef struct _CUA_PARAMETER_INFO
{
    PCWSTR Name;
    PCWSTR CliName;
    CUA_PARAMETER_TYPE Type;
    LOGICAL Private;
} CUA_PARAMETER_INFO;

typedef struct _CUA_PARAMETERS
{
    ULONGLONG Present;
    union
    {
        HSTRING String;
        LONG Number;
        LOGICAL Boolean;
    } Values[CuaParamCount];
    HSTRING Keys[CUA_MAX_KEYS];
    ULONG KeyCount;
} CUA_PARAMETERS;

typedef enum _CUA_METHOD
{
    CuaMethodUnknown,
    CuaMethodCapabilities,
    CuaMethodInspect,
    CuaMethodLocate,
    CuaMethodActivate,
    CuaMethodMinimize,
    CuaMethodMaximize,
    CuaMethodCloseWindow,
    CuaMethodSessionEnableChildSession,
    CuaMethodSessionList,
    CuaMethodSessionCreateChild,
    CuaMethodSessionPreview,
    CuaMethodSessionDestroy,
    CuaMethodClick,
    CuaMethodDoubleClick,
    CuaMethodMove,
    CuaMethodScroll,
    CuaMethodDrag,
    CuaMethodDown,
    CuaMethodUp,
    CuaMethodKeys,
    CuaMethodType,
    CuaMethodInvoke,
    CuaMethodSetValue,
    CuaMethodToggle,
    CuaMethodSelect
} CUA_METHOD;

typedef struct _CUA_REQUEST
{
    CUA_METHOD Method;
    CUA_PARAMETERS Parameters;
} CUA_REQUEST;

typedef struct _CUA_ERROR
{
    HRESULT Status;
    WCHAR Message[512];
} CUA_ERROR;

EXTERN_C_START

extern const CUA_PARAMETER_INFO CuaParameterInfo[CuaParamCount];

PCWSTR
CuaMethodName(
    _In_ CUA_METHOD Method);

CUA_METHOD
CuaFindMethod(
    _In_ PCWSTR Name);

CUA_METHOD
CuaFindCommand(
    _In_ PCWSTR Tool,
    _In_ PCWSTR Command,
    _In_ PCWSTR Operation);

LOGICAL
CuaHas(
    _In_ const CUA_PARAMETERS* Parameters,
    _In_ CUA_PARAMETER Parameter);

PCWSTR
CuaString(
    _In_ const CUA_PARAMETERS* Parameters,
    _In_ CUA_PARAMETER Parameter,
    _In_ PCWSTR Default);

LONG
CuaNumber(
    _In_ const CUA_PARAMETERS* Parameters,
    _In_ CUA_PARAMETER Parameter,
    _In_ LONG Default);

LOGICAL
CuaBoolean(
    _In_ const CUA_PARAMETERS* Parameters,
    _In_ CUA_PARAMETER Parameter,
    _In_ LOGICAL Default);

HRESULT
CuaSetString(
    _Inout_ CUA_PARAMETERS* Parameters,
    _In_ CUA_PARAMETER Parameter,
    _In_ PCWSTR Value);

VOID
CuaFreeParameters(
    _Inout_ CUA_PARAMETERS* Parameters);

VOID
CuaFreeRequest(
    _Inout_ CUA_REQUEST* Request);

HRESULT
CuaParseRequest(
    _In_ IJsonValue* Wire,
    _Out_ CUA_REQUEST* Request,
    _Out_ CUA_ERROR* Error);

HRESULT
CuaValidateParameters(
    _In_ const CUA_REQUEST* Request,
    _Out_ CUA_ERROR* Error);

HRESULT
CuaNewId(
    _Out_writes_(CUA_GUID_CCH) PWSTR Id);

HRESULT
CuaBuildRequest(
    _In_ const CUA_REQUEST* Request,
    _Outptr_ IJsonValue** Wire);

HRESULT
CuaBuildReply(
    _In_opt_ IJsonValue* Result,
    _In_opt_ const CUA_ERROR* Error,
    _Outptr_ IJsonValue** Reply);

EXTERN_C_END
