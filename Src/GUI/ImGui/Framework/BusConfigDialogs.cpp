#include "Framework/BusConfigDialogs.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "imgui.h"

#include "Framework/DialogManager.h"

extern "C" {
#include "IniDataBase.h"
#include "ImportDbc.h"
#include "Scheduler.h"
#include "Message.h"

int LoadConfig_CCP(int ConnectionNo, const char *fname);
int SaveConfig_CCP(int ConnectionNo, char *fname);
int LoadConfig_XCP(int ConnectionNo, char *fname);
int SaveConfig_XCP(int ConnectionNo, char *fname);
}

namespace XilEnvImGui
{

namespace
{

constexpr int MAX_CAN_CHANNELS_LOCAL{16};
constexpr int BUS_CFG_USER{0x40000};
constexpr int BUS_CFG_MODE_MASK{0xF0000};

const char *const CCP_VERSION_LABELS[]{"2.0", "2.1", "2.1 ESV1", "2.1 S_STATUS"};
const char *const BYTE_ORDER_LABELS[]{"lsb_first", "msb_first"};
const char *const DATA_TYPE_LABELS[]{"BYTE", "UBYTE", "WORD",  "UWORD",
                                     "DWORD", "UDWORD", "FLOAT", "DOUBLE"};
const char *const TIMESTAMP_LABELS[]{"No timestamp", "1 byte", "2 byte", "4 byte"};

/**
 * @brief Bounded copy of a std::string into a fixed-size char array.
 */
template <std::size_t N>
void CopyToArray(std::array<char, N> &par_Dst, const std::string &par_Src)
{
    std::snprintf(par_Dst.data(), N, "%s", par_Src.c_str());
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
 * @brief Formats an unsigned value into a "0x.." hex char array.
 */
template <std::size_t N>
void SetHexField(std::array<char, N> &par_Dst, unsigned int par_Value)
{
    std::snprintf(par_Dst.data(), N, "0x%X", par_Value);
}

/**
 * @brief Parses a (possibly "0x"-prefixed) hex text field into an unsigned value.
 */
unsigned int ParseHexField(const char *par_Text)
{
    if (nullptr == par_Text)
    {
        return 0u;
    }
    return static_cast<unsigned int>(std::strtoul(par_Text, nullptr, 16));
}

/**
 * @brief Trims a std::string of leading/trailing ASCII whitespace.
 */
std::string Trim(const std::string &par_Text)
{
    const std::size_t begin{par_Text.find_first_not_of(" \t\r\n")};
    if (std::string::npos == begin)
    {
        return std::string{};
    }
    const std::size_t end{par_Text.find_last_not_of(" \t\r\n")};
    return par_Text.substr(begin, (end - begin) + 1u);
}

/**
 * @brief Splits a comma-separated label line into fields.
 */
std::vector<std::string> SplitLine(const std::string &par_Line)
{
    std::vector<std::string> fields;
    std::string current;
    for (const char ch : par_Line)
    {
        if (',' == ch)
        {
            fields.push_back(current);
            current.clear();
        }
        else
        {
            current.push_back(ch);
        }
    }
    fields.push_back(current);
    return fields;
}

/**
 * @brief Serialises the shared label editor into a "type,name,addr[,unit][,conv][,min,max]" line.
 */
std::string BuildLabelLine(const VarLabelEditorState &par_Editor)
{
    std::string line{DATA_TYPE_LABELS[par_Editor.dataTypeIndex]};
    line.append(",");
    line.append(Trim(par_Editor.name.data()));
    line.append(",");
    line.append(Trim(par_Editor.address.data()));
    if (par_Editor.useUnit)
    {
        line.append(",");
        line.append(Trim(par_Editor.unit.data()));
    }
    else if (par_Editor.useConversion || par_Editor.useMinMax)
    {
        line.append(",");
    }
    if (par_Editor.useConversion)
    {
        line.append(",");
        line.append(Trim(par_Editor.conversion.data()));
    }
    else if (par_Editor.useMinMax)
    {
        line.append(",");
    }
    if (par_Editor.useMinMax)
    {
        line.append(",");
        line.append(Trim(par_Editor.minValue.data()));
        line.append(",");
        line.append(Trim(par_Editor.maxValue.data()));
    }
    return line;
}

/**
 * @brief Loads a selected label line back into the shared editor buffers.
 */
void LoadLabelLine(const std::string &par_Line, VarLabelEditorState &par_Editor)
{
    const std::vector<std::string> fields{SplitLine(par_Line)};
    if (fields.empty())
    {
        return;
    }
    par_Editor.dataTypeIndex = 0;
    for (int i = 0; i < static_cast<int>(std::size(DATA_TYPE_LABELS)); ++i)
    {
        if (0 == fields.at(0).compare(DATA_TYPE_LABELS[i]))
        {
            par_Editor.dataTypeIndex = i;
            break;
        }
    }
    CopyToArray(par_Editor.name, (fields.size() > 1u) ? fields.at(1) : std::string{});
    CopyToArray(par_Editor.address, (fields.size() > 2u) ? fields.at(2) : std::string{});
    par_Editor.useUnit = (fields.size() > 3u);
    CopyToArray(par_Editor.unit, (fields.size() > 3u) ? fields.at(3) : std::string{});
    par_Editor.useConversion = (fields.size() > 4u);
    CopyToArray(par_Editor.conversion, (fields.size() > 4u) ? fields.at(4) : std::string{});
    par_Editor.useMinMax = (fields.size() > 6u);
    CopyToArray(par_Editor.minValue, (fields.size() > 5u) ? fields.at(5) : std::string{});
    CopyToArray(par_Editor.maxValue, (fields.size() > 6u) ? fields.at(6) : std::string{});
}

/**
 * @brief Reads a numbered string list ("v0","v1",..) from an ini section until the first gap.
 */
std::vector<std::string> ReadNumberedList(const char *par_Section, const char *par_Prefix, int par_Fd)
{
    std::vector<std::string> list;
    for (int index = 0;; ++index)
    {
        char entry[64];
        std::snprintf(entry, sizeof(entry), "%s%d", par_Prefix, index);
        const std::string line{ReadIniString(par_Section, entry, "", par_Fd)};
        if (line.empty())
        {
            break;
        }
        list.push_back(line);
    }
    return list;
}

/**
 * @brief Writes a numbered string list and deletes stale higher-index entries.
 */
void WriteNumberedList(const char *par_Section, const char *par_Prefix,
                       const std::vector<std::string> &par_List, int par_OldCount, int par_Fd)
{
    int index = 0;
    for (; index < static_cast<int>(par_List.size()); ++index)
    {
        char entry[64];
        std::snprintf(entry, sizeof(entry), "%s%d", par_Prefix, index);
        (void)IniFileDataBaseWriteString(par_Section, entry, par_List.at(index).c_str(), par_Fd);
    }
    for (; index < par_OldCount; ++index)
    {
        char entry[64];
        std::snprintf(entry, sizeof(entry), "%s%d", par_Prefix, index);
        (void)IniFileDataBaseWriteString(par_Section, entry, nullptr, par_Fd);
    }
}

/**
 * @brief Renders a listbox of strings with single selection; returns the clicked row or -1.
 */
int DrawStringListBox(const char *par_Id, const std::vector<std::string> &par_List, int par_Selected,
                      const ImVec2 &par_Size)
{
    int clicked{-1};
    if (ImGui::BeginListBox(par_Id, par_Size))
    {
        for (int i = 0; i < static_cast<int>(par_List.size()); ++i)
        {
            ImGui::PushID(i);
            const bool isSelected{i == par_Selected};
            if (ImGui::Selectable(par_List.at(i).c_str(), isSelected))
            {
                clicked = i;
            }
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }
    return clicked;
}

} // namespace

BusConfigDialogManager &BusConfigDialogManager::GetInstance()
{
    static BusConfigDialogManager s_instance;
    return s_instance;
}

// -------------------------------------------------------------------------
// Snapshots (ini -> edit state)
// -------------------------------------------------------------------------

void BusConfigDialogManager::SnapshotCcp()
{
    const int fd{GetMainFileDescriptor()};
    for (int t = 0; t < BUS_CONNECTION_COUNT; ++t)
    {
        CcpTargetState &state = m_ccp.at(static_cast<std::size_t>(t));
        state = CcpTargetState{};

        char section[128];
        std::snprintf(section, sizeof(section), "CCP Configuration for Target %d", t);

        state.debug = (0 != IniFileDataBaseReadYesNo(section, "Debug", 0, fd));
        const std::string version{ReadIniString(section, "Protocol Version", "2.1", fd)};
        state.protocolVersionIndex = 1;
        for (int i = 0; i < static_cast<int>(std::size(CCP_VERSION_LABELS)); ++i)
        {
            if (0 == version.compare(CCP_VERSION_LABELS[i]))
            {
                state.protocolVersionIndex = i;
                break;
            }
        }
        const std::string byteOrder{ReadIniString(section, "ByteOrder", "lsb_first", fd)};
        state.byteOrderIndex = (0 == byteOrder.compare("msb_first")) ? 1 : 0;
        state.channel = IniFileDataBaseReadInt(section, "Channel", 0, fd);
        state.timeout = IniFileDataBaseReadInt(section, "Timeout", 1000, fd);
        CopyToArray(state.seedKeyDll, ReadIniString(section, "SeedKeyDll", "", fd));
        state.seedAndKey = (0 != IniFileDataBaseReadInt(section, "SeedKeyFlag", 0, fd));
        CopyToArray(state.seedKeyDllCal, ReadIniString(section, "SeedKeyDllCal", "", fd));
        SetHexField(state.croId, IniFileDataBaseReadUInt(section, "CRO_ID", 0, fd));
        SetHexField(state.crmId, IniFileDataBaseReadUInt(section, "CRM_ID", 0, fd));
        SetHexField(state.dtoId, IniFileDataBaseReadUInt(section, "DTO_ID", 0, fd));
        state.extIds = (0 != IniFileDataBaseReadYesNo(section, "ExtIds", 0, fd));
        state.prescaler = IniFileDataBaseReadInt(section, "Prescaler", 1, fd);
        SetHexField(state.stationAddress, IniFileDataBaseReadUInt(section, "StationAddress", 0x39, fd));
        state.eventChannel = IniFileDataBaseReadInt(section, "EventChannel", 0, fd);
        state.onlyBytePointerInDaq = IniFileDataBaseReadYesNo(section, "OnlyBytePointerInDAQ", 0, fd);
        state.calibrationEnable = (0 != IniFileDataBaseReadYesNo(section, "CalibrationEnable", 0, fd));
        state.moveRomToRam = (0 != IniFileDataBaseReadYesNo(section, "MoveROM2RAM", 0, fd));
        state.setCalibrationPage = (0 != IniFileDataBaseReadYesNo(section, "SelCalPage", 0, fd));
        SetHexField(state.calibRomAddr, IniFileDataBaseReadUInt(section, "CalibROMStartAddr", 0, fd));
        SetHexField(state.calibRamAddr, IniFileDataBaseReadUInt(section, "CalibRAMStartAddr", 0, fd));
        SetHexField(state.calibRomRamSize, IniFileDataBaseReadUInt(section, "CalibROMRAMSize", 0, fd));
        state.readParameterAfterCalib =
            (0 != IniFileDataBaseReadInt(section, "ReadParameterAfterCalib", 0, fd));
        state.measurements = ReadNumberedList(section, "v", fd);
        state.calibrations = ReadNumberedList(section, "p", fd);
        state.measurementCountOld = static_cast<int>(state.measurements.size());
        state.calibrationCountOld = static_cast<int>(state.calibrations.size());
        state.editor.useUnit = (0 != IniFileDataBaseReadYesNo(section, "UseUnit", 0, fd));
        state.editor.useConversion = (0 != IniFileDataBaseReadYesNo(section, "UseConversion", 0, fd));
        state.editor.useMinMax = (0 != IniFileDataBaseReadYesNo(section, "UseMinMax", 0, fd));
    }
}

void BusConfigDialogManager::SnapshotXcp()
{
    const int fd{GetMainFileDescriptor()};
    for (int t = 0; t < BUS_CONNECTION_COUNT; ++t)
    {
        XcpTargetState &state = m_xcp.at(static_cast<std::size_t>(t));
        state = XcpTargetState{};

        char section[128];
        std::snprintf(section, sizeof(section), "XCP Configuration for Target %d", t);

        state.debug = (0 != IniFileDataBaseReadYesNo(section, "DebugMessages", 0, fd));
        const std::string byteOrder{ReadIniString(section, "ByteOrder", "lsb_first", fd)};
        state.byteOrderIndex = (0 == byteOrder.compare("msb_first")) ? 1 : 0;
        state.channel = IniFileDataBaseReadInt(section, "Channel", 0, fd);
        state.timeout = IniFileDataBaseReadInt(section, "Timeout", 1000, fd);
        CopyToArray(state.seedKeyDll, ReadIniString(section, "SeedKeyDll", "", fd));
        SetHexField(state.croId, IniFileDataBaseReadUInt(section, "CRO_ID", 0, fd));
        SetHexField(state.crmId, IniFileDataBaseReadUInt(section, "CRM_ID", 0, fd));
        SetHexField(state.dtoId, IniFileDataBaseReadUInt(section, "DTO_ID", 0, fd));
        state.extIds = (0 != IniFileDataBaseReadInt(section, "ExtIds", 0, fd));
        state.prescaler = IniFileDataBaseReadInt(section, "Prescaler", 1, fd);
        state.eventChannel = IniFileDataBaseReadInt(section, "EventChannel", 0, fd);
        state.onlyBytePointerInDaq = IniFileDataBaseReadYesNo(section, "OnlyBytePointerInDAQ", 0, fd);
        state.calibrationEnable = (0 != IniFileDataBaseReadYesNo(section, "CalibrationEnable", 1, fd));
        state.moveRomToRam = (0 != IniFileDataBaseReadYesNo(section, "MoveROM2RAM", 1, fd));
        state.setCalibrationPage = (0 != IniFileDataBaseReadYesNo(section, "SelCalPage", 1, fd));
        SetHexField(state.calibRomSegment, IniFileDataBaseReadUInt(section, "CalibROMSegment", 0, fd));
        SetHexField(state.calibRomPage, IniFileDataBaseReadUInt(section, "CalibROMPageNo", 0, fd));
        SetHexField(state.calibRamSegment, IniFileDataBaseReadUInt(section, "CalibRAMSegment", 0, fd));
        SetHexField(state.calibRamPage, IniFileDataBaseReadUInt(section, "CalibRAMPageNo", 0, fd));
        state.readParameterAfterCalib =
            (0 != IniFileDataBaseReadInt(section, "ReadParameterAfterCalib", 0, fd));
        state.measurements = ReadNumberedList(section, "v", fd);
        state.calibrations = ReadNumberedList(section, "p", fd);
        state.measurementCountOld = static_cast<int>(state.measurements.size());
        state.calibrationCountOld = static_cast<int>(state.calibrations.size());
        state.editor.useUnit = (0 != IniFileDataBaseReadYesNo(section, "UseUnit", 0, fd));
        state.editor.useConversion = (0 != IniFileDataBaseReadYesNo(section, "UseConversion", 0, fd));
        state.editor.useMinMax = (0 != IniFileDataBaseReadYesNo(section, "UseMinMax", 0, fd));
    }
}

void BusConfigDialogManager::SnapshotXcpEth()
{
    const int fd{GetMainFileDescriptor()};
    for (int t = 0; t < BUS_CONNECTION_COUNT; ++t)
    {
        XcpEthTargetState &state = m_xcpEth.at(static_cast<std::size_t>(t));
        state = XcpEthTargetState{};

        char section[128];
        std::snprintf(section, sizeof(section), "XCP over Ethernet Configuration for Target %d", t);

        state.enable = (0 != IniFileDataBaseReadYesNo(section, "Enable", 0, fd));
        CopyToArray(state.identification, ReadIniString(section, "Identification", "", fd));
        CopyToArray(state.associatedProcess, ReadIniString(section, "AssociatedProcessName", "", fd));
        CopyToArray(state.dataSegmentName, ReadIniString(section, "CalibrationDataSegmentName", "", fd));
        CopyToArray(state.codeSegmentName, ReadIniString(section, "CodeSegmentName", "", fd));
        state.port = static_cast<int>(IniFileDataBaseReadUInt(section, "Port", 1802, fd));
        state.addressTranslationForDlls =
            (0 != IniFileDataBaseReadYesNo(section, "AddressTranslationForProcessesInsideDll", 0, fd));
        state.debugFile = (0 != IniFileDataBaseReadYesNo(section, "DebugFile", 0, fd));
        CopyToArray(state.debugFileName, ReadIniString(section, "DebugFileName", "", fd));
        state.tasks = ReadNumberedList(section, "daq_", fd);
        state.taskCountOld = static_cast<int>(state.tasks.size());
    }
}

void BusConfigDialogManager::SnapshotCanGlobal()
{
    const int fd{GetMainFileDescriptor()};
    m_can = CanGlobalState{};

    const int countExt{IniFileDataBaseReadInt("CAN/Global", "can_controller_count_ext", 0, fd)};
    const int count{IniFileDataBaseReadInt("CAN/Global", "can_controller_count", 0, fd)};
    int channels{(countExt >= 4) ? countExt : count};
    if (channels > MAX_CAN_CHANNELS_LOCAL)
    {
        channels = MAX_CAN_CHANNELS_LOCAL;
    }
    if (channels < 1)
    {
        channels = 1;
    }
    m_can.numberOfChannels = channels;

    m_can.dontUseUnits = (0 != IniFileDataBaseReadInt("CAN/Global", "dont use units for blackboard", 0, fd));
    m_can.restartImmediately = (0 != IniFileDataBaseReadInt("CAN/Global", "restart CAN immediately", 0, fd));
    m_can.enableGatewayDeviceDriver =
        (0 != IniFileDataBaseReadYesNo("CAN/Global", "enable gateway device driver", 0, fd));
    m_can.enableGatewayVirtual =
        (0 != IniFileDataBaseReadUInt("CAN/Global", "enable gateway virtual device driver", 1, fd));
    m_can.enableGatewayReal =
        (0 != IniFileDataBaseReadUInt("CAN/Global", "enable gateway real device driver", 1, fd));

    for (int c = 0; c < channels; ++c)
    {
        CanChannelState &channel = m_can.channels.at(static_cast<std::size_t>(c));
        char entry[64];
        std::snprintf(entry, sizeof(entry), "can_controller%d_startup_state", c + 1);
        channel.startupState = IniFileDataBaseReadInt("CAN/Global", entry, 1, fd);
        std::snprintf(entry, sizeof(entry), "can_controller%d_baudrate_config", c + 1);
        channel.busConfig = IniFileDataBaseReadInt("CAN/Global", entry, 1, fd);
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
        std::snprintf(entry, sizeof(entry), "can_controller%d_variante", c + 1);
        channel.varianteList = ReadIniString("CAN/Global", entry, "", fd);
    }
}

// -------------------------------------------------------------------------
// Apply (edit state -> ini)
// -------------------------------------------------------------------------

void BusConfigDialogManager::ApplyCcp()
{
    const int fd{GetMainFileDescriptor()};
    for (int t = 0; t < BUS_CONNECTION_COUNT; ++t)
    {
        CcpTargetState &state = m_ccp.at(static_cast<std::size_t>(t));
        char section[128];
        std::snprintf(section, sizeof(section), "CCP Configuration for Target %d", t);

        (void)IniFileDataBaseWriteYesNo(section, "Debug", state.debug ? 1 : 0, fd);
        (void)IniFileDataBaseWriteString(section, "Protocol Version",
                                         CCP_VERSION_LABELS[state.protocolVersionIndex], fd);
        (void)IniFileDataBaseWriteString(section, "ByteOrder", BYTE_ORDER_LABELS[state.byteOrderIndex], fd);
        (void)IniFileDataBaseWriteInt(section, "Channel", state.channel, fd);
        (void)IniFileDataBaseWriteInt(section, "Timeout", state.timeout, fd);
        (void)IniFileDataBaseWriteString(section, "SeedKeyDll", state.seedKeyDll.data(), fd);
        (void)IniFileDataBaseWriteInt(section, "SeedKeyFlag", state.seedAndKey ? 1 : 0, fd);
        (void)IniFileDataBaseWriteString(section, "SeedKeyDllCal", state.seedKeyDllCal.data(), fd);
        (void)IniFileDataBaseWriteUIntHex(section, "CRO_ID", ParseHexField(state.croId.data()), fd);
        (void)IniFileDataBaseWriteUIntHex(section, "CRM_ID", ParseHexField(state.crmId.data()), fd);
        (void)IniFileDataBaseWriteUIntHex(section, "DTO_ID", ParseHexField(state.dtoId.data()), fd);
        (void)IniFileDataBaseWriteInt(section, "Prescaler", state.prescaler, fd);
        (void)IniFileDataBaseWriteUIntHex(section, "StationAddress",
                                          ParseHexField(state.stationAddress.data()), fd);
        (void)IniFileDataBaseWriteInt(section, "ExtIds", state.extIds ? 1 : 0, fd);
        (void)IniFileDataBaseWriteInt(section, "EventChannel", state.eventChannel, fd);
        (void)IniFileDataBaseWriteYesNo(section, "OnlyBytePointerInDAQ", state.onlyBytePointerInDaq, fd);
        (void)IniFileDataBaseWriteYesNo(section, "CalibrationEnable", state.calibrationEnable ? 1 : 0, fd);
        (void)IniFileDataBaseWriteYesNo(section, "MoveROM2RAM", state.moveRomToRam ? 1 : 0, fd);
        (void)IniFileDataBaseWriteYesNo(section, "SelCalPage", state.setCalibrationPage ? 1 : 0, fd);
        (void)IniFileDataBaseWriteInt(section, "ReadParameterAfterCalib",
                                      state.readParameterAfterCalib ? 1 : 0, fd);
        (void)IniFileDataBaseWriteUIntHex(section, "CalibROMStartAddr",
                                          ParseHexField(state.calibRomAddr.data()), fd);
        (void)IniFileDataBaseWriteUIntHex(section, "CalibRAMStartAddr",
                                          ParseHexField(state.calibRamAddr.data()), fd);
        (void)IniFileDataBaseWriteUIntHex(section, "CalibROMRAMSize",
                                          ParseHexField(state.calibRomRamSize.data()), fd);

        WriteNumberedList(section, "v", state.measurements, state.measurementCountOld, fd);
        WriteNumberedList(section, "p", state.calibrations, state.calibrationCountOld, fd);
        state.measurementCountOld = static_cast<int>(state.measurements.size());
        state.calibrationCountOld = static_cast<int>(state.calibrations.size());

        (void)IniFileDataBaseWriteYesNo(section, "UseUnit", state.editor.useUnit ? 1 : 0, fd);
        (void)IniFileDataBaseWriteYesNo(section, "UseConversion", state.editor.useConversion ? 1 : 0, fd);
        (void)IniFileDataBaseWriteYesNo(section, "UseMinMax", state.editor.useMinMax ? 1 : 0, fd);
    }
}

void BusConfigDialogManager::ApplyXcp()
{
    const int fd{GetMainFileDescriptor()};
    for (int t = 0; t < BUS_CONNECTION_COUNT; ++t)
    {
        XcpTargetState &state = m_xcp.at(static_cast<std::size_t>(t));
        char section[128];
        std::snprintf(section, sizeof(section), "XCP Configuration for Target %d", t);

        (void)IniFileDataBaseWriteYesNo(section, "DebugMessages", state.debug ? 1 : 0, fd);
        (void)IniFileDataBaseWriteString(section, "ByteOrder", BYTE_ORDER_LABELS[state.byteOrderIndex], fd);
        (void)IniFileDataBaseWriteInt(section, "Channel", state.channel, fd);
        (void)IniFileDataBaseWriteInt(section, "Timeout", state.timeout, fd);
        (void)IniFileDataBaseWriteString(section, "SeedKeyDll", state.seedKeyDll.data(), fd);
        (void)IniFileDataBaseWriteUIntHex(section, "CRO_ID", ParseHexField(state.croId.data()), fd);
        (void)IniFileDataBaseWriteUIntHex(section, "CRM_ID", ParseHexField(state.crmId.data()), fd);
        (void)IniFileDataBaseWriteUIntHex(section, "DTO_ID", ParseHexField(state.dtoId.data()), fd);
        (void)IniFileDataBaseWriteInt(section, "Prescaler", state.prescaler, fd);
        (void)IniFileDataBaseWriteInt(section, "ExtIds", state.extIds ? 1 : 0, fd);
        (void)IniFileDataBaseWriteInt(section, "EventChannel", state.eventChannel, fd);
        (void)IniFileDataBaseWriteYesNo(section, "OnlyBytePointerInDAQ", state.onlyBytePointerInDaq, fd);
        (void)IniFileDataBaseWriteYesNo(section, "CalibrationEnable", state.calibrationEnable ? 1 : 0, fd);
        (void)IniFileDataBaseWriteYesNo(section, "MoveROM2RAM", state.moveRomToRam ? 1 : 0, fd);
        (void)IniFileDataBaseWriteYesNo(section, "SelCalPage", state.setCalibrationPage ? 1 : 0, fd);
        (void)IniFileDataBaseWriteInt(section, "ReadParameterAfterCalib",
                                      state.readParameterAfterCalib ? 1 : 0, fd);
        (void)IniFileDataBaseWriteUIntHex(section, "CalibROMSegment",
                                          ParseHexField(state.calibRomSegment.data()), fd);
        (void)IniFileDataBaseWriteUIntHex(section, "CalibROMPageNo",
                                          ParseHexField(state.calibRomPage.data()), fd);
        (void)IniFileDataBaseWriteUIntHex(section, "CalibRAMSegment",
                                          ParseHexField(state.calibRamSegment.data()), fd);
        (void)IniFileDataBaseWriteUIntHex(section, "CalibRAMPageNo",
                                          ParseHexField(state.calibRamPage.data()), fd);

        WriteNumberedList(section, "v", state.measurements, state.measurementCountOld, fd);
        WriteNumberedList(section, "p", state.calibrations, state.calibrationCountOld, fd);
        state.measurementCountOld = static_cast<int>(state.measurements.size());
        state.calibrationCountOld = static_cast<int>(state.calibrations.size());

        (void)IniFileDataBaseWriteYesNo(section, "UseUnit", state.editor.useUnit ? 1 : 0, fd);
        (void)IniFileDataBaseWriteYesNo(section, "UseConversion", state.editor.useConversion ? 1 : 0, fd);
        (void)IniFileDataBaseWriteYesNo(section, "UseMinMax", state.editor.useMinMax ? 1 : 0, fd);
    }
}

void BusConfigDialogManager::ApplyXcpEth()
{
    const int fd{GetMainFileDescriptor()};
    for (int t = 0; t < BUS_CONNECTION_COUNT; ++t)
    {
        XcpEthTargetState &state = m_xcpEth.at(static_cast<std::size_t>(t));
        char section[128];
        std::snprintf(section, sizeof(section), "XCP over Ethernet Configuration for Target %d", t);

        (void)IniFileDataBaseWriteYesNo(section, "Enable", state.enable ? 1 : 0, fd);
        (void)IniFileDataBaseWriteString(section, "Identification", state.identification.data(), fd);
        (void)IniFileDataBaseWriteString(section, "AssociatedProcessName",
                                         state.associatedProcess.data(), fd);
        (void)IniFileDataBaseWriteString(section, "CalibrationDataSegmentName",
                                         state.dataSegmentName.data(), fd);
        (void)IniFileDataBaseWriteString(section, "CodeSegmentName", state.codeSegmentName.data(), fd);
        (void)IniFileDataBaseWriteUInt(section, "Port", static_cast<unsigned int>(state.port), fd);
        (void)IniFileDataBaseWriteYesNo(section, "AddressTranslationForProcessesInsideDll",
                                        state.addressTranslationForDlls ? 1 : 0, fd);
        (void)IniFileDataBaseWriteYesNo(section, "DebugFile", state.debugFile ? 1 : 0, fd);
        (void)IniFileDataBaseWriteString(section, "DebugFileName", state.debugFileName.data(), fd);

        WriteNumberedList(section, "daq_", state.tasks, state.taskCountOld, fd);
        state.taskCountOld = static_cast<int>(state.tasks.size());
    }
}

void BusConfigDialogManager::ApplyCanGlobal()
{
    const int fd{GetMainFileDescriptor()};
    const int channels{m_can.numberOfChannels};

    (void)IniFileDataBaseWriteInt("CAN/Global", "can_controller_count_ext", channels, fd);
    (void)IniFileDataBaseWriteInt("CAN/Global", "can_controller_count", (channels > 4) ? 4 : channels, fd);
    (void)IniFileDataBaseWriteString("CAN/Global", "dont use units for blackboard",
                                     m_can.dontUseUnits ? "1" : "0", fd);
    (void)IniFileDataBaseWriteString("CAN/Global", "restart CAN immediately",
                                     m_can.restartImmediately ? "1" : "0", fd);
    (void)IniFileDataBaseWriteYesNo("CAN/Global", "enable gateway device driver",
                                    m_can.enableGatewayDeviceDriver ? 1 : 0, fd);
    if (m_can.enableGatewayDeviceDriver)
    {
        (void)IniFileDataBaseWriteUInt("CAN/Global", "enable gateway virtual device driver",
                                       m_can.enableGatewayVirtual ? 1u : 0u, fd);
        (void)IniFileDataBaseWriteUInt("CAN/Global", "enable gateway real device driver",
                                       m_can.enableGatewayReal ? 1u : 0u, fd);
    }

    for (int c = 0; c < channels; ++c)
    {
        const CanChannelState &channel = m_can.channels.at(static_cast<std::size_t>(c));
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
        // Preserve the existing variant assignment (the variant/object/signal tree is not edited here).
        std::snprintf(entry, sizeof(entry), "can_controller%d_variante", c + 1);
        (void)IniFileDataBaseWriteString("CAN/Global", entry, channel.varianteList.c_str(), fd);
    }

    if (m_can.restartImmediately)
    {
        const PID pid{get_pid_by_name("CANServer")};
        if (pid > 0)
        {
            (void)write_message(pid, NEW_CANSERVER_STOP, 0, nullptr);
        }
    }
}

// -------------------------------------------------------------------------
// Open methods
// -------------------------------------------------------------------------

void BusConfigDialogManager::OpenCanConfig()
{
    SnapshotCanGlobal();
    m_canOpen = true;
    m_canOpenRequested = true;
}

void BusConfigDialogManager::OpenDbcImport()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    m_filePickerDir = fs::current_path(ec).string();
    m_filePickerSaveMode = false;
    m_filePickerExt = ".dbc";
    m_filePickerTitle = "Select CAN DBC file";
    m_filePickerAction = BusFileAction::DbcSelect;
    m_filePickerOpen = true;
    m_filePickerOpenRequested = true;
}

void BusConfigDialogManager::OpenCcpConfig()
{
    SnapshotCcp();
    m_ccpOpen = true;
    m_ccpOpenRequested = true;
}

void BusConfigDialogManager::OpenXcpConfig()
{
    SnapshotXcp();
    m_xcpOpen = true;
    m_xcpOpenRequested = true;
}

void BusConfigDialogManager::OpenXcpOverEthernetConfig()
{
    SnapshotXcpEth();
    m_xcpEthOpen = true;
    m_xcpEthOpenRequested = true;
}

void BusConfigDialogManager::OpenCcpImport()
{
    m_connPickerAction = BusFileAction::CcpImport;
    m_connPickerTitle = "Import CCP configuration";
    m_connPickerConnection = 0;
    m_connPickerOpen = true;
    m_connPickerOpenRequested = true;
}

void BusConfigDialogManager::OpenCcpExport()
{
    m_connPickerAction = BusFileAction::CcpExport;
    m_connPickerTitle = "Export CCP configuration";
    m_connPickerConnection = 0;
    m_connPickerOpen = true;
    m_connPickerOpenRequested = true;
}

void BusConfigDialogManager::OpenXcpImport()
{
    m_connPickerAction = BusFileAction::XcpImport;
    m_connPickerTitle = "Import XCP configuration";
    m_connPickerConnection = 0;
    m_connPickerOpen = true;
    m_connPickerOpenRequested = true;
}

void BusConfigDialogManager::OpenXcpExport()
{
    m_connPickerAction = BusFileAction::XcpExport;
    m_connPickerTitle = "Export XCP configuration";
    m_connPickerConnection = 0;
    m_connPickerOpen = true;
    m_connPickerOpenRequested = true;
}

// -------------------------------------------------------------------------
// Rendering
// -------------------------------------------------------------------------

void BusConfigDialogManager::Render()
{
    RenderCanConfig();
    RenderCcpConfig();
    RenderXcpConfig();
    RenderXcpOverEthernetConfig();
    RenderDbcImport();
    RenderConnectionFilePicker();
    RenderFilePicker();
}

void BusConfigDialogManager::RenderVarListEditor(const char *par_Id, VarLabelEditorState &par_Editor,
                                                 std::vector<std::string> &par_Measurements,
                                                 std::vector<std::string> &par_Calibrations,
                                                 int &par_ActiveTab, int &par_SelectedRow)
{
    ImGui::PushID(par_Id);

    if (ImGui::BeginTabBar("##listtabs"))
    {
        if (ImGui::BeginTabItem("Measurements"))
        {
            par_ActiveTab = 0;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Calibrations"))
        {
            par_ActiveTab = 1;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    std::vector<std::string> &list{(0 == par_ActiveTab) ? par_Measurements : par_Calibrations};
    if (par_SelectedRow >= static_cast<int>(list.size()))
    {
        par_SelectedRow = -1;
    }

    const int clicked{DrawStringListBox("##labellist", list, par_SelectedRow, ImVec2(-1.0f, 140.0f))};
    if (clicked >= 0)
    {
        par_SelectedRow = clicked;
        LoadLabelLine(list.at(static_cast<std::size_t>(clicked)), par_Editor);
    }

    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo("Data type", &par_Editor.dataTypeIndex, DATA_TYPE_LABELS,
                 static_cast<int>(std::size(DATA_TYPE_LABELS)));
    ImGui::InputText("Name", par_Editor.name.data(), par_Editor.name.size());
    ImGui::InputText("Address", par_Editor.address.data(), par_Editor.address.size());
    ImGui::Checkbox("Unit", &par_Editor.useUnit);
    if (par_Editor.useUnit)
    {
        ImGui::SameLine();
        ImGui::InputText("##unit", par_Editor.unit.data(), par_Editor.unit.size());
    }
    ImGui::Checkbox("Conversion", &par_Editor.useConversion);
    if (par_Editor.useConversion)
    {
        ImGui::SameLine();
        ImGui::InputText("##conv", par_Editor.conversion.data(), par_Editor.conversion.size());
    }
    ImGui::Checkbox("Min/Max", &par_Editor.useMinMax);
    if (par_Editor.useMinMax)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::InputText("##min", par_Editor.minValue.data(), par_Editor.minValue.size());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::InputText("##max", par_Editor.maxValue.data(), par_Editor.maxValue.size());
    }

    if (ImGui::Button("Add behind"))
    {
        const std::string line{BuildLabelLine(par_Editor)};
        const int insertAt{(par_SelectedRow >= 0) ? (par_SelectedRow + 1) : static_cast<int>(list.size())};
        list.insert(list.begin() + insertAt, line);
    }
    ImGui::SameLine();
    if (ImGui::Button("Add before"))
    {
        const std::string line{BuildLabelLine(par_Editor)};
        const int insertAt{(par_SelectedRow >= 0) ? par_SelectedRow : 0};
        list.insert(list.begin() + insertAt, line);
    }
    ImGui::SameLine();
    if (ImGui::Button("Change") && (par_SelectedRow >= 0) &&
        (par_SelectedRow < static_cast<int>(list.size())))
    {
        list.at(static_cast<std::size_t>(par_SelectedRow)) = BuildLabelLine(par_Editor);
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove") && (par_SelectedRow >= 0) &&
        (par_SelectedRow < static_cast<int>(list.size())))
    {
        list.erase(list.begin() + par_SelectedRow);
        par_SelectedRow = -1;
    }

    ImGui::PopID();
}

void BusConfigDialogManager::RenderCcpConfig()
{
    if (m_ccpOpenRequested)
    {
        ImGui::OpenPopup("CCP configuration###CcpConfig");
        m_ccpOpenRequested = false;
    }
    if (!m_ccpOpen)
    {
        return;
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620.0f, 720.0f), ImGuiCond_Appearing);

    bool open{true};
    if (ImGui::BeginPopupModal("CCP configuration###CcpConfig", &open, ImGuiWindowFlags_NoSavedSettings))
    {
        if (ImGui::BeginTabBar("##ccptargets"))
        {
            for (int t = 0; t < BUS_CONNECTION_COUNT; ++t)
            {
                char label[32];
                std::snprintf(label, sizeof(label), "Target %d", t);
                if (ImGui::BeginTabItem(label))
                {
                    m_activeCcpTab = t;
                    CcpTargetState &s = m_ccp.at(static_cast<std::size_t>(t));
                    ImGui::PushID(t);
                    if (ImGui::BeginChild("##ccpscroll", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() - 8.0f)))
                    {
                        ImGui::SeparatorText("Connection");
                        ImGui::Combo("Protocol version", &s.protocolVersionIndex, CCP_VERSION_LABELS,
                                     static_cast<int>(std::size(CCP_VERSION_LABELS)));
                        ImGui::Combo("Byte order", &s.byteOrderIndex, BYTE_ORDER_LABELS,
                                     static_cast<int>(std::size(BYTE_ORDER_LABELS)));
                        ImGui::InputInt("Channel", &s.channel);
                        ImGui::Checkbox("Debug messages", &s.debug);
                        ImGui::InputText("CRO id", s.croId.data(), s.croId.size());
                        ImGui::InputText("CRM id", s.crmId.data(), s.crmId.size());
                        ImGui::InputText("DTO id", s.dtoId.data(), s.dtoId.size());
                        ImGui::InputText("Station address", s.stationAddress.data(), s.stationAddress.size());
                        ImGui::InputInt("Timeout [ms]", &s.timeout);
                        ImGui::InputInt("Event channel", &s.eventChannel);
                        ImGui::InputInt("Prescaler", &s.prescaler);
                        ImGui::Checkbox("Extended identifier", &s.extIds);

                        ImGui::SeparatorText("Seed and key");
                        ImGui::Checkbox("Enable seed and key", &s.seedAndKey);
                        if (s.seedAndKey)
                        {
                            ImGui::InputText("DAQ DLL", s.seedKeyDll.data(), s.seedKeyDll.size());
                            ImGui::InputText("CAL DLL", s.seedKeyDllCal.data(), s.seedKeyDllCal.size());
                        }

                        ImGui::SeparatorText("Calibration");
                        ImGui::Checkbox("Enable calibration", &s.calibrationEnable);
                        if (s.calibrationEnable)
                        {
                            ImGui::Checkbox("Move ROM to RAM", &s.moveRomToRam);
                            ImGui::Checkbox("Set calibration page", &s.setCalibrationPage);
                            ImGui::Checkbox("Read parameter after calibration", &s.readParameterAfterCalib);
                            if (s.moveRomToRam)
                            {
                                ImGui::InputText("Calibration ROM address", s.calibRomAddr.data(),
                                                 s.calibRomAddr.size());
                                ImGui::InputText("Calibration RAM address", s.calibRamAddr.data(),
                                                 s.calibRamAddr.size());
                                ImGui::InputText("Calibration ROM/RAM size", s.calibRomRamSize.data(),
                                                 s.calibRomRamSize.size());
                            }
                        }

                        ImGui::SeparatorText("Measurement / calibration labels");
                        RenderVarListEditor("ccp", s.editor, s.measurements, s.calibrations, s.activeTab,
                                            s.selectedRow);
                    }
                    ImGui::EndChild();
                    ImGui::PopID();
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }

        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(100.0f, 0.0f)))
        {
            ApplyCcp();
            m_ccpOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Import...", ImVec2(100.0f, 0.0f)))
        {
            OpenCcpImport();
        }
        ImGui::SameLine();
        if (ImGui::Button("Export...", ImVec2(100.0f, 0.0f)))
        {
            OpenCcpExport();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
        {
            m_ccpOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!open)
    {
        m_ccpOpen = false;
    }
}

void BusConfigDialogManager::RenderXcpConfig()
{
    if (m_xcpOpenRequested)
    {
        ImGui::OpenPopup("XCP configuration###XcpConfig");
        m_xcpOpenRequested = false;
    }
    if (!m_xcpOpen)
    {
        return;
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620.0f, 720.0f), ImGuiCond_Appearing);

    bool open{true};
    if (ImGui::BeginPopupModal("XCP configuration###XcpConfig", &open, ImGuiWindowFlags_NoSavedSettings))
    {
        if (ImGui::BeginTabBar("##xcptargets"))
        {
            for (int t = 0; t < BUS_CONNECTION_COUNT; ++t)
            {
                char label[32];
                std::snprintf(label, sizeof(label), "Target %d", t);
                if (ImGui::BeginTabItem(label))
                {
                    m_activeXcpTab = t;
                    XcpTargetState &s = m_xcp.at(static_cast<std::size_t>(t));
                    ImGui::PushID(t);
                    if (ImGui::BeginChild("##xcpscroll", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() - 8.0f)))
                    {
                        ImGui::SeparatorText("Connection");
                        ImGui::Combo("Byte order", &s.byteOrderIndex, BYTE_ORDER_LABELS,
                                     static_cast<int>(std::size(BYTE_ORDER_LABELS)));
                        ImGui::InputInt("Channel", &s.channel);
                        ImGui::Checkbox("Debug messages", &s.debug);
                        ImGui::InputText("CRO id", s.croId.data(), s.croId.size());
                        ImGui::InputText("CRM id", s.crmId.data(), s.crmId.size());
                        ImGui::InputText("DTO id", s.dtoId.data(), s.dtoId.size());
                        ImGui::InputInt("Timeout [ms]", &s.timeout);
                        ImGui::InputInt("Event channel", &s.eventChannel);
                        ImGui::InputInt("Prescaler", &s.prescaler);
                        ImGui::Checkbox("Extended identifier", &s.extIds);
                        ImGui::InputText("Seed and key DLL", s.seedKeyDll.data(), s.seedKeyDll.size());

                        ImGui::SeparatorText("Calibration");
                        ImGui::Checkbox("Enable calibration", &s.calibrationEnable);
                        if (s.calibrationEnable)
                        {
                            ImGui::Checkbox("Move ROM to RAM", &s.moveRomToRam);
                            ImGui::Checkbox("Set calibration page", &s.setCalibrationPage);
                            ImGui::Checkbox("Read parameter after calibration", &s.readParameterAfterCalib);
                            if (s.moveRomToRam)
                            {
                                ImGui::InputText("Calibration ROM segment", s.calibRomSegment.data(),
                                                 s.calibRomSegment.size());
                                ImGui::InputText("Calibration ROM page no.", s.calibRomPage.data(),
                                                 s.calibRomPage.size());
                                ImGui::InputText("Calibration RAM segment", s.calibRamSegment.data(),
                                                 s.calibRamSegment.size());
                                ImGui::InputText("Calibration RAM page no.", s.calibRamPage.data(),
                                                 s.calibRamPage.size());
                            }
                        }

                        ImGui::SeparatorText("Measurement / calibration labels");
                        RenderVarListEditor("xcp", s.editor, s.measurements, s.calibrations, s.activeTab,
                                            s.selectedRow);
                    }
                    ImGui::EndChild();
                    ImGui::PopID();
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }

        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(100.0f, 0.0f)))
        {
            ApplyXcp();
            m_xcpOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Import...", ImVec2(100.0f, 0.0f)))
        {
            OpenXcpImport();
        }
        ImGui::SameLine();
        if (ImGui::Button("Export...", ImVec2(100.0f, 0.0f)))
        {
            OpenXcpExport();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
        {
            m_xcpOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!open)
    {
        m_xcpOpen = false;
    }
}

void BusConfigDialogManager::RenderXcpOverEthernetConfig()
{
    if (m_xcpEthOpenRequested)
    {
        ImGui::OpenPopup("Configure XCP over ethernet###XcpEthConfig");
        m_xcpEthOpenRequested = false;
    }
    if (!m_xcpEthOpen)
    {
        return;
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620.0f, 660.0f), ImGuiCond_Appearing);

    bool open{true};
    if (ImGui::BeginPopupModal("Configure XCP over ethernet###XcpEthConfig", &open,
                               ImGuiWindowFlags_NoSavedSettings))
    {
        if (ImGui::BeginTabBar("##xcpethtargets"))
        {
            for (int t = 0; t < BUS_CONNECTION_COUNT; ++t)
            {
                char label[32];
                std::snprintf(label, sizeof(label), "Connection %d", t);
                if (ImGui::BeginTabItem(label))
                {
                    m_activeXcpEthTab = t;
                    XcpEthTargetState &s = m_xcpEth.at(static_cast<std::size_t>(t));
                    ImGui::PushID(t);
                    if (ImGui::BeginChild("##xcpethscroll", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() - 8.0f)))
                    {
                        ImGui::Checkbox("Enable connection", &s.enable);
                        ImGui::BeginDisabled(!s.enable);
                        ImGui::InputText("Identification", s.identification.data(), s.identification.size());
                        ImGui::InputText("Associated process", s.associatedProcess.data(),
                                         s.associatedProcess.size());
                        ImGui::InputText("Data segment name", s.dataSegmentName.data(),
                                         s.dataSegmentName.size());
                        ImGui::InputText("Code segment name", s.codeSegmentName.data(),
                                         s.codeSegmentName.size());
                        ImGui::InputInt("Port", &s.port);
                        ImGui::Checkbox("Address translation for DLL processes",
                                        &s.addressTranslationForDlls);
                        ImGui::Checkbox("Write debug file", &s.debugFile);
                        ImGui::InputText("Debug file", s.debugFileName.data(), s.debugFileName.size());

                        ImGui::SeparatorText("Measurement tasks");
                        if (s.selectedRow >= static_cast<int>(s.tasks.size()))
                        {
                            s.selectedRow = -1;
                        }
                        const int clicked{DrawStringListBox("##tasklist", s.tasks, s.selectedRow,
                                                            ImVec2(-1.0f, 140.0f))};
                        if (clicked >= 0)
                        {
                            s.selectedRow = clicked;
                            const std::vector<std::string> fields{SplitLine(s.tasks.at(static_cast<std::size_t>(clicked)))};
                            if (fields.size() >= 4u)
                            {
                                s.timestampIndex = 0;
                                for (int i = 0; i < static_cast<int>(std::size(TIMESTAMP_LABELS)); ++i)
                                {
                                    if (0 == fields.at(0).compare(TIMESTAMP_LABELS[i]))
                                    {
                                        s.timestampIndex = i;
                                        break;
                                    }
                                }
                                CopyToArray(s.taskCycle, fields.at(1));
                                CopyToArray(s.taskPrio, fields.at(2));
                                CopyToArray(s.taskName, fields.at(3));
                            }
                        }
                        ImGui::SetNextItemWidth(140.0f);
                        ImGui::Combo("Timestamp", &s.timestampIndex, TIMESTAMP_LABELS,
                                     static_cast<int>(std::size(TIMESTAMP_LABELS)));
                        ImGui::InputText("Name", s.taskName.data(), s.taskName.size());
                        ImGui::InputText("Cycle", s.taskCycle.data(), s.taskCycle.size());
                        ImGui::InputText("Prio", s.taskPrio.data(), s.taskPrio.size());
                        if (ImGui::Button("Add task"))
                        {
                            std::string line{TIMESTAMP_LABELS[s.timestampIndex]};
                            line.append(",");
                            line.append(Trim(s.taskCycle.data()));
                            line.append(",");
                            line.append(Trim(s.taskPrio.data()));
                            line.append(",");
                            line.append(Trim(s.taskName.data()));
                            const int insertAt{(s.selectedRow >= 0) ? (s.selectedRow + 1)
                                                                    : static_cast<int>(s.tasks.size())};
                            s.tasks.insert(s.tasks.begin() + insertAt, line);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Remove task") && (s.selectedRow >= 0) &&
                            (s.selectedRow < static_cast<int>(s.tasks.size())))
                        {
                            s.tasks.erase(s.tasks.begin() + s.selectedRow);
                            s.selectedRow = -1;
                        }
                        ImGui::EndDisabled();
                    }
                    ImGui::EndChild();
                    ImGui::PopID();
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }

        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(100.0f, 0.0f)))
        {
            ApplyXcpEth();
            m_xcpEthOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
        {
            m_xcpEthOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!open)
    {
        m_xcpEthOpen = false;
    }
}

void BusConfigDialogManager::RenderCanConfig()
{
    if (m_canOpenRequested)
    {
        ImGui::OpenPopup("CAN configuration###CanConfig");
        m_canOpenRequested = false;
    }
    if (!m_canOpen)
    {
        return;
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(720.0f, 620.0f), ImGuiCond_Appearing);

    bool open{true};
    if (ImGui::BeginPopupModal("CAN configuration###CanConfig", &open, ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::TextWrapped("Global CAN bus / controller configuration. The variant / object / signal "
                           "database editor and channel-to-variant mapping are not part of this dialog; "
                           "existing variant assignments are preserved.");
        ImGui::Separator();

        if (ImGui::BeginChild("##canscroll", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() - 8.0f)))
        {
            ImGui::SeparatorText("Global");
            if (ImGui::InputInt("Number of channels", &m_can.numberOfChannels))
            {
                m_can.numberOfChannels = std::clamp(m_can.numberOfChannels, 1, MAX_CAN_CHANNELS_LOCAL);
            }
            ImGui::Checkbox("Do not use units for blackboard", &m_can.dontUseUnits);
            ImGui::Checkbox("Restart CAN immediately on apply", &m_can.restartImmediately);
            ImGui::Checkbox("Enable gateway device driver", &m_can.enableGatewayDeviceDriver);
            if (m_can.enableGatewayDeviceDriver)
            {
                ImGui::Checkbox("Virtual device driver", &m_can.enableGatewayVirtual);
                ImGui::SameLine();
                ImGui::Checkbox("Real device driver", &m_can.enableGatewayReal);
            }

            ImGui::SeparatorText("Channels");
            const ImGuiTableFlags tableFlags{ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                             ImGuiTableFlags_SizingStretchProp};
            if (ImGui::BeginTable("##canchannels", 8, tableFlags))
            {
                ImGui::TableSetupColumn("Ch");
                ImGui::TableSetupColumn("Startup");
                ImGui::TableSetupColumn("Bus cfg");
                ImGui::TableSetupColumn("FD");
                ImGui::TableSetupColumn("Baud");
                ImGui::TableSetupColumn("Sample");
                ImGui::TableSetupColumn("Data baud");
                ImGui::TableSetupColumn("Data sample");
                ImGui::TableHeadersRow();

                for (int c = 0; c < m_can.numberOfChannels; ++c)
                {
                    CanChannelState &ch = m_can.channels.at(static_cast<std::size_t>(c));
                    const bool userCfg{BUS_CFG_USER == (ch.busConfig & BUS_CFG_MODE_MASK)};
                    ImGui::PushID(c);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%d", c + 1);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputInt("##startup", &ch.startupState, 0);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputInt("##buscfg", &ch.busConfig, 0);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::BeginDisabled(!userCfg);
                    ImGui::Checkbox("##fd", &ch.fd);
                    ImGui::TableSetColumnIndex(4);
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputInt("##baud", &ch.baudRate, 0);
                    ImGui::TableSetColumnIndex(5);
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputDouble("##sample", &ch.samplePoint, 0.0, 0.0, "%.3f");
                    ImGui::TableSetColumnIndex(6);
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputInt("##databaud", &ch.dataBaudRate, 0);
                    ImGui::TableSetColumnIndex(7);
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputDouble("##datasample", &ch.dataSamplePoint, 0.0, 0.0, "%.3f");
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::TextDisabled("Bus cfg: 0x40000 = user-defined (enables FD/baud/sample fields); "
                                "other values select first/last/selected variant baudrate.");
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(100.0f, 0.0f)))
        {
            ApplyCanGlobal();
            m_canOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
        {
            m_canOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!open)
    {
        m_canOpen = false;
    }
}

void BusConfigDialogManager::RenderDbcImport()
{
    if (m_dbcOpenRequested)
    {
        ImGui::OpenPopup("Import CAN DBC###DbcImport");
        m_dbcOpenRequested = false;
    }
    if (!m_dbcOpen)
    {
        return;
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(720.0f, 720.0f), ImGuiCond_Appearing);

    bool open{true};
    if (ImGui::BeginPopupModal("Import CAN DBC###DbcImport", &open, ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::Text("File: %s", m_dbc.filePath.c_str());
        ImGui::Separator();

        if (ImGui::BeginChild("##dbcscroll", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() - 8.0f)))
        {
            ImGui::SeparatorText("Variant name");
            ImGui::Checkbox("User-defined name", &m_dbc.userDefinedName);
            if (m_dbc.userDefinedName)
            {
                ImGui::InputText("##variantname", m_dbc.variantName.data(), m_dbc.variantName.size());
            }
            else
            {
                ImGui::TextDisabled("Same as DBC file name");
            }

            ImGui::SeparatorText("Bus members");
            ImGui::BeginGroup();
            ImGui::TextUnformatted("Available");
            const int availClicked{DrawStringListBox("##available", m_dbc.available, m_dbc.availableSel,
                                                     ImVec2(200.0f, 150.0f))};
            if (availClicked >= 0)
            {
                m_dbc.availableSel = availClicked;
            }
            ImGui::EndGroup();
            ImGui::SameLine();
            ImGui::BeginGroup();
            if (ImGui::Button("-> Tx") && (m_dbc.availableSel >= 0) &&
                (m_dbc.availableSel < static_cast<int>(m_dbc.available.size())))
            {
                m_dbc.transmit.push_back(m_dbc.available.at(static_cast<std::size_t>(m_dbc.availableSel)));
            }
            if (ImGui::Button("-> Rx") && (m_dbc.availableSel >= 0) &&
                (m_dbc.availableSel < static_cast<int>(m_dbc.available.size())))
            {
                m_dbc.receive.push_back(m_dbc.available.at(static_cast<std::size_t>(m_dbc.availableSel)));
            }
            ImGui::EndGroup();
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextUnformatted("Transmit");
            const int txClicked{DrawStringListBox("##transmit", m_dbc.transmit, m_dbc.transmitSel,
                                                  ImVec2(160.0f, 66.0f))};
            if (txClicked >= 0)
            {
                m_dbc.transmitSel = txClicked;
            }
            if (ImGui::Button("Remove Tx") && (m_dbc.transmitSel >= 0) &&
                (m_dbc.transmitSel < static_cast<int>(m_dbc.transmit.size())))
            {
                m_dbc.transmit.erase(m_dbc.transmit.begin() + m_dbc.transmitSel);
                m_dbc.transmitSel = -1;
            }
            ImGui::TextUnformatted("Receive");
            const int rxClicked{DrawStringListBox("##receive", m_dbc.receive, m_dbc.receiveSel,
                                                  ImVec2(160.0f, 66.0f))};
            if (rxClicked >= 0)
            {
                m_dbc.receiveSel = rxClicked;
            }
            if (ImGui::Button("Remove Rx") && (m_dbc.receiveSel >= 0) &&
                (m_dbc.receiveSel < static_cast<int>(m_dbc.receive.size())))
            {
                m_dbc.receive.erase(m_dbc.receive.begin() + m_dbc.receiveSel);
                m_dbc.receiveSel = -1;
            }
            ImGui::EndGroup();

            ImGui::SeparatorText("Signal naming");
            ImGui::Checkbox("Additional signal prefix", &m_dbc.additionalPrefix);
            if (m_dbc.additionalPrefix)
            {
                ImGui::SameLine();
                ImGui::InputText("##prefix", m_dbc.prefix.data(), m_dbc.prefix.size());
            }
            ImGui::Checkbox("Additional signal postfix", &m_dbc.additionalPostfix);
            if (m_dbc.additionalPostfix)
            {
                ImGui::SameLine();
                ImGui::InputText("##postfix", m_dbc.postfix.data(), m_dbc.postfix.size());
            }
            ImGui::Checkbox("Extend signal name with object name", &m_dbc.extendSignalWithObject);
            ImGui::Checkbox("Object name with double point", &m_dbc.objectNameDoublePoint);

            ImGui::SeparatorText("Transfer settings from existing variant");
            ImGui::Checkbox("Transfer settings", &m_dbc.transferSettings);
            if (m_dbc.transferSettings)
            {
                const char *preview{(m_dbc.transferVariantIndex >= 0 &&
                                     m_dbc.transferVariantIndex < static_cast<int>(m_dbc.variantNames.size()))
                                        ? m_dbc.variantNames.at(static_cast<std::size_t>(m_dbc.transferVariantIndex)).c_str()
                                        : "<select>"};
                if (ImGui::BeginCombo("Variant", preview))
                {
                    for (int i = 0; i < static_cast<int>(m_dbc.variantNames.size()); ++i)
                    {
                        const bool selected{i == m_dbc.transferVariantIndex};
                        if (ImGui::Selectable(m_dbc.variantNames.at(static_cast<std::size_t>(i)).c_str(),
                                              selected))
                        {
                            m_dbc.transferVariantIndex = i;
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::Checkbox("Object additional equations", &m_dbc.objectAdditionalEquations);
                ImGui::Checkbox("Object init data", &m_dbc.objectInitData);
                ImGui::Checkbox("Signal init values", &m_dbc.signalInitValues);
                ImGui::Checkbox("Signal data type", &m_dbc.signalDataType);
                ImGui::Checkbox("Signal equations", &m_dbc.signalEquations);
                ImGui::Checkbox("Sort signals", &m_dbc.sortSignals);
            }

            ImGui::SeparatorText("Export");
            ImGui::Checkbox("Also export to .can file", &m_dbc.exportToFile);
            if (m_dbc.exportToFile)
            {
                ImGui::InputText("##exportname", m_dbc.exportFileName.data(), m_dbc.exportFileName.size());
                ImGui::SameLine();
                if (ImGui::Button("Browse..."))
                {
                    namespace fs = std::filesystem;
                    std::error_code ec;
                    m_filePickerDir = fs::current_path(ec).string();
                    m_filePickerSaveMode = true;
                    m_filePickerExt = ".can";
                    m_filePickerTitle = "Export CAN variant to";
                    m_filePickerAction = BusFileAction::DbcExport;
                    m_filePickerName[0] = '\0';
                    m_filePickerOpen = true;
                    m_filePickerOpenRequested = true;
                }
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (ImGui::Button("Import", ImVec2(100.0f, 0.0f)))
        {
            std::string variantName;
            if (m_dbc.userDefinedName)
            {
                variantName = m_dbc.variantName.data();
            }
            else
            {
                namespace fs = std::filesystem;
                variantName = fs::path{m_dbc.filePath}.stem().string();
            }

            // Build the double-NUL-terminated member lists expected by CANDB_Import.
            const auto buildMemberBlock{[](const std::vector<std::string> &par_Members) {
                std::string block;
                for (const std::string &member : par_Members)
                {
                    block.append(member);
                    block.push_back('\0');
                }
                block.push_back('\0');
                return block;
            }};
            std::string txBlock{buildMemberBlock(m_dbc.transmit)};
            std::string rxBlock{buildMemberBlock(m_dbc.receive)};

            const std::string prefix{m_dbc.additionalPrefix ? std::string{m_dbc.prefix.data()} : std::string{}};
            const std::string postfix{m_dbc.additionalPostfix ? std::string{m_dbc.postfix.data()}
                                                              : std::string{}};
            const int transferIndex{m_dbc.transferSettings ? m_dbc.transferVariantIndex : -1};
            std::string exportName{m_dbc.exportFileName.data()};

            (void)CANDB_Import(m_dbc.filePath.c_str(),
                               (m_dbc.exportToFile && !exportName.empty()) ? exportName.data() : nullptr,
                               variantName.data(), txBlock.data(), rxBlock.data(), prefix.c_str(),
                               postfix.c_str(), (transferIndex >= 0) ? 1 : 0, transferIndex,
                               m_dbc.objectAdditionalEquations ? 1 : 0, m_dbc.sortSignals ? 1 : 0,
                               m_dbc.signalDataType ? 1 : 0, m_dbc.signalEquations ? 1 : 0,
                               m_dbc.signalInitValues ? 1 : 0, m_dbc.objectInitData ? 1 : 0,
                               m_dbc.extendSignalWithObject ? 1 : 0, m_dbc.objectNameDoublePoint ? 1 : 0);
            m_dbcOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
        {
            m_dbcOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!open)
    {
        m_dbcOpen = false;
    }
}

void BusConfigDialogManager::RenderConnectionFilePicker()
{
    if (m_connPickerOpenRequested)
    {
        ImGui::OpenPopup("Select connection###BusConnPicker");
        m_connPickerOpenRequested = false;
    }
    if (!m_connPickerOpen)
    {
        return;
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    bool open{true};
    if (ImGui::BeginPopupModal("Select connection###BusConnPicker", &open,
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(m_connPickerTitle.c_str());
        ImGui::Separator();
        for (int i = 0; i < BUS_CONNECTION_COUNT; ++i)
        {
            char label[32];
            std::snprintf(label, sizeof(label), "Connection %d", i);
            ImGui::RadioButton(label, &m_connPickerConnection, i);
        }
        ImGui::Separator();
        if (ImGui::Button("Continue...", ImVec2(120.0f, 0.0f)))
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            m_filePickerDir = fs::current_path(ec).string();
            m_filePickerConnection = m_connPickerConnection;
            m_filePickerAction = m_connPickerAction;
            switch (m_connPickerAction)
            {
            case BusFileAction::CcpImport:
                m_filePickerSaveMode = false;
                m_filePickerExt = ".ccp";
                m_filePickerTitle = "Open CCP file";
                break;
            case BusFileAction::CcpExport:
                m_filePickerSaveMode = true;
                m_filePickerExt = ".ccp";
                m_filePickerTitle = "Save CCP file";
                break;
            case BusFileAction::XcpImport:
                m_filePickerSaveMode = false;
                m_filePickerExt = ".xcp";
                m_filePickerTitle = "Open XCP file";
                break;
            case BusFileAction::XcpExport:
                m_filePickerSaveMode = true;
                m_filePickerExt = ".xcp";
                m_filePickerTitle = "Save XCP file";
                break;
            default:
                break;
            }
            m_filePickerName[0] = '\0';
            m_filePickerOpen = true;
            m_filePickerOpenRequested = true;
            m_connPickerOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
        {
            m_connPickerOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!open)
    {
        m_connPickerOpen = false;
    }
}

void BusConfigDialogManager::RenderFilePicker()
{
    namespace fs = std::filesystem;

    if (m_filePickerOpenRequested)
    {
        ImGui::OpenPopup("Select file###BusFilePicker");
        m_filePickerOpenRequested = false;
    }
    if (!m_filePickerOpen)
    {
        return;
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560.0f, 480.0f), ImGuiCond_Appearing);

    bool open{true};
    if (ImGui::BeginPopupModal("Select file###BusFilePicker", &open, ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::TextUnformatted(m_filePickerTitle.c_str());
        ImGui::TextDisabled("%s", m_filePickerDir.c_str());
        if (ImGui::Button(".. (up)"))
        {
            const fs::path parent{fs::path{m_filePickerDir}.parent_path()};
            if (!parent.empty())
            {
                m_filePickerDir = parent.string();
            }
        }

        std::error_code ec;
        std::vector<std::string> dirs;
        std::vector<std::string> files;
        for (fs::directory_iterator it{m_filePickerDir, fs::directory_options::skip_permission_denied, ec};
             !ec && (it != fs::directory_iterator{}); it.increment(ec))
        {
            std::error_code typeEc;
            if (it->is_directory(typeEc))
            {
                dirs.push_back(it->path().filename().string());
            }
            else if (it->is_regular_file(typeEc))
            {
                const std::string name{it->path().filename().string()};
                const std::string ext{it->path().extension().string()};
                if (m_filePickerExt.empty() || (0 == ext.compare(m_filePickerExt)))
                {
                    files.push_back(name);
                }
            }
        }
        std::sort(dirs.begin(), dirs.end());
        std::sort(files.begin(), files.end());

        ImGui::BeginChild("##filelist", ImVec2(0.0f, 300.0f), true);
        for (const std::string &name : dirs)
        {
            const std::string label{"[DIR] " + name};
            if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                m_filePickerDir = (fs::path{m_filePickerDir} / name).string();
            }
        }
        for (const std::string &name : files)
        {
            if (ImGui::Selectable(name.c_str()))
            {
                std::snprintf(m_filePickerName.data(), m_filePickerName.size(), "%s", name.c_str());
            }
        }
        ImGui::EndChild();

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##filename", m_filePickerName.data(), m_filePickerName.size());

        const char *confirmLabel{m_filePickerSaveMode ? "Save" : "Open"};
        if (ImGui::Button(confirmLabel, ImVec2(100.0f, 0.0f)) && ('\0' != m_filePickerName[0]))
        {
            const fs::path chosen{fs::path{m_filePickerDir} / m_filePickerName.data()};
            std::string chosenStr{chosen.string()};

            switch (m_filePickerAction)
            {
            case BusFileAction::CcpImport:
                if (0 != LoadConfig_CCP(m_filePickerConnection, chosenStr.c_str()))
                {
                    DialogManager::GetInstance().AddErrorMessage(2, "Import CCP configuration",
                                                                 "cannot import the CCP configuration file");
                }
                break;
            case BusFileAction::CcpExport:
                (void)SaveConfig_CCP(m_filePickerConnection, chosenStr.data());
                break;
            case BusFileAction::XcpImport:
                if (0 != LoadConfig_XCP(m_filePickerConnection, chosenStr.data()))
                {
                    DialogManager::GetInstance().AddErrorMessage(2, "Import XCP configuration",
                                                                 "cannot import the XCP configuration file");
                }
                break;
            case BusFileAction::XcpExport:
                (void)SaveConfig_XCP(m_filePickerConnection, chosenStr.data());
                break;
            case BusFileAction::DbcSelect:
            {
                m_dbc = DbcImportState{};
                m_dbc.filePath = chosenStr;
                char *members{nullptr};
                if (CANDB_GetAllBusMembers(chosenStr.c_str(), &members) >= 1 && (nullptr != members))
                {
                    char *cursor{members};
                    while ('\0' != *cursor)
                    {
                        m_dbc.available.emplace_back(cursor);
                        cursor += std::strlen(cursor) + 1u;
                    }
                    CANDB_FreeMembersList(members);
                }
                // Populate existing variant names for the transfer-settings combo.
                const int fd{GetMainFileDescriptor()};
                const int variantCount{IniFileDataBaseReadInt("CAN/Global", "can_varianten_count", 0, fd)};
                for (int i = 0; i < variantCount; ++i)
                {
                    char section[64];
                    std::snprintf(section, sizeof(section), "CAN/Variante_%d", i);
                    m_dbc.variantNames.push_back(ReadIniString(section, "name", "", fd));
                }
                m_dbcOpen = true;
                m_dbcOpenRequested = true;
                break;
            }
            case BusFileAction::DbcExport:
                CopyToArray(m_dbc.exportFileName, chosenStr);
                break;
            default:
                break;
            }

            m_filePickerOpen = false;
            m_filePickerAction = BusFileAction::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
        {
            m_filePickerOpen = false;
            m_filePickerAction = BusFileAction::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!open)
    {
        m_filePickerOpen = false;
        m_filePickerAction = BusFileAction::None;
    }
}

} // namespace XilEnvImGui
