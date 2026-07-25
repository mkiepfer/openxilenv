#include "Widgets/A2LCalMap/A2LCalMapData.h"

#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "A2LCalWidgetSync.h"

extern "C" {
#include "A2LLinkThread.h"
#include "A2LValue.h"
#include "Blackboard.h"
#include "BlackboardAccess.h"
#include "BlackboardConversion.h"
#include "BlackboardConvertFromTo.h"
#include "Compare2DoubleEqual.h"
#include "EquationParser.h"
#include "MyMemory.h"
#include "PrintFormatToString.h"
#include "TextReplace.h"
#include "ThrowError.h"
}

namespace XilEnvImGui
{

namespace
{

constexpr uint64_t FETCH_CHANNEL_TIMEOUT_NS{100000000u};
constexpr int X_AXIS_NO{0};
constexpr int Y_AXIS_NO{1};
constexpr int MAP_NO{2};

/**
 * @brief Writes a blackboard union value into an A2L single value, flagging it for update.
 */
void SetValueFromBBVari(A2L_SINGLE_VALUE *par_Value, union BB_VARI par_ToValue, enum BB_DATA_TYPES par_Type)
{
    // Convert the source blackboard union to double once; the target type dictates storage.
    double asDouble{0.0};
    switch (par_Type)
    {
    case BB_BYTE:
        asDouble = static_cast<double>(par_ToValue.b);
        break;
    case BB_UBYTE:
        asDouble = static_cast<double>(par_ToValue.ub);
        break;
    case BB_WORD:
        asDouble = static_cast<double>(par_ToValue.w);
        break;
    case BB_UWORD:
        asDouble = static_cast<double>(par_ToValue.uw);
        break;
    case BB_DWORD:
        asDouble = static_cast<double>(par_ToValue.dw);
        break;
    case BB_UDWORD:
        asDouble = static_cast<double>(par_ToValue.udw);
        break;
    case BB_QWORD:
        asDouble = static_cast<double>(par_ToValue.qw);
        break;
    case BB_UQWORD:
        asDouble = static_cast<double>(par_ToValue.uqw);
        break;
    case BB_FLOAT:
        asDouble = static_cast<double>(par_ToValue.f);
        break;
    case BB_DOUBLE:
        asDouble = par_ToValue.d;
        break;
    default:
        return;
    }

    switch (par_Value->Type)
    {
    case A2L_ELEM_TYPE_INT:
        // sc_convert_from_to writes only the narrow union member for BB_BYTE/WORD/DWORD, so reading
        // par_ToValue.qw would pick up garbage upper bytes. asDouble already read the exact member
        // above (lossless for <= 32-bit); use the full 64-bit member only for BB_(U)QWORD sources.
        if (BB_QWORD == par_Type)
        {
            par_Value->Value.Int = par_ToValue.qw;
        }
        else if (BB_UQWORD == par_Type)
        {
            par_Value->Value.Int = static_cast<int64_t>(par_ToValue.uqw);
        }
        else
        {
            par_Value->Value.Int = static_cast<int64_t>(asDouble);
        }
        par_Value->Flags |= A2L_VALUE_FLAG_UPDATE;
        break;
    case A2L_ELEM_TYPE_UINT:
        if (BB_UQWORD == par_Type)
        {
            par_Value->Value.Uint = par_ToValue.uqw;
        }
        else if (BB_QWORD == par_Type)
        {
            par_Value->Value.Uint = static_cast<uint64_t>(par_ToValue.qw);
        }
        else
        {
            par_Value->Value.Uint = static_cast<uint64_t>(asDouble);
        }
        par_Value->Flags |= A2L_VALUE_FLAG_UPDATE;
        break;
    case A2L_ELEM_TYPE_DOUBLE:
    case A2L_ELEM_TYPE_PHYS_DOUBLE:
        par_Value->Value.Double = asDouble;
        par_Value->Flags |= A2L_VALUE_FLAG_UPDATE;
        break;
    default:
        break;
    }
}

/**
 * @brief Computes raw min/max from the physical min/max of one axis via its formula.
 */
void CalcRawMinMaxValuesOneAxis(CHARACTERISTIC_AXIS_INFO *par_AxisInfo, const char *par_AxisName)
{
    double help{0.0};
    char *pErrString{nullptr};

    if (1 == par_AxisInfo->m_ConvType) // formula
    {
        if (0 != calc_raw_value_for_phys_value(par_AxisInfo->m_Conversion, par_AxisInfo->m_Min, "", BB_DOUBLE,
                                               &par_AxisInfo->m_RawMin, &help, &pErrString))
        {
            par_AxisInfo->m_RawMin = par_AxisInfo->m_Min;
            ThrowError(1, "cannot convert physical min. value of %s axis to raw value (formula \"%s\": \"%s\")",
                       par_AxisName, par_AxisInfo->m_Conversion, pErrString);
            par_AxisInfo->m_ConvType = 0;
            FreeErrStringBuffer(&pErrString);
        }
        if (0 != calc_raw_value_for_phys_value(par_AxisInfo->m_Conversion, par_AxisInfo->m_Max, "", BB_DOUBLE,
                                               &par_AxisInfo->m_RawMax, &help, &pErrString))
        {
            par_AxisInfo->m_RawMax = par_AxisInfo->m_Max;
            ThrowError(1, "cannot convert physical max. value of %s axis to raw value (formula \"%s\": \"%s\")",
                       par_AxisName, par_AxisInfo->m_Conversion, pErrString);
            par_AxisInfo->m_ConvType = 0;
            FreeErrStringBuffer(&pErrString);
        }
    }
    else
    {
        par_AxisInfo->m_RawMin = par_AxisInfo->m_Min;
        par_AxisInfo->m_RawMax = par_AxisInfo->m_Max;
    }
}

double AddButMinimumIncOrDecByOne(double par_Value, int par_Type, double par_Add)
{
    const double result{par_Value + par_Add};
    double minValue{0.0};
    double maxValue{0.0};
    get_datatype_min_max_value(par_Type, &minValue, &maxValue);
    if ((result >= maxValue) && (par_Add > 0.0))
    {
        return maxValue;
    }
    if ((result <= minValue) && (par_Add < 0.0))
    {
        return minValue;
    }
    if ((par_Type >= BB_BYTE) && (par_Type <= BB_UDWORD))
    {
        int64_t asInt{static_cast<int64_t>(result + 0.5)};
        if (static_cast<int64_t>(par_Value) == asInt)
        {
            asInt += (par_Add > 0.0) ? 1 : -1;
        }
        return static_cast<double>(asInt);
    }
    return result;
}

} // namespace

A2LCalMapData::A2LCalMapData()
{
    std::memset(&m_xAxisInfo, 0, sizeof(m_xAxisInfo));
    std::memset(&m_yAxisInfo, 0, sizeof(m_yAxisInfo));
    std::memset(&m_zAxisInfo, 0, sizeof(m_zAxisInfo));
}

A2LCalMapData::~A2LCalMapData()
{
    Clear();
}

void A2LCalMapData::ResetAxisInfo(CHARACTERISTIC_AXIS_INFO *par_AxisInfo, bool par_Free)
{
    if (par_Free)
    {
        if (nullptr != par_AxisInfo->m_Unit)
        {
            my_free(par_AxisInfo->m_Unit);
        }
        if (nullptr != par_AxisInfo->m_Input)
        {
            my_free(par_AxisInfo->m_Input);
        }
        if (nullptr != par_AxisInfo->m_Conversion)
        {
            my_free(par_AxisInfo->m_Conversion);
        }
    }
    par_AxisInfo->m_Max = 1.0;
    par_AxisInfo->m_Min = 0.0;
    par_AxisInfo->m_Unit = nullptr;
    par_AxisInfo->m_Input = nullptr;
    par_AxisInfo->m_Conversion = nullptr;
    par_AxisInfo->m_ConvType = 0;
    par_AxisInfo->m_FormatLayout = 8;
    par_AxisInfo->m_FormatLength = 8;
}

void A2LCalMapData::Reset(bool par_Free)
{
    ResetAxisInfo(&m_xAxisInfo, par_Free);
    ResetAxisInfo(&m_yAxisInfo, par_Free);
    ResetAxisInfo(&m_zAxisInfo, par_Free);

    if (0 < m_xVid)
    {
        remove_bbvari_unknown_wait(m_xVid);
        m_xVid = -1;
    }
    if (0 < m_yVid)
    {
        remove_bbvari_unknown_wait(m_yVid);
        m_yVid = -1;
    }
}

void A2LCalMapData::Clear()
{
    if (0 <= m_getDataChannelNo)
    {
        DeleteDataFromLinkChannel(m_getDataChannelNo);
        m_getDataChannelNo = -1;
    }
    if (nullptr != m_pData)
    {
        FreeA2lData(m_pData);
        m_pData = nullptr;
    }
    Reset(true);
    m_xDimPickMap = -1;
    m_yDimPickMap = -1;
    m_xPickLast = 0;
    m_yPickLast = 0;
    m_pickMap.clear();
}

void A2LCalMapData::SetProcess(const std::string &par_ProcessName, void *par_CallbackParam)
{
    m_processName = par_ProcessName;
    m_linkNo = A2LGetLinkToExternProcess(m_processName.c_str());
    if (0 <= m_getDataChannelNo)
    {
        DeleteDataFromLinkChannel(m_getDataChannelNo);
        m_getDataChannelNo = -1;
    }
    if (0 <= m_linkNo)
    {
        m_getDataChannelNo = NewDataFromLinkChannel(m_linkNo, FETCH_CHANNEL_TIMEOUT_NS,
                                                    GlobalNotifiyGetDataFromLinkAckCallback, par_CallbackParam);
    }
}

void A2LCalMapData::ProcessTerminated()
{
    Clear();
}

void A2LCalMapData::AttachAxisInput(CHARACTERISTIC_AXIS_INFO &par_AxisInfo, int &ret_Vid)
{
    if ((nullptr == par_AxisInfo.m_Input) || (1 >= std::strlen(par_AxisInfo.m_Input)))
    {
        return;
    }
    ret_Vid = get_bbvarivid_by_name(par_AxisInfo.m_Input);
    if (0 < ret_Vid)
    {
        attach_bbvari_unknown_wait(ret_Vid);
    }
    else
    {
        const int inputIdx{A2LGetIndexFromLink(m_linkNo, par_AxisInfo.m_Input,
                                               A2L_LABEL_TYPE_SINGEL_VALUE_MEASUREMENT |
                                                   A2L_LABEL_TYPE_NOT_REFERENCED_MEASUREMENT)};
        if (0 <= inputIdx)
        {
            if (0 == A2LReferenceMeasurementToBlackboard(m_linkNo, inputIdx, 0x2, 1))
            {
                ret_Vid = get_bbvarivid_by_name(par_AxisInfo.m_Input);
                if (0 < ret_Vid)
                {
                    attach_bbvari_unknown_wait(ret_Vid);
                }
            }
        }
    }
}

void A2LCalMapData::SetCharacteristicName(const std::string &par_Characteristic)
{
    // Free axis descriptors / detach axis-input VIDs from a previous binding first.
    Reset(true);
    m_characteristicName = par_Characteristic;
    if (0 > m_linkNo)
    {
        return;
    }
    m_index = A2LGetIndexFromLink(m_linkNo, m_characteristicName.c_str(),
                                  A2L_LABEL_TYPE_VAL_BLK_CALIBRATION | A2L_LABEL_TYPE_CURVE_CALIBRATION |
                                      A2L_LABEL_TYPE_MAP_CALIBRATION);
    if (0 > m_index)
    {
        return;
    }
    const int type{A2LGetCharacteristicInfoType(m_linkNo, m_index)};
    (void)A2LGetCharacteristicAxisInfos(m_linkNo, m_index, 0, &m_zAxisInfo); // 0 -> map/value
    if ((4 == type) || (5 == type))                                         // curve and map
    {
        if (0 == A2LGetCharacteristicAxisInfos(m_linkNo, m_index, 1, &m_xAxisInfo)) // 1 -> x axis
        {
            AttachAxisInput(m_xAxisInfo, m_xVid);
        }
    }
    if (5 == type) // map
    {
        if (0 == A2LGetCharacteristicAxisInfos(m_linkNo, m_index, 2, &m_yAxisInfo)) // 2 -> y axis
        {
            AttachAxisInput(m_yAxisInfo, m_yVid);
        }
    }
    if (0 == m_zAxisInfo.m_ConvType)
    {
        m_physicalFlag = false; // no formula -> cannot display physical
    }
    CalcRawMinMaxValues();
}

int A2LCalMapData::GetType() const
{
    switch (GetLinkDataArrayCount(m_pData))
    {
    case 1:
    case 2:
        return 1; // curve
    case 3:
        return 2; // map
    default:
        return 0;
    }
}

int A2LCalMapData::GetXDim() const
{
    if (1 <= GetLinkDataArrayCount(m_pData))
    {
        return GetLinkDataArraySize(m_pData, 0);
    }
    return 0;
}

int A2LCalMapData::GetYDim() const
{
    if (3 <= GetLinkDataArrayCount(m_pData))
    {
        return GetLinkDataArraySize(m_pData, 1);
    }
    return 1;
}

int A2LCalMapData::TranslateAxisToArrayNo(AxisType par_Axis) const
{
    if (nullptr == m_pData)
    {
        return -1;
    }
    switch (par_Axis)
    {
    case AxisType::XAxis:
        switch (m_pData->Type)
        {
        case A2L_DATA_TYPE_CURVE:
        case A2L_DATA_TYPE_MAP:
            return 0;
        default:
            return -1;
        }
    case AxisType::YAxis:
        return (A2L_DATA_TYPE_MAP == m_pData->Type) ? 1 : -1;
    case AxisType::Map:
        switch (m_pData->Type)
        {
        case A2L_DATA_TYPE_ASCII:
        case A2L_DATA_TYPE_VAL_BLK:
            return 0;
        case A2L_DATA_TYPE_CURVE:
            return 1;
        case A2L_DATA_TYPE_MAP:
            return 2;
        default:
            return -1;
        }
    }
    return -1;
}

A2L_SINGLE_VALUE *A2LCalMapData::GetValue(AxisType par_Axis, int par_X, int par_Y) const
{
    if (nullptr == m_pData)
    {
        return nullptr;
    }
    const int arrayNo{TranslateAxisToArrayNo(par_Axis)};
    if (0 > arrayNo)
    {
        return nullptr;
    }
    int elementNo{par_X};
    if (AxisType::Map == par_Axis)
    {
        const int xDim{GetXDim()};
        const int yDim{GetYDim()};
        if ((0 >= xDim) || (0 >= yDim))
        {
            return nullptr;
        }
        elementNo = par_Y * xDim + par_X;
    }
    return GetLinkArrayValueData(m_pData, arrayNo, elementNo);
}

double A2LCalMapData::GetRawValue(AxisType par_Axis, int par_X, int par_Y) const
{
    A2L_SINGLE_VALUE *pValue{GetValue(par_Axis, par_X, par_Y)};
    if (nullptr == pValue)
    {
        return 0.0;
    }
    return ConvertRawValueToDouble(pValue);
}

double A2LCalMapData::GetDoubleValue(AxisType par_Axis, int par_X, int par_Y)
{
    if (nullptr == GetValue(par_Axis, par_X, par_Y))
    {
        return 0.0;
    }
    const double raw{GetRawValue(par_Axis, par_X, par_Y)};
    switch (par_Axis)
    {
    case AxisType::XAxis:
        return Conv(raw, m_xAxisInfo);
    case AxisType::YAxis:
        return Conv(raw, m_yAxisInfo);
    case AxisType::Map:
        return Conv(raw, m_zAxisInfo);
    }
    return 0.0;
}

double A2LCalMapData::GetValueNorm(AxisType par_Axis, int par_X, int par_Y)
{
    if (nullptr == GetValue(par_Axis, par_X, par_Y))
    {
        return 0.0;
    }
    const double raw{GetRawValue(par_Axis, par_X, par_Y)};
    switch (par_Axis)
    {
    case AxisType::XAxis:
        return ConvNorm(raw, m_xAxisInfo);
    case AxisType::YAxis:
        return ConvNorm(raw, m_yAxisInfo);
    case AxisType::Map:
        return ConvNorm(raw, m_zAxisInfo);
    }
    return 0.0;
}

std::string A2LCalMapData::ConvertValueToString(A2L_SINGLE_VALUE *par_Value, int par_Precision) const
{
    char buffer[128];
    switch (par_Value->Type)
    {
    case A2L_ELEM_TYPE_INT:
        PrintFormatToString(buffer, sizeof(buffer), "%" PRIi64 "", par_Value->Value.Int);
        return buffer;
    case A2L_ELEM_TYPE_UINT:
        PrintFormatToString(buffer, sizeof(buffer), "%" PRIu64 "", par_Value->Value.Uint);
        return buffer;
    case A2L_ELEM_TYPE_DOUBLE:
    {
        int prec{15};
        while (true)
        {
            PrintFormatToString(buffer, sizeof(buffer), "%.*g", prec, par_Value->Value.Double);
            if ((18 == prec++) || (par_Value->Value.Double == std::strtod(buffer, nullptr)))
            {
                break;
            }
        }
        return buffer;
    }
    case A2L_ELEM_TYPE_PHYS_DOUBLE:
        std::snprintf(buffer, sizeof(buffer), "%.*f", par_Precision, par_Value->Value.Double);
        return buffer;
    case A2L_ELEM_TYPE_TEXT_REPLACE:
        std::snprintf(buffer, sizeof(buffer), "\"%s\"", par_Value->Value.String);
        return buffer;
    default:
        return "error";
    }
}

std::string A2LCalMapData::ConvertToString(double par_Value, CHARACTERISTIC_AXIS_INFO &par_AxisInfo,
                                           bool par_PhysFlag)
{
    if (par_PhysFlag)
    {
        if (BB_CONV_TEXTREP == par_AxisInfo.m_ConvType)
        {
            char help[256];
            int color{0};
            if (0 == convert_value2textreplace(static_cast<int64_t>(par_Value), par_AxisInfo.m_Conversion, help,
                                               sizeof(help), &color))
            {
                return help;
            }
            return std::string();
        }
        A2L_SINGLE_VALUE help;
        help.Type = A2L_ELEM_TYPE_PHYS_DOUBLE;
        help.Value.Double = Conv(par_Value, par_AxisInfo);
        return ConvertValueToString(&help, par_AxisInfo.m_FormatLayout);
    }
    A2L_SINGLE_VALUE help;
    help.Type = A2L_ELEM_TYPE_PHYS_DOUBLE;
    help.Value.Double = par_Value;
    return ConvertValueToString(&help, par_AxisInfo.m_FormatLayout);
}

std::string A2LCalMapData::GetValueString(AxisType par_Axis, int par_X, int par_Y)
{
    if (nullptr == GetValue(par_Axis, par_X, par_Y))
    {
        return std::string();
    }
    const double raw{GetRawValue(par_Axis, par_X, par_Y)};
    switch (par_Axis)
    {
    case AxisType::XAxis:
        return ConvertToString(raw, m_xAxisInfo, m_physicalFlag);
    case AxisType::YAxis:
        return ConvertToString(raw, m_yAxisInfo, m_physicalFlag);
    case AxisType::Map:
        return ConvertToString(raw, m_zAxisInfo, m_physicalFlag);
    }
    return std::string();
}

double A2LCalMapData::Conv(double par_Value, CHARACTERISTIC_AXIS_INFO &par_AxisInfo)
{
    if (!m_physicalFlag)
    {
        return par_Value;
    }
    double result{par_Value};
    bool convError{false};
    switch (par_AxisInfo.m_ConvType)
    {
    default:
    case BB_CONV_NONE:
        result = par_Value;
        break;
    case BB_CONV_FORMULA:
        if ((nullptr != par_AxisInfo.m_Conversion) && (0 < std::strlen(par_AxisInfo.m_Conversion)))
        {
            if (direct_solve_equation_err_state_replace_value(par_AxisInfo.m_Conversion, par_Value, &result))
            {
                convError = true;
            }
        }
        break;
    case BB_CONV_FACTOFF:
        convError = (0 != Conv_FactorOffsetRawToPhysFromString(par_AxisInfo.m_Conversion, par_Value, &result));
        break;
    case BB_CONV_OFFFACT:
        convError = (0 != Conv_OffsetFactorRawToPhysFromString(par_AxisInfo.m_Conversion, par_Value, &result));
        break;
    case BB_CONV_TAB_INTP:
        convError = (0 != Conv_TableInterpolRawToPhysFromString(par_AxisInfo.m_Conversion, par_Value, &result));
        break;
    case BB_CONV_TAB_NOINTP:
        convError = (0 != Conv_TableNoInterpolRawToPhysFromString(par_AxisInfo.m_Conversion, par_Value, &result));
        break;
    case BB_CONV_RAT_FUNC:
        convError = (0 != Conv_RationalFunctionRawToPhysFromString(par_AxisInfo.m_Conversion, par_Value, &result));
        break;
    }
    if (convError)
    {
        ThrowError(1, "switch to raw view because of errors inside conversion \"%s\" from label \"%s\"",
                   par_AxisInfo.m_Conversion, m_characteristicName.c_str());
        m_physicalFlag = false;
        result = par_Value;
    }
    return result;
}

double A2LCalMapData::ConvNorm(double par_Value, CHARACTERISTIC_AXIS_INFO &par_AxisInfo)
{
    double result{par_Value};
    if (m_physicalFlag)
    {
        result = Conv(par_Value, par_AxisInfo);
        if (par_AxisInfo.m_Max <= par_AxisInfo.m_Min)
        {
            par_AxisInfo.m_Max = par_AxisInfo.m_Min + 1.0;
        }
        result = (result - par_AxisInfo.m_Min) / (par_AxisInfo.m_Max - par_AxisInfo.m_Min);
    }
    else
    {
        if (par_AxisInfo.m_RawMax <= par_AxisInfo.m_RawMin)
        {
            par_AxisInfo.m_RawMax = par_AxisInfo.m_RawMin + 1.0;
        }
        result = (par_Value - par_AxisInfo.m_RawMin) / (par_AxisInfo.m_RawMax - par_AxisInfo.m_RawMin);
    }
    return result;
}

int A2LCalMapData::ChangeValueOP(int par_Op, double par_Value, AxisType par_Axis, int par_X, int par_Y)
{
    A2L_SINGLE_VALUE *pValue{GetValue(par_Axis, par_X, par_Y)};
    if (nullptr == pValue)
    {
        return -1;
    }
    union BB_VARI fromValue;
    union BB_VARI toValue;
    double help{0.0};

    switch (par_Op)
    {
    case 'i':
    case 'I':
        fromValue.d = par_Value;
        sc_convert_from_to(BB_DOUBLE, &fromValue, pValue->TargetType, &toValue);
        SetValueFromBBVari(pValue, toValue, static_cast<enum BB_DATA_TYPES>(pValue->TargetType));
        break;
    case '+':
        help = ConvertRawValueToDouble(pValue) + par_Value;
        fromValue.d = help;
        sc_convert_from_to(BB_DOUBLE, &fromValue, pValue->TargetType, &toValue);
        SetValueFromBBVari(pValue, toValue, static_cast<enum BB_DATA_TYPES>(pValue->TargetType));
        break;
    case '-':
        help = ConvertRawValueToDouble(pValue) - par_Value;
        fromValue.d = help;
        sc_convert_from_to(BB_DOUBLE, &fromValue, pValue->TargetType, &toValue);
        SetValueFromBBVari(pValue, toValue, static_cast<enum BB_DATA_TYPES>(pValue->TargetType));
        break;
    case '*':
        help = ConvertRawValueToDouble(pValue) * par_Value;
        fromValue.d = help;
        sc_convert_from_to(BB_DOUBLE, &fromValue, pValue->TargetType, &toValue);
        SetValueFromBBVari(pValue, toValue, static_cast<enum BB_DATA_TYPES>(pValue->TargetType));
        break;
    case '/':
        if ((par_Value > 0.0) || (par_Value < 0.0))
        {
            help = ConvertRawValueToDouble(pValue) / par_Value;
            fromValue.d = help;
            sc_convert_from_to(BB_DOUBLE, &fromValue, pValue->TargetType, &toValue);
            SetValueFromBBVari(pValue, toValue, static_cast<enum BB_DATA_TYPES>(pValue->TargetType));
        }
        break;
    case 'o':
    case 'O':
        m_decIncOffset = par_Value;
        break;
    case 'p':
    case 'P':
        help = ConvertRawValueToDouble(pValue) + m_decIncOffset;
        fromValue.d = help;
        sc_convert_from_to(BB_DOUBLE, &fromValue, pValue->TargetType, &toValue);
        SetValueFromBBVari(pValue, toValue, static_cast<enum BB_DATA_TYPES>(pValue->TargetType));
        break;
    case 'm':
    case 'M':
        help = ConvertRawValueToDouble(pValue) - m_decIncOffset;
        fromValue.d = help;
        sc_convert_from_to(BB_DOUBLE, &fromValue, pValue->TargetType, &toValue);
        SetValueFromBBVari(pValue, toValue, static_cast<enum BB_DATA_TYPES>(pValue->TargetType));
        break;
    case 1: // add, but change the value by at least one
        help = ConvertRawValueToDouble(pValue);
        help = AddButMinimumIncOrDecByOne(help, pValue->TargetType, par_Value);
        fromValue.d = help;
        sc_convert_from_to(BB_DOUBLE, &fromValue, pValue->TargetType, &toValue);
        SetValueFromBBVari(pValue, toValue, static_cast<enum BB_DATA_TYPES>(pValue->TargetType));
        break;
    default:
        break;
    }
    return 0;
}

int A2LCalMapData::WriteDataToTargetLink(bool par_Readback)
{
    if ((0 < m_linkNo) && (0 <= m_getDataChannelNo))
    {
        INDEX_DATA_BLOCK *pIdb{GetIndexDataBlock(1)};
        if (nullptr == pIdb)
        {
            return -1;
        }
        pIdb->Data->LinkNo = m_linkNo;
        pIdb->Data->Index = m_index;
        pIdb->Data->Data = m_pData;
        pIdb->Data->Flags = 0;
        const int ret{A2LGetDataFromLinkReq(m_getDataChannelNo, 1, pIdb)};
        if ((0 == ret) && par_Readback)
        {
            UpdateReq();
        }
    }
    return 0;
}

int A2LCalMapData::UpdateReq()
{
    if ((0 < m_linkNo) && (0 <= m_getDataChannelNo) && (0 <= m_index))
    {
        INDEX_DATA_BLOCK *pIdb{GetIndexDataBlock(1)};
        if (nullptr == pIdb)
        {
            return -1;
        }
        pIdb->Data->LinkNo = m_linkNo;
        pIdb->Data->Index = m_index;
        pIdb->Data->Data = m_pData;
        pIdb->Data->Flags = 0;
        return A2LGetDataFromLinkReq(m_getDataChannelNo, 0, pIdb);
    }
    return -1;
}

int A2LCalMapData::UpdateAck(void *par_Data)
{
    A2L_DATA *pNew{DupA2lData(par_Data)};
    if (nullptr == pNew)
    {
        return -1;
    }
    if (nullptr != m_pData)
    {
        FreeA2lData(m_pData);
    }
    m_pData = pNew;

    const int xDim{GetXDim()};
    const int yDim{GetYDim()};
    if ((xDim != m_xDimPickMap) || (yDim != m_yDimPickMap))
    {
        m_xDimPickMap = xDim;
        m_yDimPickMap = yDim;
        if ((1 <= m_xDimPickMap) && (m_xDimPickMap <= 10000) && (1 <= m_yDimPickMap) && (m_yDimPickMap <= 10000))
        {
            m_pickMap.assign(static_cast<size_t>(m_xDimPickMap) * static_cast<size_t>(m_yDimPickMap), 0);
        }
        else
        {
            m_pickMap.clear();
        }
    }
    return 0;
}

void A2LCalMapData::GetLastPick(int *ret_XPick, int *ret_YPick) const
{
    *ret_XPick = m_xPickLast;
    *ret_YPick = m_yPickLast;
}

void A2LCalMapData::SetPick(int par_X, int par_Y, int par_Op)
{
    if ((nullptr == m_pData) || m_pickMap.empty())
    {
        return;
    }
    const int pos{par_Y * m_xDimPickMap + par_X};
    if (pos >= (m_xDimPickMap * m_yDimPickMap))
    {
        return;
    }
    switch (par_Op)
    {
    case 1:
        m_pickMap[static_cast<size_t>(pos)] = 1;
        break;
    case 2:
        m_pickMap[static_cast<size_t>(pos)] = 0;
        break;
    case 3:
        m_pickMap[static_cast<size_t>(pos)] ^= 1;
        break;
    default:
        break;
    }
    if (0 != m_pickMap[static_cast<size_t>(pos)])
    {
        m_xPickLast = par_X;
        m_yPickLast = par_Y;
    }
}

bool A2LCalMapData::IsPicked(int par_X, int par_Y) const
{
    if ((nullptr == m_pData) || m_pickMap.empty())
    {
        return false;
    }
    const int pos{par_Y * m_xDimPickMap + par_X};
    if (pos >= (m_xDimPickMap * m_yDimPickMap))
    {
        return false;
    }
    return (0 != m_pickMap[static_cast<size_t>(pos)]);
}

void A2LCalMapData::ChangeSelectionOfAll(bool par_Select)
{
    const int xDim{GetXDim()};
    const int yDim{GetYDim()};
    for (int y{0}; y < yDim; ++y)
    {
        for (int x{0}; x < xDim; ++x)
        {
            SetPick(x, y, par_Select ? 1 : 2);
        }
    }
}

double A2LCalMapData::GetMaxMinusMin(int par_Axis) const
{
    const CHARACTERISTIC_AXIS_INFO *pInfo{nullptr};
    switch (par_Axis)
    {
    case X_AXIS_NO:
        pInfo = &m_xAxisInfo;
        break;
    case Y_AXIS_NO:
        pInfo = &m_yAxisInfo;
        break;
    case MAP_NO:
        pInfo = &m_zAxisInfo;
        break;
    default:
        return 1.0;
    }
    if (m_physicalFlag)
    {
        return pInfo->m_Max - pInfo->m_Min;
    }
    return pInfo->m_RawMax - pInfo->m_RawMin;
}

void A2LCalMapData::AdjustMinMax()
{
    double xMin{+1.0E37};
    double yMin{+1.0E37};
    double zMin{+1.0E37};
    double xMax{-1.0E37};
    double yMax{-1.0E37};
    double zMax{-1.0E37};
    const int xDim{GetXDim()};
    const int yDim{GetYDim()};
    for (int y{0}; y < yDim; ++y)
    {
        for (int x{0}; x < xDim; ++x)
        {
            const double xv{GetDoubleValue(AxisType::XAxis, x, 0)};
            const double yv{GetDoubleValue(AxisType::YAxis, y, 0)};
            const double zv{GetDoubleValue(AxisType::Map, x, y)};
            xMin = (xv < xMin) ? xv : xMin;
            xMax = (xv > xMax) ? xv : xMax;
            yMin = (yv < yMin) ? yv : yMin;
            yMax = (yv > yMax) ? yv : yMax;
            zMin = (zv < zMin) ? zv : zMin;
            zMax = (zv > zMax) ? zv : zMax;
        }
    }
    if (xMin >= xMax)
    {
        xMin -= 1.0;
        xMax += 1.0;
    }
    if (yMin >= yMax)
    {
        yMin -= 1.0;
        yMax += 1.0;
    }
    if (zMin >= zMax)
    {
        zMin -= 1.0;
        zMax += 1.0;
    }
    xMin -= (xMax - xMin) / 10.0;
    yMin -= (yMax - yMin) / 10.0;
    zMin -= (zMax - zMin) / 10.0;
    xMax += (xMax - xMin) / 10.0;
    yMax += (yMax - yMin) / 10.0;
    zMax += (zMax - zMin) / 10.0;

    m_xAxisInfo.m_Min = xMin;
    m_xAxisInfo.m_Max = xMax;
    m_yAxisInfo.m_Min = yMin;
    m_yAxisInfo.m_Max = yMax;
    m_zAxisInfo.m_Min = zMin;
    m_zAxisInfo.m_Max = zMax;

    CalcRawMinMaxValues();
}

void A2LCalMapData::CalcRawMinMaxValues()
{
    CalcRawMinMaxValuesOneAxis(&m_xAxisInfo, "x");
    const int type{A2LGetCharacteristicInfoType(m_linkNo, m_index)};
    if (5 == type) // map
    {
        CalcRawMinMaxValuesOneAxis(&m_yAxisInfo, "y");
    }
    CalcRawMinMaxValuesOneAxis(&m_zAxisInfo, "map");
}

const char *A2LCalMapData::GetXInput() const
{
    return (nullptr != m_xAxisInfo.m_Input) ? m_xAxisInfo.m_Input : "";
}

const char *A2LCalMapData::GetXUnit() const
{
    return (nullptr != m_xAxisInfo.m_Unit) ? m_xAxisInfo.m_Unit : "";
}

const char *A2LCalMapData::GetYInput() const
{
    return (nullptr != m_yAxisInfo.m_Input) ? m_yAxisInfo.m_Input : "";
}

const char *A2LCalMapData::GetYUnit() const
{
    return (nullptr != m_yAxisInfo.m_Unit) ? m_yAxisInfo.m_Unit : "";
}

const char *A2LCalMapData::GetMapUnit() const
{
    return (nullptr != m_zAxisInfo.m_Unit) ? m_zAxisInfo.m_Unit : "";
}

double A2LCalMapData::MapAccess(double *ret_PointX, double *ret_PointY, double *ret_PointZ)
{
    double z0{0.0};
    double z1{0.0};
    int i{0};
    int j{0};

    const int xDim{GetXDim()};
    const int yDim{GetYDim()};

    double xValue{read_bbvari_convert_double(m_xVid)};
    double yValue{(2 == GetType()) ? read_bbvari_convert_double(m_yVid) : 0.0};

    const double firstX{GetRawValue(AxisType::XAxis, 0, 0)};
    const double lastX{GetRawValue(AxisType::XAxis, xDim - 1, 0)};
    xValue = (xValue > lastX) ? lastX : ((xValue < firstX) ? firstX : xValue);
    const double firstY{GetRawValue(AxisType::YAxis, 0, 0)};
    const double lastY{GetRawValue(AxisType::YAxis, yDim - 1, 0)};
    yValue = (yValue > lastY) ? lastY : ((yValue < firstY) ? firstY : yValue);

    for (i = 0; i < xDim; ++i)
    {
        if (xValue <= GetRawValue(AxisType::XAxis, i, 0))
        {
            break;
        }
    }
    if (0 == i)
    {
        xValue = firstX;
    }
    else if (xDim == i)
    {
        xValue = lastX;
        i -= 2;
    }
    else
    {
        --i;
    }

    for (j = 0; j < yDim; ++j)
    {
        if (yValue <= GetRawValue(AxisType::YAxis, j, 0))
        {
            break;
        }
    }
    if (0 == j)
    {
        yValue = firstY;
    }
    else if (yDim == j)
    {
        yValue = lastY;
        j -= 2;
    }
    else
    {
        --j;
    }

    const double xi0{GetRawValue(AxisType::XAxis, i, 0)};
    const double xi1{GetRawValue(AxisType::XAxis, i + 1, 0)};
    const double yj0{GetRawValue(AxisType::YAxis, j, 0)};
    const double yj1{GetRawValue(AxisType::YAxis, j + 1, 0)};
    double zji0{GetRawValue(AxisType::Map, xDim * j + i, 0)};
    double zji1{GetRawValue(AxisType::Map, xDim * j + i + 1, 0)};

    double ret{0.0};
    if (CompareDoubleEqual_int(xi1, xi0))
    {
        z0 = zji0;
    }
    else
    {
        z0 = (zji1 >= zji0) ? (((zji1 - zji0) * (xValue - xi0)) / (xi1 - xi0)) + zji0
                            : zji0 - (((zji0 - zji1) * (xValue - xi0)) / (xi1 - xi0));
        ++j;
        zji0 = GetRawValue(AxisType::Map, xDim * j + i, 0);
        zji1 = GetRawValue(AxisType::Map, xDim * j + i + 1, 0);
        z1 = (zji1 >= zji0) ? (((zji1 - zji0) * (xValue - xi0)) / (xi1 - xi0)) + zji0
                            : zji0 - (((zji0 - zji1) * (xValue - xi0)) / (xi1 - xi0));
    }
    if (CompareDoubleEqual_int(yj1, yj0))
    {
        ret = z0;
    }
    else
    {
        ret = (z1 >= z0) ? (((z1 - z0) * (yValue - yj0)) / (yj1 - yj0)) + z0
                         : z0 - (((z0 - z1) * (yValue - yj0)) / (yj1 - yj0));
    }
    *ret_PointX = ConvNorm(xValue, m_xAxisInfo);
    *ret_PointY = ConvNorm(yValue, m_yAxisInfo);
    *ret_PointZ = ConvNorm(ret, m_zAxisInfo);
    return ret;
}

} // namespace XilEnvImGui
