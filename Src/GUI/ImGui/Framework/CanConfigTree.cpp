#include "Framework/CanConfigTree.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <regex>
#include <string>
#include <system_error>
#include <vector>

#include "imgui.h"
#include "imgui_stdlib.h"

extern "C" {
#include "IniDataBase.h"
#include "Message.h"
#include "Scheduler.h"
}

namespace XilEnvImGui
{

namespace
{

constexpr int MAX_CAN_CHANNELS{16};
constexpr int BUS_CFG_MODE_MASK{0xF0000};
constexpr int BUS_CFG_USER{0x40000};

const char *const BYTE_ORDER_LABELS[]{"lsb_first", "msb_first"};
const char *const SIGN_LABELS[]{"signed", "unsigned", "float"};
const char *const CONVERSION_LABELS[]{"factor * raw + offset", "factor * (raw + offset)", "curve",
                                      "equation"};
const char *const SIGNAL_TYPE_LABELS[]{"normal", "multiplexer", "multiplexed by"};
const char *const OBJECT_TYPE_LABELS[]{"normal", "multiplexed", "j1939", "j1939 multi C-PG",
                                       "j1939 C-PG"};
const char *const DIRECTION_LABELS[]{"write", "read", "write (variable id)",
                                     "read (startup variable id)"};
const char *const TRANSMIT_LABELS[]{"cyclic", "event/equation"};
const char *const CONTROL_BB_LABELS[]{"PrefixId", "PrefixObjName", "NoPrefixObjName",
                                      "PrefixVarObjName", "NoPrefixVarObjName"};
const char *const BB_TYPE_LABELS[]{"BYTE",  "UBYTE",  "WORD",   "UWORD",         "DWORD",
                                   "UDWORD", "FLOAT", "DOUBLE", "UNKNOWN_DOUBLE"};

/**
 * @brief Draws a labelled combo bound to an int-backed enum value.
 * @return true if the selection changed.
 */
bool EnumCombo(const char *par_Label, int *par_Value, const char *const *par_Items, int par_Count)
{
    bool changed{false};
    const int current{(*par_Value >= 0 && *par_Value < par_Count) ? *par_Value : 0};
    if (ImGui::BeginCombo(par_Label, par_Items[current]))
    {
        for (int i = 0; i < par_Count; ++i)
        {
            const bool selected{i == current};
            if (ImGui::Selectable(par_Items[i], selected))
            {
                *par_Value = i;
                changed = true;
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

/**
 * @brief Draws a hex text field editing an unsigned identifier.
 */
void HexField(const char *par_Label, uint32_t *par_Value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%X", *par_Value);
    if (ImGui::InputText(par_Label, buffer, sizeof(buffer), ImGuiInputTextFlags_CharsHexadecimal |
                                                                ImGuiInputTextFlags_CharsNoBlank))
    {
        *par_Value = static_cast<uint32_t>(std::strtoul(buffer, nullptr, 16));
    }
}

/**
 * @brief Reads an ini string entry into a std::string.
 */
std::string ReadIniString(const char *par_Section, const char *par_Entry, const char *par_Default,
                          int par_Fd)
{
    char buffer[4096];
    buffer[0] = '\0';
    (void)IniFileDataBaseReadString(par_Section, par_Entry, par_Default, buffer,
                                    static_cast<int>(sizeof(buffer)), par_Fd);
    return std::string{buffer};
}

/**
 * @brief Bounded copy of a std::string into a fixed-size char array.
 */
template <std::size_t N>
void CopyToArray(std::array<char, N> &par_Dst, const std::string &par_Src)
{
    std::snprintf(par_Dst.data(), N, "%s", par_Src.c_str());
}

bool EqualsNoCase(const std::string &par_A, const char *par_B)
{
    return 0 == strcasecmp(par_A.c_str(), par_B);
}

// -----------------------------------------------------------------------------
// Signal read/write
// -----------------------------------------------------------------------------

CanTreeSignal ReadSignal(int par_Variant, int par_Object, int par_Signal, int par_Fd, bool &out_Exists)
{
    CanTreeSignal signal;
    char section[256];
    std::snprintf(section, sizeof(section), "CAN/Variante_%d/Object_%d/Signal_%d", par_Variant,
                  par_Object, par_Signal);
    if (0 == IniFileDataBaseLookIfSectionExist(section, par_Fd))
    {
        out_Exists = false;
        return signal;
    }
    out_Exists = true;

    signal.name = ReadIniString(section, "name", "", par_Fd);
    signal.description = ReadIniString(section, "desc", "", par_Fd);
    signal.unit = ReadIniString(section, "unit", "", par_Fd);

    const std::string convType{ReadIniString(section, "convtype", "mx+b", par_Fd)};
    if (EqualsNoCase(convType, "m(x+b)"))
    {
        signal.conversion = CanSignalConversion::AddOffsetMultiplyFactor;
    }
    else if (EqualsNoCase(convType, "curve"))
    {
        signal.conversion = CanSignalConversion::Curve;
    }
    else if (EqualsNoCase(convType, "equation"))
    {
        signal.conversion = CanSignalConversion::Equation;
    }
    else
    {
        signal.conversion = CanSignalConversion::MultiplyFactorAddOffset;
    }

    signal.factor = ReadIniString(section, "convert", "1", par_Fd);
    signal.offset = ReadIniString(section, "offset", "0", par_Fd);
    signal.convString = ReadIniString(section, "convstring", "", par_Fd);
    signal.startBit = IniFileDataBaseReadInt(section, "startbit", 0, par_Fd);
    signal.bitSize = IniFileDataBaseReadInt(section, "bitsize", 8, par_Fd);

    const std::string byteOrder{ReadIniString(section, "byteorder", "", par_Fd)};
    signal.byteOrder = ("lsb_first" == byteOrder) ? CanByteOrder::LsbFirst : CanByteOrder::MsbFirst;

    signal.startValueEnable = (0 != IniFileDataBaseReadYesNo(section, "startvalue active", 1, par_Fd));
    signal.startValue = ReadIniString(section, "startvalue", "0", par_Fd);

    const std::string type{ReadIniString(section, "type", "", par_Fd)};
    if (EqualsNoCase(type, "mux_by"))
    {
        signal.signalType = CanSignalType::MultiplexedBy;
        signal.muxBySignal = ReadIniString(section, "mux_by_signal", "", par_Fd);
        signal.muxStartBit = 0;
        signal.muxBitSize = 0;
        signal.muxValue = IniFileDataBaseReadInt(section, "mux_value", 0, par_Fd);
    }
    else if (EqualsNoCase(type, "mux"))
    {
        signal.signalType = CanSignalType::Multiplexed;
        signal.muxStartBit = IniFileDataBaseReadInt(section, "mux_startbit", 0, par_Fd);
        signal.muxBitSize = IniFileDataBaseReadInt(section, "mux_bitsize", 1, par_Fd);
        signal.muxValue = IniFileDataBaseReadInt(section, "mux_value", 0, par_Fd);
    }
    else
    {
        signal.signalType = CanSignalType::Normal;
    }

    signal.bbType = ReadIniString(section, "bbtype", "UNKNOWN_DOUBLE", par_Fd);

    const std::string sign{ReadIniString(section, "sign", "unsigned", par_Fd)};
    if ("signed" == sign)
    {
        signal.sign = CanSignalSign::Signed;
    }
    else if ("float" == sign)
    {
        signal.sign = CanSignalSign::Float;
    }
    else
    {
        signal.sign = CanSignalSign::Unsigned;
    }
    return signal;
}

void WriteSignal(const CanTreeSignal &par_Signal, int par_VariantNr, int par_ObjectNr,
                 int par_SignalNr, int par_Fd)
{
    char section[256];
    std::snprintf(section, sizeof(section), "CAN/Variante_%d/Object_%d/Signal_%d", par_VariantNr,
                  par_ObjectNr, par_SignalNr);
    (void)IniFileDataBaseWriteString(section, nullptr, nullptr, par_Fd);

    (void)IniFileDataBaseWriteString(section, "name", par_Signal.name.c_str(), par_Fd);
    (void)IniFileDataBaseWriteString(section, "desc", par_Signal.description.c_str(), par_Fd);
    (void)IniFileDataBaseWriteString(section, "unit", par_Signal.unit.c_str(), par_Fd);
    switch (par_Signal.conversion)
    {
    case CanSignalConversion::AddOffsetMultiplyFactor:
        (void)IniFileDataBaseWriteString(section, "convtype", "m(x+b)", par_Fd);
        break;
    case CanSignalConversion::Curve:
        (void)IniFileDataBaseWriteString(section, "convtype", "curve", par_Fd);
        break;
    case CanSignalConversion::Equation:
        (void)IniFileDataBaseWriteString(section, "convtype", "equation", par_Fd);
        break;
    case CanSignalConversion::MultiplyFactorAddOffset:
    default:
        (void)IniFileDataBaseWriteString(section, "convtype", "mx+b", par_Fd);
        break;
    }
    (void)IniFileDataBaseWriteString(section, "convert", par_Signal.factor.c_str(), par_Fd);
    (void)IniFileDataBaseWriteString(section, "offset", par_Signal.offset.c_str(), par_Fd);
    (void)IniFileDataBaseWriteString(section, "convstring", par_Signal.convString.c_str(), par_Fd);
    (void)IniFileDataBaseWriteInt(section, "startbit", par_Signal.startBit, par_Fd);
    (void)IniFileDataBaseWriteInt(section, "bitsize", par_Signal.bitSize, par_Fd);
    (void)IniFileDataBaseWriteString(section, "byteorder",
                                     CanByteOrder::LsbFirst == par_Signal.byteOrder ? "lsb_first"
                                                                                    : "msb_first",
                                     par_Fd);
    (void)IniFileDataBaseWriteYesNo(section, "startvalue active", par_Signal.startValueEnable ? 1 : 0,
                                    par_Fd);
    (void)IniFileDataBaseWriteString(section, "startvalue", par_Signal.startValue.c_str(), par_Fd);
    switch (par_Signal.signalType)
    {
    case CanSignalType::MultiplexedBy:
        (void)IniFileDataBaseWriteString(section, "type", "mux_by", par_Fd);
        (void)IniFileDataBaseWriteString(section, "mux_by_signal", par_Signal.muxBySignal.c_str(),
                                         par_Fd);
        break;
    case CanSignalType::Multiplexed:
        (void)IniFileDataBaseWriteString(section, "type", "mux", par_Fd);
        break;
    case CanSignalType::Normal:
    default:
        (void)IniFileDataBaseWriteString(section, "type", "normal", par_Fd);
        break;
    }
    (void)IniFileDataBaseWriteInt(section, "mux_startbit", par_Signal.muxStartBit, par_Fd);
    (void)IniFileDataBaseWriteInt(section, "mux_bitsize", par_Signal.muxBitSize, par_Fd);
    (void)IniFileDataBaseWriteInt(section, "mux_value", par_Signal.muxValue, par_Fd);
    (void)IniFileDataBaseWriteString(section, "bbtype", par_Signal.bbType.c_str(), par_Fd);
    switch (par_Signal.sign)
    {
    case CanSignalSign::Signed:
        (void)IniFileDataBaseWriteString(section, "sign", "signed", par_Fd);
        break;
    case CanSignalSign::Float:
        (void)IniFileDataBaseWriteString(section, "sign", "float", par_Fd);
        break;
    case CanSignalSign::Unsigned:
    default:
        (void)IniFileDataBaseWriteString(section, "sign", "unsigned", par_Fd);
        break;
    }
}

// -----------------------------------------------------------------------------
// Object read/write
// -----------------------------------------------------------------------------

CanTreeObject ReadObject(int par_Variant, int par_Object, int par_Fd, bool &out_Exists)
{
    CanTreeObject object;
    char section[256];
    std::snprintf(section, sizeof(section), "CAN/Variante_%d/Object_%d", par_Variant, par_Object);
    if (0 == IniFileDataBaseLookIfSectionExist(section, par_Fd))
    {
        out_Exists = false;
        return object;
    }
    out_Exists = true;

    object.name = ReadIniString(section, "name", "", par_Fd);
    object.description = ReadIniString(section, "desc", "", par_Fd);
    const std::string id{ReadIniString(section, "id", "", par_Fd)};
    object.identifier = static_cast<uint32_t>(std::strtoul(id.c_str(), nullptr, 0));
    object.variableId = ReadIniString(section, "variable_id", "", par_Fd);
    object.extendedId = (0 != IniFileDataBaseReadYesNo(section, "extended", 0, par_Fd));
    object.bitRateSwitch = (0 != IniFileDataBaseReadYesNo(section, "bit_rate_switch", 0, par_Fd));
    object.fdFrameFormat = (0 != IniFileDataBaseReadYesNo(section, "fd_frame_format_switch", 0, par_Fd));
    const std::string byteOrder{ReadIniString(section, "byteorder", "lsb_first", par_Fd)};
    object.byteOrder = ("msb_first" == byteOrder) ? CanByteOrder::MsbFirst : CanByteOrder::LsbFirst;
    object.size = IniFileDataBaseReadInt(section, "size", 1, par_Fd);
    if (0 > object.size)
    {
        object.size = 0;
    }
    const std::string direction{ReadIniString(section, "direction", "read", par_Fd)};
    if ("read_startup_variable_id" == direction)
    {
        object.direction = CanObjectDirection::ReadStartupVariableId;
    }
    else if ("write_variable_id" == direction)
    {
        object.direction = CanObjectDirection::WriteVariableId;
    }
    else if ("write" == direction)
    {
        object.direction = CanObjectDirection::Write;
    }
    else
    {
        object.direction = CanObjectDirection::Read;
    }

    const std::string type{ReadIniString(section, "type", "", par_Fd)};
    if (EqualsNoCase(type, "mux"))
    {
        object.type = CanObjectType::Multiplexed;
    }
    else if (EqualsNoCase(type, "j1939"))
    {
        object.type = CanObjectType::J1939;
        const std::string variableDlc{ReadIniString(section, "variable_dlc", "", par_Fd)};
        object.variableDlc = !variableDlc.empty();
        object.variableDlcName = variableDlc;
        const std::string variableDst{ReadIniString(section, "variable_dst_addr", "", par_Fd)};
        if (!variableDst.empty())
        {
            object.destAddrType = CanObjectDestAddr::Variable;
            object.variableDestAddrName = variableDst;
            object.variableDestAddrInit = IniFileDataBaseReadUInt(section, "init_dst_addr", 0xFF, par_Fd);
        }
        else
        {
            object.destAddrType = CanObjectDestAddr::Fixed;
            object.fixedDestAddr = IniFileDataBaseReadUInt(section, "fixed_dst_addr", 0xFF, par_Fd);
        }
    }
    else if (EqualsNoCase(type, "j1939_multi_c_pg"))
    {
        object.type = CanObjectType::J1939MultiPG;
    }
    else if (EqualsNoCase(type, "j1939_c_pg"))
    {
        object.type = CanObjectType::J1939CPG;
    }
    else
    {
        object.type = CanObjectType::Normal;
    }

    object.muxStartBit = IniFileDataBaseReadInt(section, "mux_startbit", 0, par_Fd);
    object.muxBitSize = IniFileDataBaseReadInt(section, "mux_bitsize", 0, par_Fd);
    object.muxValue = IniFileDataBaseReadInt(section, "mux_value", 0, par_Fd);

    const std::string cyclicOrEvent{ReadIniString(section, "CyclicOrEvent", "Cyclic", par_Fd)};
    object.transmitType =
        EqualsNoCase(cyclicOrEvent, "Event") ? CanObjectTransmit::Equation : CanObjectTransmit::Cyclic;
    object.transmitCycles = ReadIniString(section, "cycles", "1", par_Fd);
    object.transmitDelay = ReadIniString(section, "delay", "0", par_Fd);
    object.transmitEquation = ReadIniString(section, "EventEquation", "", par_Fd);

    for (int i = 0;; ++i)
    {
        char entry[64];
        std::snprintf(entry, sizeof(entry), "additional_variable_%d", i);
        const std::string value{ReadIniString(section, entry, "", par_Fd)};
        if (value.empty())
        {
            break;
        }
        object.additionalVariables.push_back(value);
    }
    for (int i = 0;; ++i)
    {
        char entry[64];
        std::snprintf(entry, sizeof(entry), "equ_before_%d", i);
        const std::string value{ReadIniString(section, entry, "", par_Fd)};
        if (value.empty())
        {
            break;
        }
        object.equationsBefore.push_back(value);
    }
    for (int i = 0;; ++i)
    {
        char entry[64];
        std::snprintf(entry, sizeof(entry), "equ_behind_%d", i);
        const std::string value{ReadIniString(section, entry, "", par_Fd)};
        if (value.empty())
        {
            break;
        }
        object.equationsBehind.push_back(value);
    }

    const std::string initData{ReadIniString(section, "InitData", "", par_Fd)};
    object.initValue = initData.empty() ? 0u : std::strtoull(initData.c_str(), nullptr, 0);

    if (CanObjectType::J1939MultiPG == object.type)
    {
        const std::string cpgs{ReadIniString(section, "C_PGs", "", par_Fd)};
        const char *cursor{cpgs.c_str()};
        while ('\0' != *cursor)
        {
            char *next{nullptr};
            const long id{std::strtol(cursor, &next, 0)};
            if ((nullptr == next) || (next == cursor))
            {
                break;
            }
            object.cPgIds.push_back(static_cast<int>(id));
            cursor = next;
            while ((0 != std::isspace(static_cast<unsigned char>(*cursor))))
            {
                ++cursor;
            }
            if (',' != *cursor)
            {
                break;
            }
            ++cursor;
        }
    }

    for (int signalNr = 0;; ++signalNr)
    {
        bool exists{false};
        CanTreeSignal signal{ReadSignal(par_Variant, par_Object, signalNr, par_Fd, exists)};
        if (!exists)
        {
            break;
        }
        object.signals.push_back(std::move(signal));
    }
    return object;
}

void WriteObject(const CanTreeObject &par_Object, int par_VariantNr, int par_ObjectNr, int par_Fd)
{
    char section[256];
    std::snprintf(section, sizeof(section), "CAN/Variante_%d/Object_%d", par_VariantNr, par_ObjectNr);
    (void)IniFileDataBaseWriteString(section, nullptr, nullptr, par_Fd);

    (void)IniFileDataBaseWriteString(section, "name", par_Object.name.c_str(), par_Fd);
    (void)IniFileDataBaseWriteString(section, "desc", par_Object.description.c_str(), par_Fd);
    char idText[32];
    std::snprintf(idText, sizeof(idText), "0x%X", par_Object.identifier);
    (void)IniFileDataBaseWriteString(section, "id", idText, par_Fd);
    if (!par_Object.variableId.empty())
    {
        (void)IniFileDataBaseWriteString(section, "variable_id", par_Object.variableId.c_str(), par_Fd);
    }
    (void)IniFileDataBaseWriteYesNo(section, "extended", par_Object.extendedId ? 1 : 0, par_Fd);
    (void)IniFileDataBaseWriteYesNo(section, "bit_rate_switch", par_Object.bitRateSwitch ? 1 : 0, par_Fd);
    (void)IniFileDataBaseWriteYesNo(section, "fd_frame_format_switch",
                                    par_Object.fdFrameFormat ? 1 : 0, par_Fd);
    (void)IniFileDataBaseWriteString(section, "byteorder",
                                     CanByteOrder::MsbFirst == par_Object.byteOrder ? "msb_first"
                                                                                    : "lsb_first",
                                     par_Fd);
    (void)IniFileDataBaseWriteInt(section, "size", par_Object.size, par_Fd);
    switch (par_Object.direction)
    {
    case CanObjectDirection::ReadStartupVariableId:
        (void)IniFileDataBaseWriteString(section, "direction", "read_startup_variable_id", par_Fd);
        break;
    case CanObjectDirection::WriteVariableId:
        (void)IniFileDataBaseWriteString(section, "direction", "write_variable_id", par_Fd);
        break;
    case CanObjectDirection::Write:
        (void)IniFileDataBaseWriteString(section, "direction", "write", par_Fd);
        break;
    case CanObjectDirection::Read:
    default:
        (void)IniFileDataBaseWriteString(section, "direction", "read", par_Fd);
        break;
    }
    switch (par_Object.type)
    {
    case CanObjectType::Multiplexed:
        (void)IniFileDataBaseWriteString(section, "type", "mux", par_Fd);
        break;
    case CanObjectType::J1939:
        (void)IniFileDataBaseWriteString(section, "type", "j1939", par_Fd);
        if (par_Object.variableDlc)
        {
            (void)IniFileDataBaseWriteString(section, "variable_dlc", par_Object.variableDlcName.c_str(),
                                             par_Fd);
        }
        if (CanObjectDestAddr::Variable == par_Object.destAddrType)
        {
            (void)IniFileDataBaseWriteString(section, "variable_dst_addr",
                                             par_Object.variableDestAddrName.c_str(), par_Fd);
            (void)IniFileDataBaseWriteUInt(section, "init_dst_addr", par_Object.variableDestAddrInit,
                                           par_Fd);
        }
        else
        {
            (void)IniFileDataBaseWriteUInt(section, "fixed_dst_addr", par_Object.fixedDestAddr, par_Fd);
        }
        break;
    case CanObjectType::J1939MultiPG:
        (void)IniFileDataBaseWriteString(section, "type", "j1939_multi_c_pg", par_Fd);
        break;
    case CanObjectType::J1939CPG:
        (void)IniFileDataBaseWriteString(section, "type", "j1939_c_pg", par_Fd);
        break;
    case CanObjectType::Normal:
    default:
        (void)IniFileDataBaseWriteString(section, "type", "normal", par_Fd);
        break;
    }
    (void)IniFileDataBaseWriteInt(section, "mux_startbit", par_Object.muxStartBit, par_Fd);
    (void)IniFileDataBaseWriteInt(section, "mux_bitsize", par_Object.muxBitSize, par_Fd);
    (void)IniFileDataBaseWriteInt(section, "mux_value", par_Object.muxValue, par_Fd);
    if (CanObjectTransmit::Equation == par_Object.transmitType)
    {
        (void)IniFileDataBaseWriteString(section, "CyclicOrEvent", "Event", par_Fd);
        (void)IniFileDataBaseWriteString(section, "EventEquation", par_Object.transmitEquation.c_str(),
                                         par_Fd);
    }
    else
    {
        (void)IniFileDataBaseWriteString(section, "CyclicOrEvent", "Cyclic", par_Fd);
    }
    (void)IniFileDataBaseWriteString(section, "cycles", par_Object.transmitCycles.c_str(), par_Fd);
    (void)IniFileDataBaseWriteString(section, "delay", par_Object.transmitDelay.c_str(), par_Fd);

    for (std::size_t i = 0; i < par_Object.additionalVariables.size(); ++i)
    {
        char entry[64];
        std::snprintf(entry, sizeof(entry), "additional_variable_%zu", i);
        (void)IniFileDataBaseWriteString(section, entry, par_Object.additionalVariables[i].c_str(),
                                         par_Fd);
    }
    for (std::size_t i = 0; i < par_Object.equationsBefore.size(); ++i)
    {
        char entry[64];
        std::snprintf(entry, sizeof(entry), "equ_before_%zu", i);
        (void)IniFileDataBaseWriteString(section, entry, par_Object.equationsBefore[i].c_str(), par_Fd);
    }
    for (std::size_t i = 0; i < par_Object.equationsBehind.size(); ++i)
    {
        char entry[64];
        std::snprintf(entry, sizeof(entry), "equ_behind_%zu", i);
        (void)IniFileDataBaseWriteString(section, entry, par_Object.equationsBehind[i].c_str(), par_Fd);
    }

    char initText[48];
    std::snprintf(initText, sizeof(initText), "0x%llX",
                  static_cast<unsigned long long>(par_Object.initValue));
    (void)IniFileDataBaseWriteString(section, "InitData", initText, par_Fd);

    if (CanObjectType::J1939MultiPG == par_Object.type)
    {
        std::string cpgs;
        char buf[32];
        for (std::size_t i = 0; i < par_Object.cPgIds.size(); ++i)
        {
            std::snprintf(buf, sizeof(buf), (0 == i) ? "0x%X" : ", 0x%X", par_Object.cPgIds[i]);
            cpgs += buf;
        }
        (void)IniFileDataBaseWriteString(section, "C_PGs", cpgs.c_str(), par_Fd);
    }

    for (std::size_t signalNr = 0; signalNr < par_Object.signals.size(); ++signalNr)
    {
        WriteSignal(par_Object.signals[signalNr], par_VariantNr, par_ObjectNr,
                    static_cast<int>(signalNr), par_Fd);
    }
    (void)IniFileDataBaseWriteInt(section, "signal_count",
                                  static_cast<int>(par_Object.signals.size()), par_Fd);
}

// -----------------------------------------------------------------------------
// Variant read/write
// -----------------------------------------------------------------------------

CanTreeVariant ReadVariant(int par_Variant, int par_Fd, bool &out_Exists)
{
    CanTreeVariant variant;
    char section[128];
    std::snprintf(section, sizeof(section), "CAN/Variante_%d", par_Variant);
    if (0 == IniFileDataBaseLookIfSectionExist(section, par_Fd))
    {
        out_Exists = false;
        return variant;
    }
    out_Exists = true;

    variant.name = ReadIniString(section, "name", "", par_Fd);
    variant.description = ReadIniString(section, "desc", "", par_Fd);
    variant.baudRate = IniFileDataBaseReadInt(section, "baud rate", 500, par_Fd);
    variant.samplePoint = IniFileDataBaseReadFloat(section, "sample point", 0.75, par_Fd);
    variant.canFdEnable = (0 != IniFileDataBaseReadYesNo(section, "can fd", 0, par_Fd));
    variant.dataBaudRate = IniFileDataBaseReadInt(section, "data baud rate", 4000, par_Fd);
    variant.dataSamplePoint = IniFileDataBaseReadFloat(section, "data sample point", 0.8, par_Fd);
    variant.enableJ1939 = (0 != IniFileDataBaseReadYesNo(section, "j1939", 0, par_Fd));

    const std::string controlBb{ReadIniString(section, "ControlBbName", "PrefixId", par_Fd)};
    if (EqualsNoCase(controlBb, "NoPrefixObjName"))
    {
        variant.controlBbName = CanControlBbName::NoPrefixObjName;
    }
    else if (EqualsNoCase(controlBb, "PrefixObjName"))
    {
        variant.controlBbName = CanControlBbName::PrefixObjName;
    }
    else if (EqualsNoCase(controlBb, "NoPrefixVarObjName"))
    {
        variant.controlBbName = CanControlBbName::NoPrefixVarObjName;
    }
    else if (EqualsNoCase(controlBb, "PrefixVarObjName"))
    {
        variant.controlBbName = CanControlBbName::PrefixVarObjName;
    }
    else
    {
        variant.controlBbName = CanControlBbName::PrefixId;
    }

    for (int objectNr = 0;; ++objectNr)
    {
        bool exists{false};
        CanTreeObject object{ReadObject(par_Variant, objectNr, par_Fd, exists)};
        if (!exists)
        {
            break;
        }
        variant.objects.push_back(std::move(object));
    }
    return variant;
}

void WriteVariant(const CanTreeVariant &par_Variant, int par_VariantNr, int par_Fd)
{
    char section[128];
    std::snprintf(section, sizeof(section), "CAN/Variante_%d", par_VariantNr);
    (void)IniFileDataBaseWriteString(section, nullptr, nullptr, par_Fd);

    (void)IniFileDataBaseWriteString(section, "name", par_Variant.name.c_str(), par_Fd);
    (void)IniFileDataBaseWriteString(section, "desc", par_Variant.description.c_str(), par_Fd);
    (void)IniFileDataBaseWriteInt(section, "baud rate", par_Variant.baudRate, par_Fd);
    (void)IniFileDataBaseWriteFloat(section, "sample point", par_Variant.samplePoint, par_Fd);
    (void)IniFileDataBaseWriteYesNo(section, "can fd", par_Variant.canFdEnable ? 1 : 0, par_Fd);
    (void)IniFileDataBaseWriteInt(section, "data baud rate", par_Variant.dataBaudRate, par_Fd);
    (void)IniFileDataBaseWriteFloat(section, "data sample point", par_Variant.dataSamplePoint, par_Fd);
    (void)IniFileDataBaseWriteYesNo(section, "j1939", par_Variant.enableJ1939 ? 1 : 0, par_Fd);
    switch (par_Variant.controlBbName)
    {
    case CanControlBbName::NoPrefixObjName:
        (void)IniFileDataBaseWriteString(section, "ControlBbName", "NoPrefixObjName", par_Fd);
        break;
    case CanControlBbName::PrefixObjName:
        (void)IniFileDataBaseWriteString(section, "ControlBbName", "PrefixObjName", par_Fd);
        break;
    case CanControlBbName::NoPrefixVarObjName:
        (void)IniFileDataBaseWriteString(section, "ControlBbName", "NoPrefixVarObjName", par_Fd);
        break;
    case CanControlBbName::PrefixVarObjName:
        (void)IniFileDataBaseWriteString(section, "ControlBbName", "PrefixVarObjName", par_Fd);
        break;
    case CanControlBbName::PrefixId:
    default:
        (void)IniFileDataBaseWriteString(section, "ControlBbName", "PrefixId", par_Fd);
        break;
    }
    (void)IniFileDataBaseWriteInt(section, "can_object_count",
                                  static_cast<int>(par_Variant.objects.size()), par_Fd);
    for (std::size_t objectNr = 0; objectNr < par_Variant.objects.size(); ++objectNr)
    {
        WriteObject(par_Variant.objects[objectNr], par_VariantNr, static_cast<int>(objectNr), par_Fd);
    }
}

} // namespace

std::vector<CanTreeVariant> SnapshotCanVariantsFromIni(int &out_NumberOfChannels)
{
    const int fd{GetMainFileDescriptor()};
    std::vector<CanTreeVariant> variants;

    const int channelCountExt{IniFileDataBaseReadInt("CAN/Global", "can_controller_count_ext", 0, fd)};
    const int channelCount{IniFileDataBaseReadInt("CAN/Global", "can_controller_count", 0, fd)};
    int numberOfChannels{(channelCountExt > channelCount) ? channelCountExt : channelCount};
    if (1 > numberOfChannels)
    {
        numberOfChannels = 1;
    }
    if (MAX_CAN_CHANNELS < numberOfChannels)
    {
        numberOfChannels = MAX_CAN_CHANNELS;
    }
    out_NumberOfChannels = numberOfChannels;

    for (int variantNr = 0;; ++variantNr)
    {
        bool exists{false};
        CanTreeVariant variant{ReadVariant(variantNr, fd, exists)};
        if (!exists)
        {
            break;
        }
        variants.push_back(std::move(variant));
    }

    for (int c = 0; c < numberOfChannels; ++c)
    {
        char entry[64];
        std::snprintf(entry, sizeof(entry), "can_controller%d_variante", c + 1);
        const std::string list{ReadIniString("CAN/Global", entry, "", fd)};
        const char *cursor{list.c_str()};
        while (0 != std::isdigit(static_cast<unsigned char>(*cursor)))
        {
            char *next{nullptr};
            const long v{std::strtol(cursor, &next, 0)};
            if ((v >= 0) && (v < static_cast<long>(variants.size())))
            {
                variants[static_cast<std::size_t>(v)].connectedChannels.at(
                    static_cast<std::size_t>(c)) = true;
            }
            cursor = next;
            if (',' == *cursor)
            {
                ++cursor;
            }
        }
    }
    return variants;
}

// =============================================================================
// CanConfigTreeManager
// =============================================================================

CanConfigTreeManager &CanConfigTreeManager::GetInstance()
{
    static CanConfigTreeManager instance;
    return instance;
}

void CanConfigTreeManager::OpenEditor()
{
    SnapshotFromIni();
    m_open = true;
    m_openRequested = true;
}

void CanConfigTreeManager::SnapshotFromIni()
{
    const int fd{GetMainFileDescriptor()};
    m_variants.clear();
    m_channels = std::array<CanTreeChannel, 16>{};

    int channelCountExt{IniFileDataBaseReadInt("CAN/Global", "can_controller_count_ext", 0, fd)};
    int channelCount{IniFileDataBaseReadInt("CAN/Global", "can_controller_count", 0, fd)};
    m_numberOfChannels = (channelCountExt > channelCount) ? channelCountExt : channelCount;
    if (1 > m_numberOfChannels)
    {
        m_numberOfChannels = 1;
    }
    if (MAX_CAN_CHANNELS < m_numberOfChannels)
    {
        m_numberOfChannels = MAX_CAN_CHANNELS;
    }

    m_doNotUseUnits = (0 != IniFileDataBaseReadInt("CAN/Global", "dont use units for blackboard", 0, fd));
    m_restartImmediately = (0 != IniFileDataBaseReadInt("CAN/Global", "restart CAN immediately", 0, fd));
    m_enableGatewayDeviceDriver =
        (0 != IniFileDataBaseReadYesNo("CAN/Global", "enable gateway device driver", 0, fd));
    if (m_enableGatewayDeviceDriver)
    {
        m_enableGatewayVirtual =
            static_cast<int>(IniFileDataBaseReadUInt("CAN/Global", "enable gateway virtual device driver", 1u, fd));
        m_enableGatewayReal =
            static_cast<int>(IniFileDataBaseReadUInt("CAN/Global", "enable gateway real device driver", 1u, fd));
        m_enableGatewayVirtualEqu =
            ReadIniString("CAN/Global", "enable gateway virtual device driver equ", "", fd);
        m_enableGatewayRealEqu =
            ReadIniString("CAN/Global", "enable gateway real device driver equ", "", fd);
    }
    else
    {
        m_enableGatewayVirtual = 1;
        m_enableGatewayReal = 1;
    }

    // Read variants (index == variant number, as m_OnlyUsedVariants is off).
    for (int variantNr = 0;; ++variantNr)
    {
        bool exists{false};
        CanTreeVariant variant{ReadVariant(variantNr, fd, exists)};
        if (!exists)
        {
            break;
        }
        m_variants.push_back(std::move(variant));
    }

    // Read channel configuration and channel<->variant mapping.
    for (int c = 0; c < m_numberOfChannels; ++c)
    {
        CanTreeChannel &channel = m_channels.at(static_cast<std::size_t>(c));
        char entry[64];
        std::snprintf(entry, sizeof(entry), "can_controller%d_startup_state", c + 1);
        channel.startupState = IniFileDataBaseReadInt("CAN/Global", entry, 1, fd);
        std::snprintf(entry, sizeof(entry), "can_controller%d_baudrate_config", c + 1);
        channel.busConfig = IniFileDataBaseReadInt("CAN/Global", entry, 1, fd);
        if (BUS_CFG_USER == (channel.busConfig & BUS_CFG_MODE_MASK))
        {
            std::snprintf(entry, sizeof(entry), "can_controller%d_user_config_fd", c + 1);
            channel.fd = (0 != IniFileDataBaseReadYesNo("CAN/Global", entry, 1, fd));
            std::snprintf(entry, sizeof(entry), "can_controller%d_user_config_baud_rate", c + 1);
            channel.baudRate = IniFileDataBaseReadInt("CAN/Global", entry, 500, fd);
            std::snprintf(entry, sizeof(entry), "can_controller%d_user_config_sample_point", c + 1);
            channel.samplePoint = IniFileDataBaseReadFloat("CAN/Global", entry, 0.75, fd);
            std::snprintf(entry, sizeof(entry), "can_controller%d_user_config_data_baud_rate", c + 1);
            channel.dataBaudRate = IniFileDataBaseReadInt("CAN/Global", entry, 4000, fd);
            std::snprintf(entry, sizeof(entry), "can_controller%d_user_config_data_sample_point", c + 1);
            channel.dataSamplePoint = IniFileDataBaseReadFloat("CAN/Global", entry, 0.8, fd);
        }
        std::snprintf(entry, sizeof(entry), "can_controller%d_variante", c + 1);
        const std::string list{ReadIniString("CAN/Global", entry, "", fd)};
        const char *cursor{list.c_str()};
        while (0 != std::isdigit(static_cast<unsigned char>(*cursor)))
        {
            char *next{nullptr};
            const long v{std::strtol(cursor, &next, 0)};
            if ((v >= 0) && (v < static_cast<long>(m_variants.size())))
            {
                m_variants[static_cast<std::size_t>(v)].connectedChannels.at(static_cast<std::size_t>(c)) =
                    true;
            }
            cursor = next;
            if (',' == *cursor)
            {
                ++cursor;
            }
        }
    }

    m_selectionKind = CanTreeSelectionKind::None;
    m_selVariant = -1;
    m_selObject = -1;
    m_selSignal = -1;
    if (!m_variants.empty())
    {
        SelectVariant(0);
    }
}

void CanConfigTreeManager::ApplyToIni()
{
    const int fd{GetMainFileDescriptor()};

    // Delete every existing CAN/Global + Variante/Object/Signal section first (Qt SaveToIni).
    if (0 != IniFileDataBaseLookIfSectionExist("CAN/Global", fd))
    {
        (void)IniFileDataBaseWriteString("CAN/Global", nullptr, nullptr, fd);
        for (int v = 0;; ++v)
        {
            char section[256];
            std::snprintf(section, sizeof(section), "CAN/Variante_%d", v);
            if (0 == IniFileDataBaseLookIfSectionExist(section, fd))
            {
                break;
            }
            (void)IniFileDataBaseWriteString(section, nullptr, nullptr, fd);
            for (int o = 0;; ++o)
            {
                std::snprintf(section, sizeof(section), "CAN/Variante_%d/Object_%d", v, o);
                if (0 == IniFileDataBaseLookIfSectionExist(section, fd))
                {
                    break;
                }
                (void)IniFileDataBaseWriteString(section, nullptr, nullptr, fd);
                for (int s = 0;; ++s)
                {
                    std::snprintf(section, sizeof(section), "CAN/Variante_%d/Object_%d/Signal_%d", v,
                                  o, s);
                    if (0 == IniFileDataBaseLookIfSectionExist(section, fd))
                    {
                        break;
                    }
                    (void)IniFileDataBaseWriteString(section, nullptr, nullptr, fd);
                }
            }
        }
    }

    // Write global values.
    (void)IniFileDataBaseWriteInt("CAN/Global", "can_controller_count_ext", m_numberOfChannels, fd);
    (void)IniFileDataBaseWriteInt("CAN/Global", "can_controller_count",
                                  (m_numberOfChannels > 4) ? 4 : m_numberOfChannels, fd);
    (void)IniFileDataBaseWriteInt("CAN/Global", "can_varianten_count",
                                  static_cast<int>(m_variants.size()), fd);
    (void)IniFileDataBaseWriteString("CAN/Global", "dont use units for blackboard",
                                     m_doNotUseUnits ? "1" : "0", fd);
    (void)IniFileDataBaseWriteString("CAN/Global", "restart CAN immediately",
                                     m_restartImmediately ? "1" : "0", fd);
    (void)IniFileDataBaseWriteYesNo("CAN/Global", "enable gateway device driver",
                                    m_enableGatewayDeviceDriver ? 1 : 0, fd);
    if (m_enableGatewayDeviceDriver)
    {
        (void)IniFileDataBaseWriteUInt("CAN/Global", "enable gateway virtual device driver",
                                       static_cast<unsigned int>(m_enableGatewayVirtual), fd);
        (void)IniFileDataBaseWriteUInt("CAN/Global", "enable gateway real device driver",
                                       static_cast<unsigned int>(m_enableGatewayReal), fd);
        (void)IniFileDataBaseWriteString("CAN/Global", "enable gateway virtual device driver equ",
                                         m_enableGatewayVirtualEqu.c_str(), fd);
        (void)IniFileDataBaseWriteString("CAN/Global", "enable gateway real device driver equ",
                                         m_enableGatewayRealEqu.c_str(), fd);
    }

    // Write variants.
    for (std::size_t variantNr = 0; variantNr < m_variants.size(); ++variantNr)
    {
        WriteVariant(m_variants[variantNr], static_cast<int>(variantNr), fd);
    }

    // Write channel configuration and channel<->variant mapping.
    for (int c = 0; c < m_numberOfChannels; ++c)
    {
        const CanTreeChannel &channel = m_channels.at(static_cast<std::size_t>(c));
        char entry[64];
        std::snprintf(entry, sizeof(entry), "can_controller%d_startup_state", c + 1);
        (void)IniFileDataBaseWriteInt("CAN/Global", entry, channel.startupState, fd);
        std::snprintf(entry, sizeof(entry), "can_controller%d_baudrate_config", c + 1);
        (void)IniFileDataBaseWriteInt("CAN/Global", entry, channel.busConfig, fd);
        if (BUS_CFG_USER == (channel.busConfig & BUS_CFG_MODE_MASK))
        {
            std::snprintf(entry, sizeof(entry), "can_controller%d_user_config_fd", c + 1);
            (void)IniFileDataBaseWriteYesNo("CAN/Global", entry, channel.fd ? 1 : 0, fd);
            std::snprintf(entry, sizeof(entry), "can_controller%d_user_config_baud_rate", c + 1);
            (void)IniFileDataBaseWriteInt("CAN/Global", entry, channel.baudRate, fd);
            std::snprintf(entry, sizeof(entry), "can_controller%d_user_config_sample_point", c + 1);
            (void)IniFileDataBaseWriteFloat("CAN/Global", entry, channel.samplePoint, fd);
            std::snprintf(entry, sizeof(entry), "can_controller%d_user_config_data_baud_rate", c + 1);
            (void)IniFileDataBaseWriteInt("CAN/Global", entry, channel.dataBaudRate, fd);
            std::snprintf(entry, sizeof(entry), "can_controller%d_user_config_data_sample_point", c + 1);
            (void)IniFileDataBaseWriteFloat("CAN/Global", entry, channel.dataSamplePoint, fd);
        }
        std::snprintf(entry, sizeof(entry), "can_controller%d_variante", c + 1);
        std::string list;
        for (std::size_t v = 0; v < m_variants.size(); ++v)
        {
            if (m_variants[v].connectedChannels.at(static_cast<std::size_t>(c)))
            {
                if (!list.empty())
                {
                    list += ",";
                }
                list += std::to_string(v);
            }
        }
        (void)IniFileDataBaseWriteString("CAN/Global", entry, list.c_str(), fd);
    }

    if (m_restartImmediately)
    {
        const PID pid{get_pid_by_name("CANServer")};
        if (pid > 0)
        {
            (void)write_message(pid, NEW_CANSERVER_STOP, 0, nullptr);
        }
    }
}

void CanConfigTreeManager::SelectVariant(int par_Variant)
{
    m_selectionKind = CanTreeSelectionKind::Variant;
    m_selVariant = par_Variant;
    m_selObject = -1;
    m_selSignal = -1;
}

void CanConfigTreeManager::SelectObject(int par_Variant, int par_Object)
{
    m_selectionKind = CanTreeSelectionKind::Object;
    m_selVariant = par_Variant;
    m_selObject = par_Object;
    m_selSignal = -1;
}

void CanConfigTreeManager::SelectSignal(int par_Variant, int par_Object, int par_Signal)
{
    m_selectionKind = CanTreeSelectionKind::Signal;
    m_selVariant = par_Variant;
    m_selObject = par_Object;
    m_selSignal = par_Signal;
}

void CanConfigTreeManager::RequestOp(CanTreePendingOp par_Op, int par_Variant, int par_Object,
                                     int par_Signal)
{
    m_pendingOp = par_Op;
    m_pendingVariant = par_Variant;
    m_pendingObject = par_Object;
    m_pendingSignal = par_Signal;
}

void CanConfigTreeManager::ApplyPendingOp()
{
    const CanTreePendingOp op{m_pendingOp};
    const int v{m_pendingVariant};
    const int o{m_pendingObject};
    const int s{m_pendingSignal};
    m_pendingOp = CanTreePendingOp::None;

    const auto validVariant{[&](int par_V) {
        return (par_V >= 0) && (par_V < static_cast<int>(m_variants.size()));
    }};
    const auto validObject{[&](int par_V, int par_O) {
        return validVariant(par_V) &&
               (par_O >= 0) &&
               (par_O < static_cast<int>(m_variants[static_cast<std::size_t>(par_V)].objects.size()));
    }};

    switch (op)
    {
    case CanTreePendingOp::AddVariant:
    {
        CanTreeVariant variant;
        variant.name = "Variant_" + std::to_string(m_variants.size());
        m_variants.push_back(std::move(variant));
        SelectVariant(static_cast<int>(m_variants.size()) - 1);
        break;
    }
    case CanTreePendingOp::DeleteVariant:
        if (validVariant(v))
        {
            m_variants.erase(m_variants.begin() + v);
            m_selectionKind = CanTreeSelectionKind::None;
            m_selVariant = -1;
        }
        break;
    case CanTreePendingOp::CopyVariant:
        if (validVariant(v))
        {
            m_clipboardVariant = m_variants[static_cast<std::size_t>(v)];
            m_clipboardKind = CanTreeSelectionKind::Variant;
        }
        break;
    case CanTreePendingOp::PasteVariant:
        if (CanTreeSelectionKind::Variant == m_clipboardKind)
        {
            m_variants.push_back(m_clipboardVariant);
            SelectVariant(static_cast<int>(m_variants.size()) - 1);
        }
        break;
    case CanTreePendingOp::AddObject:
        if (validVariant(v))
        {
            CanTreeObject object;
            object.name = "Object_" +
                          std::to_string(m_variants[static_cast<std::size_t>(v)].objects.size());
            m_variants[static_cast<std::size_t>(v)].objects.push_back(std::move(object));
            SelectObject(v,
                         static_cast<int>(m_variants[static_cast<std::size_t>(v)].objects.size()) - 1);
        }
        break;
    case CanTreePendingOp::DeleteObject:
        if (validObject(v, o))
        {
            auto &objects = m_variants[static_cast<std::size_t>(v)].objects;
            objects.erase(objects.begin() + o);
            SelectVariant(v);
        }
        break;
    case CanTreePendingOp::CopyObject:
        if (validObject(v, o))
        {
            m_clipboardObject = m_variants[static_cast<std::size_t>(v)].objects[static_cast<std::size_t>(o)];
            m_clipboardKind = CanTreeSelectionKind::Object;
        }
        break;
    case CanTreePendingOp::PasteObject:
        if (validVariant(v) && (CanTreeSelectionKind::Object == m_clipboardKind))
        {
            m_variants[static_cast<std::size_t>(v)].objects.push_back(m_clipboardObject);
            SelectObject(v,
                         static_cast<int>(m_variants[static_cast<std::size_t>(v)].objects.size()) - 1);
        }
        break;
    case CanTreePendingOp::AddSignal:
        if (validObject(v, o))
        {
            auto &object = m_variants[static_cast<std::size_t>(v)].objects[static_cast<std::size_t>(o)];
            CanTreeSignal signal;
            signal.name = "Signal_" + std::to_string(object.signals.size());
            object.signals.push_back(std::move(signal));
            SelectSignal(v, o, static_cast<int>(object.signals.size()) - 1);
        }
        break;
    case CanTreePendingOp::DeleteSignal:
        if (validObject(v, o))
        {
            auto &signals = m_variants[static_cast<std::size_t>(v)].objects[static_cast<std::size_t>(o)].signals;
            if ((s >= 0) && (s < static_cast<int>(signals.size())))
            {
                signals.erase(signals.begin() + s);
                SelectObject(v, o);
            }
        }
        break;
    case CanTreePendingOp::CopySignal:
        if (validObject(v, o))
        {
            auto &signals = m_variants[static_cast<std::size_t>(v)].objects[static_cast<std::size_t>(o)].signals;
            if ((s >= 0) && (s < static_cast<int>(signals.size())))
            {
                m_clipboardSignal = signals[static_cast<std::size_t>(s)];
                m_clipboardKind = CanTreeSelectionKind::Signal;
            }
        }
        break;
    case CanTreePendingOp::PasteSignal:
        if (validObject(v, o) && (CanTreeSelectionKind::Signal == m_clipboardKind))
        {
            auto &object = m_variants[static_cast<std::size_t>(v)].objects[static_cast<std::size_t>(o)];
            object.signals.push_back(m_clipboardSignal);
            SelectSignal(v, o, static_cast<int>(object.signals.size()) - 1);
        }
        break;
    case CanTreePendingOp::None:
    default:
        break;
    }
}

void CanConfigTreeManager::Render()
{
    if (m_openRequested)
    {
        ImGui::OpenPopup("CAN variant/object/signal editor");
        m_openRequested = false;
    }

    ImGui::SetNextWindowSize(ImVec2(960.0f, 640.0f), ImGuiCond_FirstUseEver);
    bool open{m_open};
    if (ImGui::BeginPopupModal("CAN variant/object/signal editor", &open,
                               ImGuiWindowFlags_NoSavedSettings))
    {
        const float footerHeight{ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y};
        if (ImGui::BeginChild("##cancfg_body", ImVec2(0.0f, -footerHeight)))
        {
            if (ImGui::BeginTable("##cancfg_split", 2,
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
            {
                ImGui::TableSetupColumn("Tree", ImGuiTableColumnFlags_WidthStretch, 0.42f);
                ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthStretch, 0.58f);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                RenderTree();

                ImGui::TableSetColumnIndex(1);
                RenderProperties();

                ImGui::EndTable();
            }
        }
        ImGui::EndChild();

        // Structural mutations are deferred out of the tree iteration to keep it valid.
        ApplyPendingOp();

        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120.0f, 0.0f)))
        {
            ApplyToIni();
            m_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
        {
            m_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!open)
    {
        m_open = false;
    }

    RenderJ1939AdditionalsDialog();
    RenderJ1939MultiPGDialog();
    RenderSearchDialog();
    RenderFilePicker();
}

void CanConfigTreeManager::RenderTree()
{
    if (ImGui::Button("Add variant"))
    {
        RequestOp(CanTreePendingOp::AddVariant, -1, -1, -1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Paste variant") && (CanTreeSelectionKind::Variant == m_clipboardKind))
    {
        RequestOp(CanTreePendingOp::PasteVariant, -1, -1, -1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Search..."))
    {
        OpenSearchDialog();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Right-click a node for add/copy/paste/delete actions.");
    }

    const bool hasVariant{(m_selVariant >= 0) && (m_selVariant < static_cast<int>(m_variants.size()))};
    const bool hasObject{hasVariant && (CanTreeSelectionKind::Object == m_selectionKind ||
                                        CanTreeSelectionKind::Signal == m_selectionKind) &&
                         (m_selObject >= 0)};
    if (ImGui::Button("Import variant"))
    {
        OpenFilePicker(CanTreeFileAction::ImportVariant);
    }
    ImGui::SameLine();
    if (ImGui::Button("Export variant") && hasVariant)
    {
        OpenFilePicker(CanTreeFileAction::ExportVariant);
    }
    ImGui::SameLine();
    if (ImGui::Button("Import object") && hasVariant)
    {
        OpenFilePicker(CanTreeFileAction::ImportObject);
    }
    ImGui::SameLine();
    if (ImGui::Button("Export object") && hasObject)
    {
        OpenFilePicker(CanTreeFileAction::ExportObject);
    }

    const float mappingHeight{ImGui::GetContentRegionAvail().y * 0.32f};
    if (ImGui::BeginChild("##cancfg_tree", ImVec2(0.0f, -mappingHeight)))
    {
        for (std::size_t v = 0; v < m_variants.size(); ++v)
        {
            CanTreeVariant &variant = m_variants[v];
            ImGui::PushID(static_cast<int>(v));
            ImGuiTreeNodeFlags variantFlags{ImGuiTreeNodeFlags_OpenOnArrow |
                                            ImGuiTreeNodeFlags_SpanAvailWidth};
            if ((CanTreeSelectionKind::Variant == m_selectionKind) &&
                (static_cast<int>(v) == m_selVariant))
            {
                variantFlags |= ImGuiTreeNodeFlags_Selected;
            }
            const std::string variantLabel{variant.name.empty() ? ("Variant_" + std::to_string(v))
                                                                 : variant.name};
            const bool variantOpen{ImGui::TreeNodeEx("##variant", variantFlags, "%s",
                                                     variantLabel.c_str())};
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            {
                SelectVariant(static_cast<int>(v));
            }
            if (ImGui::BeginPopupContextItem("##variant_ctx"))
            {
                if (ImGui::MenuItem("Add object"))
                {
                    RequestOp(CanTreePendingOp::AddObject, static_cast<int>(v), -1, -1);
                }
                if (ImGui::MenuItem("Paste object", nullptr, false,
                                    CanTreeSelectionKind::Object == m_clipboardKind))
                {
                    RequestOp(CanTreePendingOp::PasteObject, static_cast<int>(v), -1, -1);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Copy variant"))
                {
                    RequestOp(CanTreePendingOp::CopyVariant, static_cast<int>(v), -1, -1);
                }
                if (ImGui::MenuItem("Delete variant"))
                {
                    RequestOp(CanTreePendingOp::DeleteVariant, static_cast<int>(v), -1, -1);
                }
                ImGui::EndPopup();
            }
            if (variantOpen)
            {
                for (std::size_t o = 0; o < variant.objects.size(); ++o)
                {
                    CanTreeObject &object = variant.objects[o];
                    ImGui::PushID(static_cast<int>(o));
                    ImGuiTreeNodeFlags objectFlags{ImGuiTreeNodeFlags_OpenOnArrow |
                                                   ImGuiTreeNodeFlags_SpanAvailWidth};
                    if ((CanTreeSelectionKind::Object == m_selectionKind) &&
                        (static_cast<int>(v) == m_selVariant) &&
                        (static_cast<int>(o) == m_selObject))
                    {
                        objectFlags |= ImGuiTreeNodeFlags_Selected;
                    }
                    const char *arrow{(CanObjectDirection::Write == object.direction) ||
                                              (CanObjectDirection::WriteVariableId == object.direction)
                                          ? "->"
                                          : "<-"};
                    const std::string objectLabel{std::string{arrow} + " [0x" +
                                                  [&]() {
                                                      char b[16];
                                                      std::snprintf(b, sizeof(b), "%X",
                                                                    object.identifier);
                                                      return std::string{b};
                                                  }() +
                                                  "] " + object.name};
                    const bool objectOpen{ImGui::TreeNodeEx("##object", objectFlags, "%s",
                                                            objectLabel.c_str())};
                    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                    {
                        SelectObject(static_cast<int>(v), static_cast<int>(o));
                    }
                    if (ImGui::BeginPopupContextItem("##object_ctx"))
                    {
                        if (ImGui::MenuItem("Add signal"))
                        {
                            RequestOp(CanTreePendingOp::AddSignal, static_cast<int>(v),
                                      static_cast<int>(o), -1);
                        }
                        if (ImGui::MenuItem("Paste signal", nullptr, false,
                                            CanTreeSelectionKind::Signal == m_clipboardKind))
                        {
                            RequestOp(CanTreePendingOp::PasteSignal, static_cast<int>(v),
                                      static_cast<int>(o), -1);
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Copy object"))
                        {
                            RequestOp(CanTreePendingOp::CopyObject, static_cast<int>(v),
                                      static_cast<int>(o), -1);
                        }
                        if (ImGui::MenuItem("Delete object"))
                        {
                            RequestOp(CanTreePendingOp::DeleteObject, static_cast<int>(v),
                                      static_cast<int>(o), -1);
                        }
                        ImGui::EndPopup();
                    }
                    if (objectOpen)
                    {
                        for (std::size_t s = 0; s < object.signals.size(); ++s)
                        {
                            CanTreeSignal &signal = object.signals[s];
                            ImGui::PushID(static_cast<int>(s));
                            ImGuiTreeNodeFlags signalFlags{ImGuiTreeNodeFlags_Leaf |
                                                           ImGuiTreeNodeFlags_SpanAvailWidth |
                                                           ImGuiTreeNodeFlags_NoTreePushOnOpen};
                            if ((CanTreeSelectionKind::Signal == m_selectionKind) &&
                                (static_cast<int>(v) == m_selVariant) &&
                                (static_cast<int>(o) == m_selObject) &&
                                (static_cast<int>(s) == m_selSignal))
                            {
                                signalFlags |= ImGuiTreeNodeFlags_Selected;
                            }
                            ImGui::TreeNodeEx("##signal", signalFlags, "%s",
                                              signal.name.empty() ? "<signal>" : signal.name.c_str());
                            if (ImGui::IsItemClicked())
                            {
                                SelectSignal(static_cast<int>(v), static_cast<int>(o),
                                             static_cast<int>(s));
                            }
                            if (ImGui::BeginPopupContextItem("##signal_ctx"))
                            {
                                if (ImGui::MenuItem("Copy signal"))
                                {
                                    RequestOp(CanTreePendingOp::CopySignal, static_cast<int>(v),
                                              static_cast<int>(o), static_cast<int>(s));
                                }
                                if (ImGui::MenuItem("Delete signal"))
                                {
                                    RequestOp(CanTreePendingOp::DeleteSignal, static_cast<int>(v),
                                              static_cast<int>(o), static_cast<int>(s));
                                }
                                ImGui::EndPopup();
                            }
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    RenderChannelMapping();
}

void CanConfigTreeManager::RenderProperties()
{
    if (ImGui::BeginChild("##cancfg_props", ImVec2(0.0f, 0.0f)))
    {
        switch (m_selectionKind)
        {
        case CanTreeSelectionKind::Variant:
            if ((m_selVariant >= 0) && (m_selVariant < static_cast<int>(m_variants.size())))
            {
                RenderVariantProperties(m_variants[static_cast<std::size_t>(m_selVariant)]);
            }
            break;
        case CanTreeSelectionKind::Object:
            if ((m_selVariant >= 0) && (m_selVariant < static_cast<int>(m_variants.size())) &&
                (m_selObject >= 0) &&
                (m_selObject < static_cast<int>(m_variants[static_cast<std::size_t>(m_selVariant)]
                                                    .objects.size())))
            {
                RenderObjectProperties(
                    m_variants[static_cast<std::size_t>(m_selVariant)]
                        .objects[static_cast<std::size_t>(m_selObject)]);
            }
            break;
        case CanTreeSelectionKind::Signal:
            if ((m_selVariant >= 0) && (m_selVariant < static_cast<int>(m_variants.size())) &&
                (m_selObject >= 0) &&
                (m_selObject <
                 static_cast<int>(m_variants[static_cast<std::size_t>(m_selVariant)].objects.size())) &&
                (m_selSignal >= 0) &&
                (m_selSignal <
                 static_cast<int>(m_variants[static_cast<std::size_t>(m_selVariant)]
                                      .objects[static_cast<std::size_t>(m_selObject)]
                                      .signals.size())))
            {
                RenderSignalProperties(m_variants[static_cast<std::size_t>(m_selVariant)]
                                           .objects[static_cast<std::size_t>(m_selObject)]
                                           .signals[static_cast<std::size_t>(m_selSignal)]);
            }
            break;
        case CanTreeSelectionKind::None:
        default:
            ImGui::TextDisabled("Select a variant, object or signal.");
            break;
        }
    }
    ImGui::EndChild();
}

void CanConfigTreeManager::RenderVariantProperties(CanTreeVariant &par_Variant)
{
    ImGui::SeparatorText("Variant");
    ImGui::InputText("Name", &par_Variant.name);
    ImGui::InputText("Description", &par_Variant.description);
    ImGui::InputInt("Baud rate [kBit/s]", &par_Variant.baudRate);
    ImGui::InputDouble("Sample point", &par_Variant.samplePoint, 0.0, 0.0, "%.4f");
    ImGui::Checkbox("CAN FD", &par_Variant.canFdEnable);
    if (par_Variant.canFdEnable)
    {
        ImGui::InputInt("Data baud rate [kBit/s]", &par_Variant.dataBaudRate);
        ImGui::InputDouble("Data sample point", &par_Variant.dataSamplePoint, 0.0, 0.0, "%.4f");
    }
    ImGui::Checkbox("Enable J1939", &par_Variant.enableJ1939);

    int controlBb{static_cast<int>(par_Variant.controlBbName)};
    if (EnumCombo("Blackboard naming", &controlBb, CONTROL_BB_LABELS, 5))
    {
        par_Variant.controlBbName = static_cast<CanControlBbName>(controlBb);
    }
}

void CanConfigTreeManager::RenderObjectProperties(CanTreeObject &par_Object)
{
    ImGui::SeparatorText("Object");
    ImGui::InputText("Name", &par_Object.name);
    ImGui::InputText("Description", &par_Object.description);
    HexField("Identifier", &par_Object.identifier);
    ImGui::InputText("Variable id (formula)", &par_Object.variableId);
    ImGui::Checkbox("Extended (29 bit)", &par_Object.extendedId);
    ImGui::SameLine();
    ImGui::Checkbox("Bit rate switch", &par_Object.bitRateSwitch);
    ImGui::SameLine();
    ImGui::Checkbox("FD frame format", &par_Object.fdFrameFormat);

    int byteOrder{static_cast<int>(par_Object.byteOrder)};
    if (EnumCombo("Byte order", &byteOrder, BYTE_ORDER_LABELS, 2))
    {
        par_Object.byteOrder = static_cast<CanByteOrder>(byteOrder);
    }
    if (ImGui::InputInt("Size [byte]", &par_Object.size))
    {
        if (0 > par_Object.size)
        {
            par_Object.size = 0;
        }
    }
    int direction{static_cast<int>(par_Object.direction)};
    if (EnumCombo("Direction", &direction, DIRECTION_LABELS, 4))
    {
        par_Object.direction = static_cast<CanObjectDirection>(direction);
    }
    int type{static_cast<int>(par_Object.type)};
    if (EnumCombo("Type", &type, OBJECT_TYPE_LABELS, 5))
    {
        par_Object.type = static_cast<CanObjectType>(type);
    }

    if (CanObjectType::Multiplexed == par_Object.type)
    {
        ImGui::SeparatorText("Multiplexer");
        ImGui::InputInt("Mux start bit", &par_Object.muxStartBit);
        ImGui::InputInt("Mux bit size", &par_Object.muxBitSize);
        ImGui::InputInt("Mux value", &par_Object.muxValue);
    }

    if (CanObjectType::J1939 == par_Object.type)
    {
        ImGui::SeparatorText("J1939");
        if (ImGui::Button("J1939 additionals..."))
        {
            OpenJ1939AdditionalsDialog(m_selVariant, m_selObject);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(variable DLC / destination address)");
    }
    else if (CanObjectType::J1939MultiPG == par_Object.type)
    {
        ImGui::SeparatorText("J1939 multi-PG");
        if (ImGui::Button("Configure multi-PG..."))
        {
            OpenJ1939MultiPGDialog(m_selVariant, m_selObject);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu C-PG(s) assigned)", par_Object.cPgIds.size());
    }

    ImGui::SeparatorText("Additional variables / equations");
    if ((m_addTextVariant != m_selVariant) || (m_addTextObject != m_selObject))
    {
        SyncAdditionalTextBuffers(par_Object);
    }
    ImGui::TextDisabled("One entry per line; a trailing ';' separates entries (as in Qt).");
    const ImVec2 addSize{ImVec2(0.0f, ImGui::GetTextLineHeight() * 3.0f)};
    bool addChanged{false};
    addChanged |= ImGui::InputTextMultiline("Additional variables", &m_addVarsText, addSize);
    addChanged |= ImGui::InputTextMultiline("Equations before", &m_equBeforeText, addSize);
    addChanged |= ImGui::InputTextMultiline("Equations behind", &m_equBehindText, addSize);
    if (addChanged)
    {
        ParseAdditionalTextBuffers(par_Object);
    }

    ImGui::SeparatorText("Transmit");
    int transmit{static_cast<int>(par_Object.transmitType)};
    if (EnumCombo("Trigger", &transmit, TRANSMIT_LABELS, 2))
    {
        par_Object.transmitType = static_cast<CanObjectTransmit>(transmit);
    }
    if (CanObjectTransmit::Equation == par_Object.transmitType)
    {
        ImGui::InputText("Event equation", &par_Object.transmitEquation);
    }
    else
    {
        ImGui::InputText("Cycles", &par_Object.transmitCycles);
        ImGui::InputText("Delay", &par_Object.transmitDelay);
    }

    ImGui::SeparatorText("Init data");
    char initText[48];
    std::snprintf(initText, sizeof(initText), "0x%llX",
                  static_cast<unsigned long long>(par_Object.initValue));
    if (ImGui::InputText("Init value", initText, sizeof(initText),
                         ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsNoBlank))
    {
        par_Object.initValue = std::strtoull(initText, nullptr, 16);
    }
}

void CanConfigTreeManager::RenderSignalProperties(CanTreeSignal &par_Signal)
{
    ImGui::SeparatorText("Signal");
    ImGui::InputText("Name", &par_Signal.name);
    ImGui::InputText("Description", &par_Signal.description);
    ImGui::InputText("Unit", &par_Signal.unit);

    int bbType{0};
    for (int i = 0; i < static_cast<int>(sizeof(BB_TYPE_LABELS) / sizeof(BB_TYPE_LABELS[0])); ++i)
    {
        if (par_Signal.bbType == BB_TYPE_LABELS[i])
        {
            bbType = i;
            break;
        }
    }
    if (EnumCombo("Blackboard data type", &bbType, BB_TYPE_LABELS,
                  static_cast<int>(sizeof(BB_TYPE_LABELS) / sizeof(BB_TYPE_LABELS[0]))))
    {
        par_Signal.bbType = BB_TYPE_LABELS[bbType];
    }

    ImGui::SeparatorText("Layout");
    ImGui::InputInt("Start bit", &par_Signal.startBit);
    ImGui::InputInt("Bit size", &par_Signal.bitSize);
    int byteOrder{static_cast<int>(par_Signal.byteOrder)};
    if (EnumCombo("Byte order", &byteOrder, BYTE_ORDER_LABELS, 2))
    {
        par_Signal.byteOrder = static_cast<CanByteOrder>(byteOrder);
    }
    int sign{static_cast<int>(par_Signal.sign)};
    if (EnumCombo("Sign", &sign, SIGN_LABELS, 3))
    {
        par_Signal.sign = static_cast<CanSignalSign>(sign);
    }

    ImGui::SeparatorText("Conversion");
    int conversion{static_cast<int>(par_Signal.conversion)};
    if (EnumCombo("Type", &conversion, CONVERSION_LABELS, 4))
    {
        par_Signal.conversion = static_cast<CanSignalConversion>(conversion);
    }
    if ((CanSignalConversion::MultiplyFactorAddOffset == par_Signal.conversion) ||
        (CanSignalConversion::AddOffsetMultiplyFactor == par_Signal.conversion))
    {
        ImGui::InputText("Factor", &par_Signal.factor);
        ImGui::InputText("Offset", &par_Signal.offset);
    }
    else
    {
        ImGui::InputText((CanSignalConversion::Curve == par_Signal.conversion) ? "Curve" : "Equation",
                         &par_Signal.convString);
    }

    ImGui::SeparatorText("Start value");
    ImGui::Checkbox("Enabled", &par_Signal.startValueEnable);
    if (par_Signal.startValueEnable)
    {
        ImGui::InputText("Start value", &par_Signal.startValue);
    }

    ImGui::SeparatorText("Multiplex");
    int signalType{static_cast<int>(par_Signal.signalType)};
    if (EnumCombo("Signal type", &signalType, SIGNAL_TYPE_LABELS, 3))
    {
        par_Signal.signalType = static_cast<CanSignalType>(signalType);
    }
    if (CanSignalType::MultiplexedBy == par_Signal.signalType)
    {
        ImGui::InputText("Multiplexed by", &par_Signal.muxBySignal);
        ImGui::InputInt("Mux value", &par_Signal.muxValue);
    }
    else if (CanSignalType::Multiplexed == par_Signal.signalType)
    {
        ImGui::InputInt("Mux start bit", &par_Signal.muxStartBit);
        ImGui::InputInt("Mux bit size", &par_Signal.muxBitSize);
        ImGui::InputInt("Mux value", &par_Signal.muxValue);
    }
}

void CanConfigTreeManager::SyncAdditionalTextBuffers(const CanTreeObject &par_Object)
{
    const auto join = [](const std::vector<std::string> &par_Items) {
        std::string text;
        for (const auto &item : par_Items)
        {
            text += item;
            text += ";\n";
        }
        return text;
    };
    m_addVarsText = join(par_Object.additionalVariables);
    m_equBeforeText = join(par_Object.equationsBefore);
    m_equBehindText = join(par_Object.equationsBehind);
    m_addTextVariant = m_selVariant;
    m_addTextObject = m_selObject;
}

void CanConfigTreeManager::ParseAdditionalTextBuffers(CanTreeObject &par_Object) const
{
    // Mirrors the Qt PlainTextToStringList: newlines -> spaces, split on ';', trim, drop empties.
    const auto parse = [](const std::string &par_Text) {
        std::string text{par_Text};
        for (char &ch : text)
        {
            if (('\n' == ch) || ('\r' == ch))
            {
                ch = ' ';
            }
        }
        std::vector<std::string> items;
        std::size_t start{0};
        while (start <= text.size())
        {
            const std::size_t semi{text.find(';', start)};
            const std::size_t end{(std::string::npos == semi) ? text.size() : semi};
            std::string item{text.substr(start, end - start)};
            const std::size_t first{item.find_first_not_of(" \t")};
            if (std::string::npos != first)
            {
                const std::size_t last{item.find_last_not_of(" \t")};
                items.push_back(item.substr(first, last - first + 1));
            }
            if (std::string::npos == semi)
            {
                break;
            }
            start = semi + 1;
        }
        return items;
    };
    par_Object.additionalVariables = parse(m_addVarsText);
    par_Object.equationsBefore = parse(m_equBeforeText);
    par_Object.equationsBehind = parse(m_equBehindText);
}

void CanConfigTreeManager::OpenJ1939AdditionalsDialog(int par_Variant, int par_Object)
{
    if ((par_Variant < 0) || (par_Variant >= static_cast<int>(m_variants.size())))
    {
        return;
    }
    CanTreeVariant &variant = m_variants[static_cast<std::size_t>(par_Variant)];
    if ((par_Object < 0) || (par_Object >= static_cast<int>(variant.objects.size())))
    {
        return;
    }
    m_j1939AddVariant = par_Variant;
    m_j1939AddObject = par_Object;
    m_j1939AddBackup = variant.objects[static_cast<std::size_t>(par_Object)];
    m_j1939AddOpen = true;
    m_j1939AddOpenRequested = true;
}

void CanConfigTreeManager::RenderJ1939AdditionalsDialog()
{
    if (m_j1939AddOpenRequested)
    {
        ImGui::OpenPopup("J1939 additional configuration");
        m_j1939AddOpenRequested = false;
    }
    if ((m_j1939AddVariant < 0) || (m_j1939AddVariant >= static_cast<int>(m_variants.size())))
    {
        return;
    }
    CanTreeVariant &variant = m_variants[static_cast<std::size_t>(m_j1939AddVariant)];
    if ((m_j1939AddObject < 0) || (m_j1939AddObject >= static_cast<int>(variant.objects.size())))
    {
        return;
    }
    CanTreeObject &object = variant.objects[static_cast<std::size_t>(m_j1939AddObject)];

    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_FirstUseEver);
    bool open{m_j1939AddOpen};
    if (ImGui::BeginPopupModal("J1939 additional configuration", &open,
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Checkbox("Variable DLC", &object.variableDlc);
        ImGui::SameLine();
        if (object.variableDlc)
        {
            ImGui::InputText("##dlcvar", &object.variableDlcName);
        }
        else
        {
            ImGui::BeginDisabled();
            std::string disabled{};
            ImGui::InputText("##dlcvar", &disabled, ImGuiInputTextFlags_ReadOnly);
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        ImGui::Text("Init value %d", object.size);

        ImGui::SeparatorText("Destination Address");
        int dst{static_cast<int>(object.destAddrType)};
        if (ImGui::RadioButton("Fixed", CanObjectDestAddr::Fixed == object.destAddrType))
        {
            dst = static_cast<int>(CanObjectDestAddr::Fixed);
        }
        object.destAddrType = static_cast<CanObjectDestAddr>(dst);
        ImGui::SameLine();
        if (CanObjectDestAddr::Fixed == object.destAddrType)
        {
            HexField("##fixeddst", &object.fixedDestAddr);
        }
        else
        {
            ImGui::BeginDisabled();
            uint32_t shadow{object.fixedDestAddr};
            HexField("##fixeddst", &shadow);
            ImGui::EndDisabled();
        }

        if (ImGui::RadioButton("Variable", CanObjectDestAddr::Variable == object.destAddrType))
        {
            object.destAddrType = CanObjectDestAddr::Variable;
        }
        ImGui::SameLine();
        if (CanObjectDestAddr::Variable == object.destAddrType)
        {
            ImGui::InputText("##vardst", &object.variableDestAddrName);
            ImGui::SameLine();
            ImGui::Text("Init value");
            ImGui::SameLine();
            HexField("##vardstinit", &object.variableDestAddrInit);
        }
        else
        {
            ImGui::BeginDisabled();
            std::string shadowName{object.variableDestAddrName};
            ImGui::InputText("##vardst", &shadowName, ImGuiInputTextFlags_ReadOnly);
            ImGui::SameLine();
            ImGui::Text("Init value");
            ImGui::SameLine();
            uint32_t shadowInit{object.variableDestAddrInit};
            HexField("##vardstinit", &shadowInit);
            ImGui::EndDisabled();
        }

        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120.0f, 0.0f)))
        {
            // On OK the Qt dialog clears the field of the non-selected destination-address kind.
            if (CanObjectDestAddr::Fixed == object.destAddrType)
            {
                object.variableDestAddrName.clear();
                object.variableDestAddrInit = 0;
            }
            else
            {
                object.fixedDestAddr = 0;
            }
            m_j1939AddOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
        {
            object = m_j1939AddBackup; // Discard edits.
            m_j1939AddOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!open)
    {
        object = m_j1939AddBackup; // Window close button == Cancel.
        m_j1939AddOpen = false;
    }
}

void CanConfigTreeManager::OpenJ1939MultiPGDialog(int par_Variant, int par_Object)
{
    if ((par_Variant < 0) || (par_Variant >= static_cast<int>(m_variants.size())))
    {
        return;
    }
    CanTreeVariant &variant = m_variants[static_cast<std::size_t>(par_Variant)];
    if ((par_Object < 0) || (par_Object >= static_cast<int>(variant.objects.size())))
    {
        return;
    }
    m_multiPgVariant = par_Variant;
    m_multiPgObject = par_Object;
    m_multiPgChecked = variant.objects[static_cast<std::size_t>(par_Object)].cPgIds;
    m_multiPgOpen = true;
    m_multiPgOpenRequested = true;
}

void CanConfigTreeManager::RenderJ1939MultiPGDialog()
{
    if (m_multiPgOpenRequested)
    {
        ImGui::OpenPopup("Configure J1939 multi PG object");
        m_multiPgOpenRequested = false;
    }
    if ((m_multiPgVariant < 0) || (m_multiPgVariant >= static_cast<int>(m_variants.size())))
    {
        return;
    }
    CanTreeVariant &variant = m_variants[static_cast<std::size_t>(m_multiPgVariant)];
    if ((m_multiPgObject < 0) || (m_multiPgObject >= static_cast<int>(variant.objects.size())))
    {
        return;
    }
    CanTreeObject &object = variant.objects[static_cast<std::size_t>(m_multiPgObject)];
    const CanObjectDirection dir{object.direction};

    ImGui::SetNextWindowSize(ImVec2(460.0f, 440.0f), ImGuiCond_FirstUseEver);
    bool open{m_multiPgOpen};
    if (ImGui::BeginPopupModal("Configure J1939 multi PG object", &open,
                               ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::TextDisabled("Check the C-PG objects (same direction) that belong to this multi-PG.");
        const float footerHeight{ImGui::GetFrameHeightWithSpacing() +
                                 ImGui::GetStyle().ItemSpacing.y};
        if (ImGui::BeginChild("##multipg_list", ImVec2(0.0f, -footerHeight)))
        {
            for (std::size_t o = 0; o < variant.objects.size(); ++o)
            {
                const CanTreeObject &candidate = variant.objects[o];
                if (CanObjectType::J1939CPG != candidate.type)
                {
                    continue;
                }
                if (candidate.direction != dir)
                {
                    continue;
                }
                const int id{static_cast<int>(candidate.identifier)};
                const auto it{std::find(m_multiPgChecked.begin(), m_multiPgChecked.end(), id)};
                bool checked{m_multiPgChecked.end() != it};
                char label[320];
                std::snprintf(label, sizeof(label), "[0x%X] %s", candidate.identifier,
                              candidate.name.c_str());
                ImGui::PushID(static_cast<int>(o));
                if (ImGui::Checkbox(label, &checked))
                {
                    if (checked)
                    {
                        if (m_multiPgChecked.end() == it)
                        {
                            m_multiPgChecked.push_back(id);
                        }
                    }
                    else if (m_multiPgChecked.end() != it)
                    {
                        m_multiPgChecked.erase(it);
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120.0f, 0.0f)))
        {
            object.cPgIds = m_multiPgChecked;
            m_multiPgOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
        {
            m_multiPgOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!open)
    {
        m_multiPgOpen = false;
    }
}

void CanConfigTreeManager::OpenSearchDialog()
{
    m_searchActive = false;
    m_searchVariant = -1;
    m_searchObject = -1;
    m_searchSignal = -1;
    m_searchOpen = true;
    m_searchOpenRequested = true;
}

bool CanConfigTreeManager::SearchNextSignal()
{
    if (m_searchFilter.empty())
    {
        return false;
    }
    std::regex regex;
    try
    {
        regex = std::regex{m_searchFilter, std::regex::ECMAScript};
    }
    catch (const std::regex_error &)
    {
        return false; // Invalid pattern: behave like "no match".
    }

    // Advance linearly (variant, object, signal) after the last matched cursor position.
    for (std::size_t v = 0; v < m_variants.size(); ++v)
    {
        const CanTreeVariant &variant = m_variants[v];
        for (std::size_t o = 0; o < variant.objects.size(); ++o)
        {
            const CanTreeObject &object = variant.objects[o];
            for (std::size_t s = 0; s < object.signals.size(); ++s)
            {
                const bool afterCursor{
                    (static_cast<int>(v) > m_searchVariant) ||
                    ((static_cast<int>(v) == m_searchVariant) &&
                     (static_cast<int>(o) > m_searchObject)) ||
                    ((static_cast<int>(v) == m_searchVariant) &&
                     (static_cast<int>(o) == m_searchObject) &&
                     (static_cast<int>(s) > m_searchSignal))};
                if (!afterCursor)
                {
                    continue;
                }
                if (std::regex_search(object.signals[s].name, regex))
                {
                    m_searchVariant = static_cast<int>(v);
                    m_searchObject = static_cast<int>(o);
                    m_searchSignal = static_cast<int>(s);
                    SelectSignal(static_cast<int>(v), static_cast<int>(o), static_cast<int>(s));
                    return true;
                }
            }
        }
    }
    return false;
}

void CanConfigTreeManager::RenderSearchDialog()
{
    if (m_searchOpenRequested)
    {
        ImGui::OpenPopup("Search signal");
        m_searchOpenRequested = false;
    }
    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_FirstUseEver);
    bool open{m_searchOpen};
    if (ImGui::BeginPopupModal("Search signal", &open,
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Filter (regular expression):");
        ImGui::SetNextItemWidth(400.0f);
        const bool entered{ImGui::InputText("##searchfilter", &m_searchFilter,
                                            ImGuiInputTextFlags_EnterReturnsTrue)};
        ImGui::SameLine();
        const char *buttonLabel{m_searchActive ? "Next" : "Search"};
        if (ImGui::Button(buttonLabel) || entered)
        {
            if (SearchNextSignal())
            {
                m_searchActive = true;
            }
            else
            {
                // No further match: reset the cursor so the next click restarts at the top.
                m_searchActive = false;
                m_searchVariant = -1;
                m_searchObject = -1;
                m_searchSignal = -1;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Close"))
        {
            m_searchOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!open)
    {
        m_searchOpen = false;
    }
}

void CanConfigTreeManager::RenderChannelMapping()
{
    if (!ImGui::CollapsingHeader("Channel <-> variant mapping", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }

    if (ImGui::InputInt("Channels", &m_numberOfChannels))
    {
        if (1 > m_numberOfChannels)
        {
            m_numberOfChannels = 1;
        }
        if (MAX_CAN_CHANNELS < m_numberOfChannels)
        {
            m_numberOfChannels = MAX_CAN_CHANNELS;
        }
    }
    ImGui::Checkbox("No units for blackboard", &m_doNotUseUnits);
    ImGui::SameLine();
    ImGui::Checkbox("Restart CAN immediately", &m_restartImmediately);
    ImGui::Checkbox("Enable gateway device driver", &m_enableGatewayDeviceDriver);

    if (ImGui::BeginChild("##cancfg_mapping", ImVec2(0.0f, 0.0f)))
    {
        const int columns{m_numberOfChannels + 1};
        if ((columns >= 2) &&
            ImGui::BeginTable("##mapping_table", columns,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollX |
                                  ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("Variant", ImGuiTableColumnFlags_WidthStretch);
            for (int c = 0; c < m_numberOfChannels; ++c)
            {
                char header[16];
                std::snprintf(header, sizeof(header), "Ch %d", c + 1);
                ImGui::TableSetupColumn(header, ImGuiTableColumnFlags_WidthFixed);
            }
            ImGui::TableHeadersRow();

            for (std::size_t v = 0; v < m_variants.size(); ++v)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(v));
                const std::string &name{m_variants[v].name};
                ImGui::TextUnformatted(name.empty() ? ("Variant_" + std::to_string(v)).c_str()
                                                    : name.c_str());
                for (int c = 0; c < m_numberOfChannels; ++c)
                {
                    ImGui::TableSetColumnIndex(c + 1);
                    ImGui::PushID(c);
                    bool connected{m_variants[v].connectedChannels.at(static_cast<std::size_t>(c))};
                    if (ImGui::Checkbox("##conn", &connected))
                    {
                        m_variants[v].connectedChannels.at(static_cast<std::size_t>(c)) = connected;
                    }
                    ImGui::PopID();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}

void CanConfigTreeManager::OpenFilePicker(CanTreeFileAction par_Action)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    m_filePickerDir = fs::current_path(ec).string();
    m_filePickerAction = par_Action;
    m_filePickerName = std::array<char, 512>{};
    switch (par_Action)
    {
    case CanTreeFileAction::ExportVariant:
        m_filePickerSaveMode = true;
        m_filePickerTitle = "Export CAN variant";
        break;
    case CanTreeFileAction::ExportObject:
        m_filePickerSaveMode = true;
        m_filePickerTitle = "Export CAN object";
        break;
    case CanTreeFileAction::ImportVariant:
        m_filePickerSaveMode = false;
        m_filePickerTitle = "Import CAN variant";
        break;
    case CanTreeFileAction::ImportObject:
        m_filePickerSaveMode = false;
        m_filePickerTitle = "Import CAN object";
        break;
    default:
        return;
    }
    m_filePickerOpen = true;
    m_filePickerOpenRequested = true;
}

void CanConfigTreeManager::ExecuteFileAction(const std::string &par_Path)
{
    switch (m_filePickerAction)
    {
    case CanTreeFileAction::ImportVariant:
        ImportVariantFromFile(par_Path);
        break;
    case CanTreeFileAction::ExportVariant:
        ExportVariantToFile(par_Path);
        break;
    case CanTreeFileAction::ImportObject:
        ImportObjectFromFile(par_Path);
        break;
    case CanTreeFileAction::ExportObject:
        ExportObjectToFile(par_Path);
        break;
    default:
        break;
    }
}

void CanConfigTreeManager::ImportVariantFromFile(const std::string &par_Path)
{
    const int fd{IniFileDataBaseOpen(par_Path.c_str())};
    if (0 >= fd)
    {
        return;
    }
    if (2 == IniFileDataBaseReadInt("CAN/Global", "copy_buffer_type", 0, fd))
    {
        bool exists{false};
        CanTreeVariant variant{ReadVariant(9999, fd, exists)};
        if (exists)
        {
            m_variants.push_back(std::move(variant));
            SelectVariant(static_cast<int>(m_variants.size()) - 1);
        }
    }
    (void)IniFileDataBaseClose(fd);
}

void CanConfigTreeManager::ExportVariantToFile(const std::string &par_Path)
{
    if ((m_selVariant < 0) || (m_selVariant >= static_cast<int>(m_variants.size())))
    {
        return;
    }
    const int fd{IniFileDataBaseCreateAndOpenNewIniFile(par_Path.c_str())};
    if (0 >= fd)
    {
        return;
    }
    (void)IniFileDataBaseWriteString("CAN/Global", "copy_buffer_type", "2", fd);
    WriteVariant(m_variants[static_cast<std::size_t>(m_selVariant)], 9999, fd);
    (void)IniFileDataBaseSave(fd, par_Path.c_str(), 2);
}

void CanConfigTreeManager::ImportObjectFromFile(const std::string &par_Path)
{
    if ((m_selVariant < 0) || (m_selVariant >= static_cast<int>(m_variants.size())))
    {
        return;
    }
    const int fd{IniFileDataBaseOpen(par_Path.c_str())};
    if (0 >= fd)
    {
        return;
    }
    if (3 == IniFileDataBaseReadInt("CAN/Global", "copy_buffer_type", 0, fd))
    {
        bool exists{false};
        CanTreeObject object{ReadObject(9999, 9999, fd, exists)};
        if (exists)
        {
            auto &objects = m_variants[static_cast<std::size_t>(m_selVariant)].objects;
            objects.push_back(std::move(object));
            SelectObject(m_selVariant, static_cast<int>(objects.size()) - 1);
        }
    }
    (void)IniFileDataBaseClose(fd);
}

void CanConfigTreeManager::ExportObjectToFile(const std::string &par_Path)
{
    if ((m_selVariant < 0) || (m_selVariant >= static_cast<int>(m_variants.size())) ||
        (m_selObject < 0) ||
        (m_selObject >=
         static_cast<int>(m_variants[static_cast<std::size_t>(m_selVariant)].objects.size())))
    {
        return;
    }
    const int fd{IniFileDataBaseCreateAndOpenNewIniFile(par_Path.c_str())};
    if (0 >= fd)
    {
        return;
    }
    (void)IniFileDataBaseWriteString("CAN/Global", "copy_buffer_type", "3", fd);
    WriteObject(m_variants[static_cast<std::size_t>(m_selVariant)]
                    .objects[static_cast<std::size_t>(m_selObject)],
                9999, 9999, fd);
    (void)IniFileDataBaseSave(fd, par_Path.c_str(), 2);
}

void CanConfigTreeManager::RenderFilePicker()
{
    namespace fs = std::filesystem;
    if (m_filePickerOpenRequested)
    {
        ImGui::OpenPopup("##cancfg_filepicker");
        m_filePickerOpenRequested = false;
    }

    ImGui::SetNextWindowSize(ImVec2(600.0f, 440.0f), ImGuiCond_FirstUseEver);
    bool open{m_filePickerOpen};
    if (ImGui::BeginPopupModal(("##cancfg_filepicker"), &open, ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::TextUnformatted(m_filePickerTitle.c_str());
        ImGui::TextDisabled("%s", m_filePickerDir.c_str());
        ImGui::Separator();

        if (ImGui::BeginChild("##cancfg_files", ImVec2(0.0f, -70.0f)))
        {
            std::error_code ec;
            const fs::path dir{m_filePickerDir};
            if (dir.has_parent_path() && ImGui::Selectable(".. (parent)"))
            {
                m_filePickerDir = dir.parent_path().string();
            }
            for (fs::directory_iterator it{dir, ec}, end; it != end; it.increment(ec))
            {
                if (ec)
                {
                    break;
                }
                const fs::path &path{it->path()};
                std::error_code dirEc;
                if (fs::is_directory(path, dirEc))
                {
                    const std::string label{"[DIR] " + path.filename().string()};
                    if (ImGui::Selectable(label.c_str()))
                    {
                        m_filePickerDir = path.string();
                    }
                }
                else if (".can" == path.extension().string())
                {
                    if (ImGui::Selectable(path.filename().string().c_str()))
                    {
                        CopyToArray(m_filePickerName, path.filename().string());
                    }
                }
            }
        }
        ImGui::EndChild();

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##cancfg_filename", m_filePickerName.data(), m_filePickerName.size());

        if (ImGui::Button(m_filePickerSaveMode ? "Save" : "Open", ImVec2(100.0f, 0.0f)))
        {
            std::string name{m_filePickerName.data()};
            if (!name.empty())
            {
                if (m_filePickerSaveMode && (fs::path{name}.extension().string() != ".can"))
                {
                    name += ".can";
                }
                const fs::path full{fs::path{m_filePickerDir} / name};
                ExecuteFileAction(full.string());
                m_filePickerOpen = false;
                m_filePickerAction = CanTreeFileAction::None;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
        {
            m_filePickerOpen = false;
            m_filePickerAction = CanTreeFileAction::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!open)
    {
        m_filePickerOpen = false;
        m_filePickerAction = CanTreeFileAction::None;
    }
}

} // namespace XilEnvImGui
