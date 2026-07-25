#include "Framework/BlackboardDialogs.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"

#include "Framework/ConversionDialogs.h"
#include "Framework/GuiComponents.h"

extern "C" {
#include "Blackboard.h"
#include "BlackboardAccess.h"
#include "DebugInfoDB.h"
#include "DebugInfos.h"
#include "IniDataBase.h"
#include "Scheduler.h"
#include "UniqueNumber.h"
#include "Wildcards.h"
}

namespace XilEnvImGui
{

namespace
{

constexpr int GSF_MAX_PATH{1024};             /*!< Local buffer size for process/section names. */
const char *GSF_SECTION{"BasicSettings"};     /*!< Ini section holding the global section filter. */
const char *GSF_PREFIX{"GlobalSection_"};     /*!< Ini entry prefix of the global section filter. */

/**
 * @brief Case-insensitive substring test used by the list filters.
 */
bool ContainsCaseInsensitive(const std::string &par_Haystack, const std::string &par_Needle)
{
    if (par_Needle.empty())
    {
        return true;
    }
    const auto toLower{[](unsigned char c) { return static_cast<char>(std::tolower(c)); }};
    std::string haystack{par_Haystack};
    std::string needle{par_Needle};
    for (char &c : haystack)
    {
        c = toLower(static_cast<unsigned char>(c));
    }
    for (char &c : needle)
    {
        c = toLower(static_cast<unsigned char>(c));
    }
    return (std::string::npos != haystack.find(needle));
}

/**
 * @brief Copies a std::string into a fixed char array, always NUL-terminated.
 */
template <std::size_t N>
void CopyToArray(std::array<char, N> &out_Buffer, const std::string &par_Value)
{
    std::snprintf(out_Buffer.data(), out_Buffer.size(), "%s", par_Value.c_str());
}

/**
 * @brief Splits an RGB color integer (R | G<<8 | B<<16) into 0..1 float components.
 */
void ColorIntToFloat3(int par_Rgb, std::array<float, 3> &out_Color)
{
    if (0 > par_Rgb)
    {
        out_Color = {0.0f, 0.0f, 0.0f};
        return;
    }
    out_Color[0] = static_cast<float>((par_Rgb >> 0) & 0xFF) / 255.0f;
    out_Color[1] = static_cast<float>((par_Rgb >> 8) & 0xFF) / 255.0f;
    out_Color[2] = static_cast<float>((par_Rgb >> 16) & 0xFF) / 255.0f;
}

/**
 * @brief Packs 0..1 float components back into the R | G<<8 | B<<16 integer layout.
 */
int ColorFloat3ToInt(const std::array<float, 3> &par_Color)
{
    const int r{static_cast<int>((par_Color[0] * 255.0f) + 0.5f)};
    const int g{static_cast<int>((par_Color[1] * 255.0f) + 0.5f)};
    const int b{static_cast<int>((par_Color[2] * 255.0f) + 0.5f)};
    return (r & 0xFF) | ((g & 0xFF) << 8) | ((b & 0xFF) << 16);
}

/**
 * @brief True if two doubles are bit-identically equal (mirrors the Qt change detection).
 */
bool DoubleEqual(double par_A, double par_B)
{
    const double diff{par_A - par_B};
    return (diff <= 0.0) && (diff >= 0.0);
}

/**
 * @brief True for the integer BB_DATA_TYPES that force raw-integer value entry.
 */
bool IsIntegerType(int par_DataType)
{
    return ((BB_BYTE <= par_DataType) && (BB_UDWORD >= par_DataType)) ||
           ((BB_QWORD <= par_DataType) && (BB_UQWORD >= par_DataType));
}

} // namespace

BlackboardDialogManager &BlackboardDialogManager::GetInstance()
{
    static BlackboardDialogManager instance;
    return instance;
}

// ---------------------------------------------------------------------------
//  BlackboardInfo
// ---------------------------------------------------------------------------

void BlackboardDialogManager::OpenBlackboardInfo(const char *par_VariableName)
{
    m_infoFilter[0] = '\0';
    m_pendingInfoSelect = (nullptr != par_VariableName) ? par_VariableName : "";
    m_info = InfoState{};
    m_infoSave = InfoState{};
    m_infoOpen = true;
    m_infoOpenRequested = true;
}

void BlackboardDialogManager::LoadInfoVariable(const std::string &par_Name)
{
    InfoState state{};
    state.name = par_Name;

    const VID vid{get_bbvarivid_by_name(par_Name.c_str())};
    if (0 < vid)
    {
        state.vid = vid;
        state.dataType = get_bbvaritype(vid);
        state.attachCount = get_bbvari_attachcount(vid);
        state.value = read_bbvari_convert_double(vid);
        state.minValue = get_bbvari_min(vid);
        state.maxValue = get_bbvari_max(vid);
        state.width = get_bbvari_format_width(vid);
        state.precision = get_bbvari_format_prec(vid);

        std::array<char, BBVARI_UNIT_SIZE> unit{};
        (void)get_bbvari_unit(vid, unit.data(), static_cast<int>(unit.size()));
        CopyToArray(state.unit, std::string(unit.data()));

        const int color{get_bbvari_color(vid)};
        state.colorValid = (0 <= color);
        ColorIntToFloat3(color, state.color);

        state.conversionType = get_bbvari_conversiontype(vid);
        if (0 > state.conversionType)
        {
            state.conversionType = BB_CONV_NONE;
        }
    }

    m_info = state;
    m_infoSave = state;
    RefreshInfoConversion();
}

void BlackboardDialogManager::RefreshInfoConversion()
{
    m_info.conversionType = BB_CONV_NONE;
    m_info.conversionString.clear();

    const VID vid{get_bbvarivid_by_name(m_info.name.c_str())};
    if (0 < vid)
    {
        int type{get_bbvari_conversiontype(vid)};
        if (0 > type)
        {
            type = BB_CONV_NONE;
        }
        m_info.conversionType = type;

        // get_bbvari_conversion returns the type, or a negative required length when the buffer is
        // too small (text-replace strings can be large). Grow and retry so the display matches core.
        int capacity{4096};
        std::vector<char> buffer(static_cast<std::size_t>(capacity), '\0');
        int ret{get_bbvari_conversion(vid, buffer.data(), capacity)};
        if (-1 > ret)
        {
            capacity = -ret;
            buffer.assign(static_cast<std::size_t>(capacity), '\0');
            ret = get_bbvari_conversion(vid, buffer.data(), capacity);
        }
        if (0 <= ret)
        {
            m_info.conversionString.assign(buffer.data());
        }
    }
    // Conversion is delegated to ConversionDialogManager and applied directly to the core, so it must
    // never be counted as a pending change in the simple-field diff.
    m_infoSave.conversionType = m_info.conversionType;
    m_infoSave.conversionString = m_info.conversionString;
}

void BlackboardDialogManager::ApplyInfoVariable()
{
    if (m_info.name.empty())
    {
        return;
    }
    const VID vid{get_bbvarivid_by_name(m_info.name.c_str())};
    if (0 >= vid)
    {
        return;
    }

    if (!DoubleEqual(m_info.value, m_infoSave.value))
    {
        // The variable may have no writer attached; attach briefly so the min/max-checked write lands.
        (void)attach_bbvari(vid);
        write_bbvari_minmax_check(vid, m_info.value);
        (void)remove_bbvari(vid);
    }
    if (0 != std::strcmp(m_info.unit.data(), m_infoSave.unit.data()))
    {
        (void)set_bbvari_unit(vid, m_info.unit.data());
    }
    if (!DoubleEqual(m_info.minValue, m_infoSave.minValue))
    {
        (void)set_bbvari_min(vid, m_info.minValue);
    }
    if (!DoubleEqual(m_info.maxValue, m_infoSave.maxValue))
    {
        (void)set_bbvari_max(vid, m_info.maxValue);
    }
    if ((m_info.colorValid != m_infoSave.colorValid) || (m_info.color != m_infoSave.color))
    {
        const int color{m_info.colorValid ? ColorFloat3ToInt(m_info.color) : -1};
        (void)set_bbvari_color(vid, color);
    }
    if ((m_info.width != m_infoSave.width) || (m_info.precision != m_infoSave.precision))
    {
        (void)set_bbvari_format(vid, m_info.width, m_info.precision);
    }

    m_infoSave = m_info;
}

void BlackboardDialogManager::RenderBlackboardInfo()
{
    if (m_infoOpenRequested)
    {
        ImGui::OpenPopup("Blackboard variable info###BlackboardInfo");
        m_infoOpenRequested = false;
    }
    if (!m_infoOpen)
    {
        return;
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(720.0f, 560.0f), ImGuiCond_Appearing);

    bool open{true};
    if (ImGui::BeginPopupModal("Blackboard variable info###BlackboardInfo", &open,
                               ImGuiWindowFlags_NoSavedSettings))
    {
        // Refresh the (delegated) conversion display so ConversionDialogManager edits round-trip.
        if (!m_info.name.empty())
        {
            RefreshInfoConversion();
        }

        // -- Left: filtered variable list --
        ImGui::BeginChild("##bbinfo_list", ImVec2(240.0f, -ImGui::GetFrameHeightWithSpacing() - 8.0f),
                          true);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##bbinfo_filter", "filter...", m_infoFilter.data(),
                                 m_infoFilter.size());

        std::vector<std::string> names;
        EnumerateBlackboardVariables(names);
        const std::string filter{m_infoFilter.data()};

        if ((!m_pendingInfoSelect.empty()) && m_info.name.empty())
        {
            LoadInfoVariable(m_pendingInfoSelect);
            m_pendingInfoSelect.clear();
        }
        else if (m_pendingInfoSelect.empty() && m_info.name.empty() && !names.empty())
        {
            for (const std::string &name : names)
            {
                if (ContainsCaseInsensitive(name, filter))
                {
                    LoadInfoVariable(name);
                    break;
                }
            }
        }

        for (const std::string &name : names)
        {
            if (!ContainsCaseInsensitive(name, filter))
            {
                continue;
            }
            const bool selected{name == m_info.name};
            if (ImGui::Selectable(name.c_str(), selected))
            {
                if (name != m_info.name)
                {
                    ApplyInfoVariable();
                    LoadInfoVariable(name);
                }
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // -- Right: variable fields --
        ImGui::BeginChild("##bbinfo_fields", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() - 8.0f));
        if (m_info.name.empty())
        {
            ImGui::TextUnformatted("No variable selected.");
        }
        else
        {
            char idBuffer[32];
            std::snprintf(idBuffer, sizeof(idBuffer), "0x%X", m_info.vid);
            ImGui::Text("Variable id: %s", idBuffer);
            ImGui::Text("Data type: %s", GetDataTypeName(m_info.dataType));
            ImGui::Text("Attach counter: %d", m_info.attachCount);
            ImGui::Separator();

            ImGui::SetNextItemWidth(-FLT_MIN);
            DrawPhysValueInput("Value##bbinfo", &m_info.value, 0.0, 0.0, m_info.precision);

            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("Unit##bbinfo", m_info.unit.data(), m_info.unit.size());

            ImGui::SetNextItemWidth(-FLT_MIN);
            DrawPhysValueInput("Minimum##bbinfo", &m_info.minValue, 0.0, 0.0, m_info.precision);
            ImGui::SetNextItemWidth(-FLT_MIN);
            DrawPhysValueInput("Maximum##bbinfo", &m_info.maxValue, 0.0, 0.0, m_info.precision);

            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputInt("Width##bbinfo", &m_info.width);
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputInt("Precision##bbinfo", &m_info.precision);

            ImGui::Separator();
            ImGui::Checkbox("Color set##bbinfo", &m_info.colorValid);
            if (m_info.colorValid)
            {
                if (ImGui::ColorEdit3("Color##bbinfo", m_info.color.data()))
                {
                    m_info.colorValid = true;
                }
            }

            ImGui::Separator();
            // Conversion is read-only here; editing is fully delegated to ConversionDialogManager.
            static const std::array<const char *, 9> convLabels{{"none", "formula", "text replace",
                                                                 "# * factor + offset",
                                                                 "factor * (# + offset)",
                                                                 "table with interpolation",
                                                                 "table without interpolation",
                                                                 "fractional rational function",
                                                                 "reference"}};
            const int convIndex{((0 <= m_info.conversionType) &&
                                 (m_info.conversionType < static_cast<int>(convLabels.size())))
                                    ? m_info.conversionType
                                    : 0};
            ImGui::Text("Conversion type: %s", convLabels[static_cast<std::size_t>(convIndex)]);
            ImGui::TextWrapped("Conversion: %s", m_info.conversionString.empty()
                                                     ? "<none>"
                                                     : m_info.conversionString.c_str());
            if (ImGui::Button("Edit conversion...##bbinfo"))
            {
                ConversionDialogManager::GetInstance().OpenForVariable(m_info.name.c_str());
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (ImGui::Button("Apply", ImVec2(100.0f, 0.0f)))
        {
            ApplyInfoVariable();
        }
        ImGui::SameLine();
        if (ImGui::Button("OK", ImVec2(100.0f, 0.0f)))
        {
            ApplyInfoVariable();
            m_infoOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
        {
            m_infoOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (!open)
    {
        m_infoOpen = false;
    }
}

// ---------------------------------------------------------------------------
//  BlackboardAccess
// ---------------------------------------------------------------------------

void BlackboardDialogManager::OpenBlackboardAccess()
{
    m_accessProcessNames.clear();
    m_accessData.clear();
    m_accessSelected.clear();
    m_accessEnableSel.clear();
    m_accessDisableSel.clear();
    m_accessEnableFilter[0] = '\0';
    m_accessDisableFilter[0] = '\0';

    READ_NEXT_PROCESS_NAME *buffer{init_read_next_process_name(2 | 0x100)};
    char *name{nullptr};
    while (nullptr != (name = read_next_process_name(buffer)))
    {
        m_accessProcessNames.emplace_back(name);
    }
    close_read_next_process_name(buffer);

    m_accessOpen = true;
    m_accessOpenRequested = true;
}

void BlackboardDialogManager::LoadAccessProcess(const std::string &par_ProcessName)
{
    m_accessSelected = par_ProcessName;
    m_accessEnableSel.clear();
    m_accessDisableSel.clear();

    for (AccessProcess &entry : m_accessData)
    {
        if (entry.loaded && (get_pid_by_name(par_ProcessName.c_str()) == entry.pid))
        {
            return;
        }
    }

    AccessProcess entry{};
    entry.pid = get_pid_by_name(par_ProcessName.c_str());
    if (0 < entry.pid)
    {
        char varName[BBVARI_NAME_SIZE];
        int index{0};
        while (0 < (index = ReadNextVariableProcessAccess(index, entry.pid, 1, varName, sizeof(varName))))
        {
            entry.enable.emplace_back(varName);
        }
        index = 0;
        while (0 < (index = ReadNextVariableProcessAccess(index, entry.pid, 2, varName, sizeof(varName))))
        {
            entry.disable.emplace_back(varName);
        }
        entry.enableSave = entry.enable;
        entry.disableSave = entry.disable;
        entry.loaded = true;
        m_accessData.push_back(std::move(entry));
    }
}

void BlackboardDialogManager::ApplyAccess()
{
    for (const AccessProcess &entry : m_accessData)
    {
        for (const std::string &variable : entry.enable)
        {
            const bool wasNotEnabled{std::find(entry.enableSave.begin(), entry.enableSave.end(),
                                               variable) == entry.enableSave.end()};
            const bool notNowDisabled{std::find(entry.disable.begin(), entry.disable.end(),
                                                variable) == entry.disable.end()};
            const bool wasDisabled{std::find(entry.disableSave.begin(), entry.disableSave.end(),
                                             variable) != entry.disableSave.end()};
            if (wasNotEnabled && notNowDisabled && wasDisabled)
            {
                (void)enable_bbvari_access(entry.pid, get_bbvarivid_by_name(variable.c_str()));
            }
        }
        for (const std::string &variable : entry.disable)
        {
            const bool wasNotDisabled{std::find(entry.disableSave.begin(), entry.disableSave.end(),
                                                variable) == entry.disableSave.end()};
            const bool notNowEnabled{std::find(entry.enable.begin(), entry.enable.end(), variable) ==
                                     entry.enable.end()};
            const bool wasEnabled{std::find(entry.enableSave.begin(), entry.enableSave.end(),
                                            variable) != entry.enableSave.end()};
            if (wasNotDisabled && notNowEnabled && wasEnabled)
            {
                (void)disable_bbvari_access(entry.pid, get_bbvarivid_by_name(variable.c_str()));
            }
        }
    }
}

void BlackboardDialogManager::RenderBlackboardAccess()
{
    if (m_accessOpenRequested)
    {
        ImGui::OpenPopup("Blackboard access###BlackboardAccess");
        m_accessOpenRequested = false;
    }
    if (!m_accessOpen)
    {
        return;
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(760.0f, 560.0f), ImGuiCond_Appearing);

    bool open{true};
    if (ImGui::BeginPopupModal("Blackboard access###BlackboardAccess", &open,
                               ImGuiWindowFlags_NoSavedSettings))
    {
        const float bodyHeight{-ImGui::GetFrameHeightWithSpacing() - 8.0f};

        // -- Process list --
        ImGui::BeginChild("##acc_procs", ImVec2(180.0f, bodyHeight), true);
        ImGui::TextUnformatted("Processes");
        ImGui::Separator();
        for (const std::string &proc : m_accessProcessNames)
        {
            if (ImGui::Selectable(proc.c_str(), proc == m_accessSelected))
            {
                LoadAccessProcess(proc);
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        AccessProcess *current{nullptr};
        if (!m_accessSelected.empty())
        {
            const PID pid{get_pid_by_name(m_accessSelected.c_str())};
            for (AccessProcess &entry : m_accessData)
            {
                if (entry.pid == pid)
                {
                    current = &entry;
                    break;
                }
            }
        }

        const float listWidth{260.0f};

        // -- Enabled list --
        ImGui::BeginChild("##acc_enable", ImVec2(listWidth, bodyHeight), true);
        ImGui::TextUnformatted("Enabled (write access)");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##acc_enable_filter", "filter...", m_accessEnableFilter.data(),
                                 m_accessEnableFilter.size());
        if (nullptr != current)
        {
            const std::string filter{m_accessEnableFilter.data()};
            for (int i{0}; i < static_cast<int>(current->enable.size()); ++i)
            {
                if (!ContainsCaseInsensitive(current->enable[static_cast<std::size_t>(i)], filter))
                {
                    continue;
                }
                const bool sel{std::find(m_accessEnableSel.begin(), m_accessEnableSel.end(), i) !=
                               m_accessEnableSel.end()};
                if (ImGui::Selectable(current->enable[static_cast<std::size_t>(i)].c_str(), sel))
                {
                    if (sel)
                    {
                        m_accessEnableSel.erase(std::remove(m_accessEnableSel.begin(),
                                                            m_accessEnableSel.end(), i),
                                                m_accessEnableSel.end());
                    }
                    else
                    {
                        m_accessEnableSel.push_back(i);
                    }
                }
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // -- Move buttons --
        ImGui::BeginChild("##acc_buttons", ImVec2(60.0f, bodyHeight));
        ImGui::Dummy(ImVec2(0.0f, 40.0f));
        if (ImGui::Button("->", ImVec2(-FLT_MIN, 0.0f)) && (nullptr != current))
        {
            std::vector<std::string> move;
            for (const int i : m_accessEnableSel)
            {
                if ((0 <= i) && (i < static_cast<int>(current->enable.size())))
                {
                    move.push_back(current->enable[static_cast<std::size_t>(i)]);
                }
            }
            for (const std::string &name : move)
            {
                current->enable.erase(std::remove(current->enable.begin(), current->enable.end(),
                                                  name),
                                      current->enable.end());
                current->disable.push_back(name);
            }
            m_accessEnableSel.clear();
        }
        if (ImGui::Button("<-", ImVec2(-FLT_MIN, 0.0f)) && (nullptr != current))
        {
            std::vector<std::string> move;
            for (const int i : m_accessDisableSel)
            {
                if ((0 <= i) && (i < static_cast<int>(current->disable.size())))
                {
                    move.push_back(current->disable[static_cast<std::size_t>(i)]);
                }
            }
            for (const std::string &name : move)
            {
                current->disable.erase(std::remove(current->disable.begin(), current->disable.end(),
                                                   name),
                                       current->disable.end());
                current->enable.push_back(name);
            }
            m_accessDisableSel.clear();
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // -- Disabled list --
        ImGui::BeginChild("##acc_disable", ImVec2(listWidth, bodyHeight), true);
        ImGui::TextUnformatted("Disabled");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##acc_disable_filter", "filter...", m_accessDisableFilter.data(),
                                 m_accessDisableFilter.size());
        if (nullptr != current)
        {
            const std::string filter{m_accessDisableFilter.data()};
            for (int i{0}; i < static_cast<int>(current->disable.size()); ++i)
            {
                if (!ContainsCaseInsensitive(current->disable[static_cast<std::size_t>(i)], filter))
                {
                    continue;
                }
                const bool sel{std::find(m_accessDisableSel.begin(), m_accessDisableSel.end(), i) !=
                               m_accessDisableSel.end()};
                if (ImGui::Selectable(current->disable[static_cast<std::size_t>(i)].c_str(), sel))
                {
                    if (sel)
                    {
                        m_accessDisableSel.erase(std::remove(m_accessDisableSel.begin(),
                                                             m_accessDisableSel.end(), i),
                                                 m_accessDisableSel.end());
                    }
                    else
                    {
                        m_accessDisableSel.push_back(i);
                    }
                }
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(100.0f, 0.0f)))
        {
            ApplyAccess();
            m_accessOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
        {
            m_accessOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (!open)
    {
        m_accessOpen = false;
    }
}

// ---------------------------------------------------------------------------
//  BlackboardInternal (read-only inspector)
// ---------------------------------------------------------------------------

void BlackboardDialogManager::OpenBlackboardInternal()
{
    m_internalFilter[0] = '\0';
    m_internalOnlyUsed = false;
    m_internalSelected.clear();
    m_internalOpen = true;
}

void BlackboardDialogManager::RenderBlackboardInternal()
{
    if (!m_internalOpen)
    {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(820.0f, 620.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Blackboard internal###BlackboardInternal", &m_internalOpen))
    {
        ImGui::End();
        return;
    }

    ImGui::Checkbox("Only used variables", &m_internalOnlyUsed);
    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputTextWithHint("##internal_filter", "filter...", m_internalFilter.data(),
                             m_internalFilter.size());
    const std::string filter{m_internalFilter.data()};

    ImGui::BeginChild("##internal_list", ImVec2(240.0f, 0.0f), true);
    for (int i{0}; i < blackboard_infos.Size; ++i)
    {
        if (0 >= blackboard[i].Vid)
        {
            continue;
        }
        if (m_internalOnlyUsed && (BB_UNKNOWN_WAIT == blackboard[i].Type))
        {
            continue;
        }
        if ((nullptr == blackboard[i].pAdditionalInfos) ||
            (nullptr == blackboard[i].pAdditionalInfos->Name))
        {
            continue;
        }
        const std::string name{blackboard[i].pAdditionalInfos->Name};
        if (!ContainsCaseInsensitive(name, filter))
        {
            continue;
        }
        if (ImGui::Selectable(name.c_str(), name == m_internalSelected))
        {
            m_internalSelected = name;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##internal_detail", ImVec2(0.0f, 0.0f));
    if (!m_internalSelected.empty())
    {
        const VID vid{get_bbvarivid_by_name(m_internalSelected.c_str())};
        int index{-1};
        for (int i{0}; i < blackboard_infos.Size; ++i)
        {
            if ((blackboard[i].Vid == vid) && (0 < vid))
            {
                index = i;
                break;
            }
        }
        if (0 <= index)
        {
            const BB_VARIABLE &var{blackboard[index]};
            const BB_VARIABLE_ADDITIONAL_INFOS *info{var.pAdditionalInfos};

            ImGui::Text("Name: %s", (nullptr != info && nullptr != info->Name) ? info->Name : "NULL");
            ImGui::Text("Display name: %s",
                        (nullptr != info && nullptr != info->DisplayName) ? info->DisplayName : "NULL");
            ImGui::Text("Value: %g", read_bbvari_convert_double(var.Vid));
            ImGui::Text("Vid: 0x%X", var.Vid);
            ImGui::Text("Data type: %s", GetDataTypeName(var.Type));
            ImGui::Text("Unit: %s", (nullptr != info && nullptr != info->Unit) ? info->Unit : "NULL");
            ImGui::Text("Comment: %s",
                        (nullptr != info && nullptr != info->Comment) ? info->Comment : "NULL");
            if (nullptr != info)
            {
                ImGui::Separator();
                ImGui::Text("Min: %g   Max: %g", info->Min, info->Max);
                ImGui::Text("Prec: %d   Width: %d", info->Prec, info->Width);
                ImGui::Text("Step type: %d   Step: %g", info->StepType, info->Step);
                ImGui::Text("Color: 0x%X", info->RgbColor);
                ImGui::Text("Conversion type: %d", info->Conversion.Type);
                if (BB_CONV_FORMULA == info->Conversion.Type)
                {
                    ImGui::TextWrapped("Conversion: %s",
                                       (nullptr != info->Conversion.Conv.Formula.FormulaString)
                                           ? info->Conversion.Conv.Formula.FormulaString
                                           : "");
                }
                else if (BB_CONV_TEXTREP == info->Conversion.Type)
                {
                    ImGui::TextWrapped("Conversion: %s",
                                       (nullptr != info->Conversion.Conv.TextReplace.EnumString)
                                           ? info->Conversion.Conv.TextReplace.EnumString
                                           : "");
                }
                ImGui::Separator();
                ImGui::Text("Attach counter: %u", info->AttachCount);
                ImGui::Text("Unknown-wait attach counter: %u", info->UnknownWaitAttachCount);

                if (ImGui::BeginTable("##internal_procs", 7,
                                      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_ScrollY,
                                      ImVec2(0.0f, 220.0f)))
                {
                    ImGui::TableSetupColumn("Process");
                    ImGui::TableSetupColumn("Pid");
                    ImGui::TableSetupColumn("Attach");
                    ImGui::TableSetupColumn("UW attach");
                    ImGui::TableSetupColumn("Access");
                    ImGui::TableSetupColumn("Wr");
                    ImGui::TableSetupColumn("Range");
                    ImGui::TableHeadersRow();
                    for (int x{0}; x < MAX_PROCESSES; ++x)
                    {
                        char procName[GSF_MAX_PATH];
                        const bool named{0 == GetProcessShortName(blackboard_infos.pid_access_masks[x],
                                                                  procName, sizeof(procName))};
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(named ? procName : "unused");
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%d", blackboard_infos.pid_access_masks[x]);
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%u", info->ProcessAttachCounts[x]);
                        ImGui::TableSetColumnIndex(3);
                        ImGui::Text("%u", info->ProcessUnknownWaitAttachCounts[x]);
                        ImGui::TableSetColumnIndex(4);
                        ImGui::TextUnformatted((0 != (var.AccessFlags & (1ULL << x))) ? "X" : "-");
                        ImGui::TableSetColumnIndex(5);
                        ImGui::TextUnformatted((0 != (var.WrFlags & (1ULL << x))) ? "X" : "-");
                        ImGui::TableSetColumnIndex(6);
                        ImGui::TextUnformatted((0 != (var.RangeControlFlag & (1ULL << x))) ? "X" : "-");
                    }
                    ImGui::EndTable();
                }
            }
        }
    }
    else
    {
        ImGui::TextUnformatted("No variable selected.");
    }
    ImGui::EndChild();

    ImGui::End();
}

// ---------------------------------------------------------------------------
//  GlobalSectionFilter
// ---------------------------------------------------------------------------

void BlackboardDialogManager::OpenGlobalSectionFilter()
{
    LoadGlobalSectionFilter();
    m_gsfAvailableSel = -1;
    m_gsfIncludeSel = -1;
    m_gsfExcludeSel = -1;
    m_gsfOpen = true;
    m_gsfOpenRequested = true;
}

void BlackboardDialogManager::AddSectionsForProcess(const std::string &par_ProcessName,
                                                    int par_UniqueId)
{
    int pid{0};
    PROCESS_APPL_DATA *pappldata{ConnectToProcessDebugInfos(par_UniqueId, par_ProcessName.c_str(),
                                                            &pid, nullptr, nullptr, nullptr, 1)};
    if (nullptr == pappldata)
    {
        return;
    }
    DEBUG_INFOS_ASSOCIATED_PROCESSE *associated{pappldata->AssociatedProcess};
    if ((nullptr != associated) && (nullptr != associated->AssociatedDebugInfos))
    {
        DEBUG_INFOS_DATA *debugInfos{associated->AssociatedDebugInfos};
        if (nullptr != debugInfos->SectionNames)
        {
            for (int x{0}; x < debugInfos->NumOfSections; ++x)
            {
                if ((nullptr != debugInfos->SectionNames[x]) &&
                    (0 != std::strlen(debugInfos->SectionNames[x])))
                {
                    const std::string section{debugInfos->SectionNames[x]};
                    if (std::find(m_gsfAvailable.begin(), m_gsfAvailable.end(), section) ==
                        m_gsfAvailable.end())
                    {
                        m_gsfAvailable.push_back(section);
                    }
                }
            }
        }
    }
    (void)RemoveConnectFromProcessDebugInfos(par_UniqueId);
}

void BlackboardDialogManager::LoadGlobalSectionFilter()
{
    m_gsfProcesses.clear();
    m_gsfAvailable.clear();
    m_gsfInclude.clear();
    m_gsfExclude.clear();

    READ_NEXT_PROCESS_NAME *buffer{init_read_next_process_name(2 | 0x100)};
    char *name{nullptr};
    while (nullptr != (name = read_next_process_name(buffer)))
    {
        if (!IsInternalProcess(name))
        {
            m_gsfProcesses.emplace_back(name);
        }
    }
    close_read_next_process_name(buffer);

    const int uniqueId{AquireUniqueNumber()};
    for (const std::string &proc : m_gsfProcesses)
    {
        AddSectionsForProcess(proc, uniqueId);
    }
    FreeUniqueNumber(uniqueId);

    INCLUDE_EXCLUDE_FILTER *filter{
        BuildIncludeExcludeFilterFromIni(GSF_SECTION, GSF_PREFIX, GetMainFileDescriptor())};
    if (nullptr != filter)
    {
        for (int i{0}; i < filter->IncludeSize; ++i)
        {
            m_gsfInclude.emplace_back(filter->IncludeArray[i]);
        }
        for (int e{0}; e < filter->ExcludeSize; ++e)
        {
            m_gsfExclude.emplace_back(filter->ExcludeArray[e]);
        }
        FreeIncludeExcludeFilter(filter);
    }
}

void BlackboardDialogManager::ApplyGlobalSectionFilter()
{
    INCLUDE_EXCLUDE_FILTER filter{};
    std::vector<char *> includePtrs;
    std::vector<char *> excludePtrs;
    includePtrs.reserve(m_gsfInclude.size());
    excludePtrs.reserve(m_gsfExclude.size());
    for (std::string &s : m_gsfInclude)
    {
        includePtrs.push_back(const_cast<char *>(s.c_str()));
    }
    for (std::string &s : m_gsfExclude)
    {
        excludePtrs.push_back(const_cast<char *>(s.c_str()));
    }
    filter.IncludeSize = static_cast<int>(m_gsfInclude.size());
    filter.ExcludeSize = static_cast<int>(m_gsfExclude.size());
    filter.IncludeArray = includePtrs.empty() ? nullptr : includePtrs.data();
    filter.ExcludeArray = excludePtrs.empty() ? nullptr : excludePtrs.data();

    (void)SaveIncludeExcludeListsToIni(&filter, GSF_SECTION, GSF_PREFIX, GetMainFileDescriptor());
}

void BlackboardDialogManager::RenderGlobalSectionFilter()
{
    if (m_gsfOpenRequested)
    {
        ImGui::OpenPopup("Global section filter###GlobalSectionFilter");
        m_gsfOpenRequested = false;
    }
    if (!m_gsfOpen)
    {
        return;
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(760.0f, 520.0f), ImGuiCond_Appearing);

    bool open{true};
    if (ImGui::BeginPopupModal("Global section filter###GlobalSectionFilter", &open,
                               ImGuiWindowFlags_NoSavedSettings))
    {
        const float bodyHeight{-ImGui::GetFrameHeightWithSpacing() - 8.0f};
        const float colWidth{230.0f};

        // -- Available sections --
        ImGui::BeginChild("##gsf_avail", ImVec2(colWidth, bodyHeight), true);
        ImGui::TextUnformatted("Available sections");
        ImGui::Separator();
        for (int i{0}; i < static_cast<int>(m_gsfAvailable.size()); ++i)
        {
            if (ImGui::Selectable(m_gsfAvailable[static_cast<std::size_t>(i)].c_str(),
                                  i == m_gsfAvailableSel))
            {
                m_gsfAvailableSel = i;
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // -- Include list --
        ImGui::BeginChild("##gsf_include", ImVec2(colWidth, bodyHeight), true);
        ImGui::TextUnformatted("Include");
        if (ImGui::SmallButton("Add##gsf_inc_add") && (0 <= m_gsfAvailableSel) &&
            (m_gsfAvailableSel < static_cast<int>(m_gsfAvailable.size())))
        {
            const std::string &section{m_gsfAvailable[static_cast<std::size_t>(m_gsfAvailableSel)]};
            if (std::find(m_gsfInclude.begin(), m_gsfInclude.end(), section) == m_gsfInclude.end())
            {
                m_gsfInclude.push_back(section);
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("New...##gsf_inc_new"))
        {
            OpenAddFilter(false, [this](const std::string &name) { m_gsfInclude.push_back(name); });
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Del##gsf_inc_del") && (0 <= m_gsfIncludeSel) &&
            (m_gsfIncludeSel < static_cast<int>(m_gsfInclude.size())))
        {
            m_gsfInclude.erase(m_gsfInclude.begin() + m_gsfIncludeSel);
            m_gsfIncludeSel = -1;
        }
        ImGui::Separator();
        for (int i{0}; i < static_cast<int>(m_gsfInclude.size()); ++i)
        {
            if (ImGui::Selectable(m_gsfInclude[static_cast<std::size_t>(i)].c_str(),
                                  i == m_gsfIncludeSel))
            {
                m_gsfIncludeSel = i;
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // -- Exclude list --
        ImGui::BeginChild("##gsf_exclude", ImVec2(colWidth, bodyHeight), true);
        ImGui::TextUnformatted("Exclude");
        if (ImGui::SmallButton("Add##gsf_exc_add") && (0 <= m_gsfAvailableSel) &&
            (m_gsfAvailableSel < static_cast<int>(m_gsfAvailable.size())))
        {
            const std::string &section{m_gsfAvailable[static_cast<std::size_t>(m_gsfAvailableSel)]};
            if (std::find(m_gsfExclude.begin(), m_gsfExclude.end(), section) == m_gsfExclude.end())
            {
                m_gsfExclude.push_back(section);
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("New...##gsf_exc_new"))
        {
            OpenAddFilter(true, [this](const std::string &name) { m_gsfExclude.push_back(name); });
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Del##gsf_exc_del") && (0 <= m_gsfExcludeSel) &&
            (m_gsfExcludeSel < static_cast<int>(m_gsfExclude.size())))
        {
            m_gsfExclude.erase(m_gsfExclude.begin() + m_gsfExcludeSel);
            m_gsfExcludeSel = -1;
        }
        ImGui::Separator();
        for (int i{0}; i < static_cast<int>(m_gsfExclude.size()); ++i)
        {
            if (ImGui::Selectable(m_gsfExclude[static_cast<std::size_t>(i)].c_str(),
                                  i == m_gsfExcludeSel))
            {
                m_gsfExcludeSel = i;
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(100.0f, 0.0f)))
        {
            ApplyGlobalSectionFilter();
            m_gsfOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
        {
            m_gsfOpen = false;
            ImGui::CloseCurrentPopup();
        }

        // The add-filter helper prompt renders on top of this modal.
        RenderAddFilter();

        ImGui::EndPopup();
    }

    if (!open)
    {
        m_gsfOpen = false;
    }
}

// ---------------------------------------------------------------------------
//  AddFilter (helper prompt)
// ---------------------------------------------------------------------------

void BlackboardDialogManager::OpenAddFilter(bool par_IncludeExclude, AddFilterCallback par_Callback)
{
    m_addFilterName[0] = '\0';
    m_addFilterExclude = par_IncludeExclude;
    m_addFilterCallback = std::move(par_Callback);
    m_addFilterOpen = true;
    m_addFilterOpenRequested = true;
}

void BlackboardDialogManager::RenderAddFilter()
{
    if (m_addFilterOpenRequested)
    {
        ImGui::OpenPopup("Add filter###AddFilter");
        m_addFilterOpenRequested = false;
    }
    if (!m_addFilterOpen)
    {
        return;
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    bool open{true};
    if (ImGui::BeginPopupModal("Add filter###AddFilter", &open,
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(m_addFilterExclude ? "New exclude filter:" : "New include filter:");
        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(320.0f);
        const bool entered{ImGui::InputText("##addfilter_name", m_addFilterName.data(),
                                            m_addFilterName.size(),
                                            ImGuiInputTextFlags_EnterReturnsTrue)};

        ImGui::Separator();
        const bool okClicked{ImGui::Button("OK", ImVec2(100.0f, 0.0f))};
        if (okClicked || entered)
        {
            const std::string value{m_addFilterName.data()};
            if (!value.empty())
            {
                if (m_addFilterCallback)
                {
                    m_addFilterCallback(value);
                }
                m_addFilterOpen = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
        {
            m_addFilterOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (!open)
    {
        m_addFilterOpen = false;
    }
}

// ---------------------------------------------------------------------------
//  ChangeValue (batch value operation)
// ---------------------------------------------------------------------------

void BlackboardDialogManager::OpenChangeValue(int par_Op, const char *par_Title, int par_DataType,
                                              int par_Precision, double par_InitialValue,
                                              ChangeValueCallback par_Callback)
{
    m_changeValueOp = par_Op;
    m_changeValueTitle = (nullptr != par_Title) ? par_Title : "Change value";
    m_changeValue = par_InitialValue;
    m_changeValuePrecision = par_Precision;
    m_changeValueRawInt = IsIntegerType(par_DataType);
    m_changeValueCallback = std::move(par_Callback);
    m_changeValueOpen = true;
    m_changeValueOpenRequested = true;
}

void BlackboardDialogManager::RenderChangeValue()
{
    if (m_changeValueOpenRequested)
    {
        ImGui::OpenPopup("###ChangeValue");
        m_changeValueOpenRequested = false;
    }
    if (!m_changeValueOpen)
    {
        return;
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    const std::string title{m_changeValueTitle + "###ChangeValue"};
    bool open{true};
    if (ImGui::BeginPopupModal(title.c_str(), &open,
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(m_changeValueTitle.c_str());
        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
        }
        char format[16];
        std::snprintf(format, sizeof(format), "%%.%df", m_changeValueRawInt ? 0 : m_changeValuePrecision);
        ImGui::SetNextItemWidth(240.0f);
        const bool entered{ImGui::InputDouble("##changevalue_val", &m_changeValue, 0.0, 0.0, format,
                                              ImGuiInputTextFlags_EnterReturnsTrue)};

        ImGui::Separator();
        const bool okClicked{ImGui::Button("OK", ImVec2(100.0f, 0.0f))};
        if (okClicked || entered)
        {
            if (m_changeValueCallback)
            {
                m_changeValueCallback(m_changeValueOp, m_changeValue);
            }
            m_changeValueOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
        {
            m_changeValueOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (!open)
    {
        m_changeValueOpen = false;
    }
}

// ---------------------------------------------------------------------------
//  Render dispatch
// ---------------------------------------------------------------------------

void BlackboardDialogManager::Render()
{
    RenderBlackboardInfo();
    RenderBlackboardAccess();
    RenderBlackboardInternal();
    RenderGlobalSectionFilter();
    // When the global-section-filter modal is open it renders the add-filter prompt nested inside its
    // own popup so the two modals stack correctly; only render it here for standalone callers.
    if (!m_gsfOpen)
    {
        RenderAddFilter();
    }
    RenderChangeValue();
}

} // namespace XilEnvImGui
