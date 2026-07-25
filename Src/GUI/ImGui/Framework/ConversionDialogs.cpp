#include "Framework/ConversionDialogs.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"

#include "Framework/GuiComponents.h"

extern "C" {
#include "Blackboard.h"
#include "BlackboardAccess.h"
#include "TextReplace.h"
}

namespace XilEnvImGui
{

namespace
{

constexpr int RAW_BUFFER_MIN_SIZE{4096};       /*!< Minimum editable raw-string buffer size. */
constexpr int TEXTREP_ENUM_TEXT_MAXC{1024};    /*!< Enum-text parse buffer, mirrors the Qt dialog. */

/**
 * @brief The nine conversion-type combo labels indexed by BB_CONV_* value (0..8).
 */
const std::array<const char *, 9> CONVERSION_TYPE_LABELS{{
    "none",                            // BB_CONV_NONE
    "formula",                         // BB_CONV_FORMULA
    "text replace",                    // BB_CONV_TEXTREP
    "# * factor + offset",             // BB_CONV_FACTOFF
    "factor * (# + offset)",           // BB_CONV_OFFFACT
    "table with interpolation",        // BB_CONV_TAB_INTP
    "table without interpolation",     // BB_CONV_TAB_NOINTP
    "fractional rational function",    // BB_CONV_RAT_FUNC
    "reference"}};                     // BB_CONV_REF (not implemented)

/**
 * @brief Returns true if the conversion type has a dedicated sub-editor.
 */
bool HasSubEditor(int par_Type)
{
    return (BB_CONV_TEXTREP == par_Type) || (BB_CONV_FACTOFF == par_Type) ||
           (BB_CONV_OFFFACT == par_Type) || (BB_CONV_RAT_FUNC == par_Type) ||
           (BB_CONV_TAB_INTP == par_Type) || (BB_CONV_TAB_NOINTP == par_Type);
}

/**
 * @brief Formats a double the way the C core stores conversion coefficients ("%.18g").
 */
std::string FormatCoefficient(double par_Value)
{
    std::array<char, 64> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.18g", par_Value);
    return std::string(buffer.data());
}

} // namespace

ConversionDialogManager &ConversionDialogManager::GetInstance()
{
    static ConversionDialogManager instance;
    return instance;
}

void ConversionDialogManager::SetRawBuffer(const std::string &par_Value)
{
    const int needed{static_cast<int>(par_Value.size()) + 1};
    const int size{(needed > RAW_BUFFER_MIN_SIZE) ? needed : RAW_BUFFER_MIN_SIZE};
    m_rawBuffer.assign(static_cast<std::size_t>(size), '\0');
    std::memcpy(m_rawBuffer.data(), par_Value.data(), par_Value.size());
}

void ConversionDialogManager::ReadConversionFromVariable()
{
    m_conversionType = BB_CONV_NONE;
    std::string conversion{};

    const VID vid{get_bbvarivid_by_name(m_variableName.c_str())};
    if (0 < vid)
    {
        m_conversionType = get_bbvari_conversiontype(vid);
        if (0 > m_conversionType)
        {
            m_conversionType = BB_CONV_NONE;
        }
        // get_bbvari_conversion returns the type or a negative required length if the buffer is
        // too small (BB_CONV_TEXTREP strings can be large). Grow and retry.
        int capacity{RAW_BUFFER_MIN_SIZE};
        std::vector<char> buffer(static_cast<std::size_t>(capacity), '\0');
        int ret{get_bbvari_conversion(vid, buffer.data(), capacity)};
        if ((-1 > ret))
        {
            capacity = -ret;
            buffer.assign(static_cast<std::size_t>(capacity), '\0');
            ret = get_bbvari_conversion(vid, buffer.data(), capacity);
        }
        if (0 <= ret)
        {
            conversion.assign(buffer.data());
        }
    }
    SetRawBuffer(conversion);
}

void ConversionDialogManager::ApplyConversionToVariable()
{
    const VID vid{get_bbvarivid_by_name(m_variableName.c_str())};
    if (0 < vid)
    {
        (void)set_bbvari_conversion(vid, m_conversionType, m_rawBuffer.data());
    }
}

void ConversionDialogManager::OpenForVariable(const char *par_VariableName)
{
    if (nullptr == par_VariableName)
    {
        return;
    }
    const VID vid{get_bbvarivid_by_name(par_VariableName)};
    if (0 >= vid)
    {
        return;
    }
    m_variableName = par_VariableName;
    m_error.clear();
    ReadConversionFromVariable();
    m_dispatcherOpen = true;
    m_dispatcherOpenRequested = true;
}

void ConversionDialogManager::OpenPicker()
{
    m_pickerName[0] = '\0';
    m_pickerOpen = true;
    m_pickerOpenRequested = true;
}

void ConversionDialogManager::Render()
{
    RenderPicker();
    RenderDispatcher();
}

void ConversionDialogManager::RenderPicker()
{
    if (m_pickerOpenRequested)
    {
        ImGui::OpenPopup("Configure signal conversion###ConvPicker");
        m_pickerOpenRequested = false;
    }
    if (!m_pickerOpen)
    {
        return;
    }

    const ImGuiViewport *viewport{ImGui::GetMainViewport()};
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Appearing);

    bool open{true};
    if (ImGui::BeginPopupModal("Configure signal conversion###ConvPicker", &open,
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Select the blackboard variable to configure:");
        DrawBlackboardSelector("##ConvPickerVar", m_pickerName.data(), m_pickerName.size());

        ImGui::Separator();
        const bool valid{(0 < get_bbvarivid_by_name(m_pickerName.data()))};
        if (!valid)
        {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Configure...", ImVec2(120.0f, 0.0f)))
        {
            const std::string name{m_pickerName.data()};
            m_pickerOpen = false;
            ImGui::CloseCurrentPopup();
            OpenForVariable(name.c_str());
        }
        if (!valid)
        {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
        {
            m_pickerOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (!open)
    {
        m_pickerOpen = false;
    }
}

void ConversionDialogManager::RenderDispatcher()
{
    if (m_dispatcherOpenRequested)
    {
        ImGui::OpenPopup("Signal conversion###ConvDispatcher");
        m_dispatcherOpenRequested = false;
    }
    if (!m_dispatcherOpen)
    {
        return;
    }

    const ImGuiViewport *viewport{ImGui::GetMainViewport()};
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(600.0f, 0.0f), ImGuiCond_Appearing);

    bool open{true};
    if (ImGui::BeginPopupModal("Signal conversion###ConvDispatcher", &open,
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Variable: %s", m_variableName.c_str());
        ImGui::Separator();

        if (ImGui::BeginCombo("Conversion type",
                              CONVERSION_TYPE_LABELS[static_cast<std::size_t>(m_conversionType)]))
        {
            for (int t{0}; t < static_cast<int>(CONVERSION_TYPE_LABELS.size()); ++t)
            {
                // References are not implemented (mirrors the disabled entry in the Qt dialog).
                if (BB_CONV_REF == t)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Selectable(CONVERSION_TYPE_LABELS[static_cast<std::size_t>(t)],
                                      t == m_conversionType))
                {
                    m_conversionType = t;
                }
                if (BB_CONV_REF == t)
                {
                    ImGui::EndDisabled();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::TextUnformatted("Conversion string:");
        ImGui::InputTextMultiline("##ConvString", m_rawBuffer.data(), m_rawBuffer.size(),
                                  ImVec2(-1.0f, ImGui::GetTextLineHeight() * 4.0f));

        if (HasSubEditor(m_conversionType))
        {
            if (ImGui::Button("Edit...", ImVec2(100.0f, 0.0f)))
            {
                OpenSubEditor();
            }
        }
        else
        {
            ImGui::BeginDisabled();
            ImGui::Button("Edit...", ImVec2(100.0f, 0.0f));
            ImGui::EndDisabled();
        }

        if (!m_error.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", m_error.c_str());
        }

        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120.0f, 0.0f)))
        {
            ApplyConversionToVariable();
            m_dispatcherOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
        {
            m_dispatcherOpen = false;
            ImGui::CloseCurrentPopup();
        }

        RenderFactorOffsetEditor();
        RenderRationalFunctionEditor();
        RenderTableEditor();
        RenderTextReplaceEditor();

        ImGui::EndPopup();
    }

    if (!open)
    {
        m_dispatcherOpen = false;
    }
}

void ConversionDialogManager::OpenSubEditor()
{
    m_subEditorType = m_conversionType;
    m_subEditorOpenRequested = true;
    switch (m_conversionType)
    {
    case BB_CONV_FACTOFF:
    case BB_CONV_OFFFACT:
        ParseFactorOffset();
        break;
    case BB_CONV_RAT_FUNC:
        ParseRationalFunction();
        break;
    case BB_CONV_TAB_INTP:
    case BB_CONV_TAB_NOINTP:
        ParseTable();
        break;
    case BB_CONV_TEXTREP:
        ParseTextReplace();
        break;
    default:
        m_subEditorOpenRequested = false;
        m_subEditorType = -1;
        break;
    }
}

void ConversionDialogManager::ParseFactorOffset()
{
    m_coefficients.fill(0.0);
    char *p{m_rawBuffer.data()};
    m_coefficients[0] = std::strtod(p, &p);
    if (':' == *p)
    {
        m_coefficients[1] = std::strtod(p + 1, &p);
    }
}

std::string ConversionDialogManager::EncodeFactorOffset() const
{
    return FormatCoefficient(m_coefficients[0]) + ":" + FormatCoefficient(m_coefficients[1]);
}

void ConversionDialogManager::RenderFactorOffsetEditor()
{
    if ((BB_CONV_FACTOFF != m_subEditorType) && (BB_CONV_OFFFACT != m_subEditorType))
    {
        return;
    }
    if (m_subEditorOpenRequested)
    {
        ImGui::OpenPopup("Edit factor/offset###ConvFactorOffset");
        m_subEditorOpenRequested = false;
    }
    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Edit factor/offset###ConvFactorOffset", nullptr,
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    {
        const char *formula{(BB_CONV_FACTOFF == m_subEditorType) ? "a * x + b" : "a * (x + b)"};
        ImGui::Text("Formula: %s", formula);
        ImGui::InputDouble("a (factor)", &m_coefficients[0], 0.0, 0.0, "%.18g");
        ImGui::InputDouble("b (offset)", &m_coefficients[1], 0.0, 0.0, "%.18g");

        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(100.0f, 0.0f)))
        {
            SetRawBuffer(EncodeFactorOffset());
            m_subEditorType = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
        {
            m_subEditorType = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ConversionDialogManager::ParseRationalFunction()
{
    m_coefficients.fill(0.0);
    char *p{m_rawBuffer.data()};
    for (std::size_t i{0}; i < m_coefficients.size(); ++i)
    {
        m_coefficients[i] = std::strtod(p, &p);
        if (':' != *p)
        {
            break;
        }
        ++p;
    }
}

std::string ConversionDialogManager::EncodeRationalFunction() const
{
    std::string result{};
    for (std::size_t i{0}; i < m_coefficients.size(); ++i)
    {
        if (0u < i)
        {
            result += ":";
        }
        result += FormatCoefficient(m_coefficients[i]);
    }
    return result;
}

void ConversionDialogManager::RenderRationalFunctionEditor()
{
    if (BB_CONV_RAT_FUNC != m_subEditorType)
    {
        return;
    }
    if (m_subEditorOpenRequested)
    {
        ImGui::OpenPopup("Edit rational function###ConvRatFunc");
        m_subEditorOpenRequested = false;
    }
    ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Edit rational function###ConvRatFunc", nullptr,
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("y = (a*x^2 + b*x + c) / (d*x^2 + e*x + f)");
        static const std::array<const char *, 6> labels{{"a", "b", "c", "d", "e", "f"}};
        for (std::size_t i{0}; i < m_coefficients.size(); ++i)
        {
            ImGui::InputDouble(labels[i], &m_coefficients[i], 0.0, 0.0, "%.18g");
        }

        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(100.0f, 0.0f)))
        {
            SetRawBuffer(EncodeRationalFunction());
            m_subEditorType = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
        {
            m_subEditorType = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ConversionDialogManager::ParseTable()
{
    m_tablePairs.clear();
    // Mirrors the C parser: pairs "phys/raw" separated by ':'.
    char *p{m_rawBuffer.data()};
    while (('\0' != *p))
    {
        TablePair pair{};
        pair.phys = std::strtod(p, &p);
        if ('/' == *p)
        {
            ++p;
        }
        else
        {
            break;
        }
        pair.raw = std::strtod(p, &p);
        m_tablePairs.push_back(pair);
        if (':' == *p)
        {
            ++p;
        }
        else
        {
            break;
        }
    }
    if (m_tablePairs.empty())
    {
        m_tablePairs.push_back(TablePair{});
    }
}

std::string ConversionDialogManager::EncodeTable() const
{
    std::string result{};
    for (std::size_t i{0}; i < m_tablePairs.size(); ++i)
    {
        if (0u < i)
        {
            result += ":";
        }
        result += FormatCoefficient(m_tablePairs[i].phys);
        result += "/";
        result += FormatCoefficient(m_tablePairs[i].raw);
    }
    return result;
}

void ConversionDialogManager::RenderTableEditor()
{
    if ((BB_CONV_TAB_INTP != m_subEditorType) && (BB_CONV_TAB_NOINTP != m_subEditorType))
    {
        return;
    }
    if (m_subEditorOpenRequested)
    {
        ImGui::OpenPopup("Edit table###ConvTable");
        m_subEditorOpenRequested = false;
    }
    ImGui::SetNextWindowSize(ImVec2(420.0f, 360.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Edit table###ConvTable", nullptr,
                               ImGuiWindowFlags_NoSavedSettings))
    {
        if (ImGui::BeginTable("##convtabletbl", 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY,
                              ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 2.0f)))
        {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableSetupColumn("Phys");
            ImGui::TableSetupColumn("Raw");
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableHeadersRow();

            int removeIndex{-1};
            for (int i{0}; i < static_cast<int>(m_tablePairs.size()); ++i)
            {
                ImGui::TableNextRow();
                ImGui::PushID(i);
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%d", i);
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputDouble("##phys", &m_tablePairs[static_cast<std::size_t>(i)].phys, 0.0, 0.0,
                                   "%.18g");
                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputDouble("##raw", &m_tablePairs[static_cast<std::size_t>(i)].raw, 0.0, 0.0,
                                   "%.18g");
                ImGui::TableSetColumnIndex(3);
                if (ImGui::SmallButton("X"))
                {
                    removeIndex = i;
                }
                ImGui::PopID();
            }
            if ((0 <= removeIndex) && (1 < m_tablePairs.size()))
            {
                m_tablePairs.erase(m_tablePairs.begin() + removeIndex);
            }
            ImGui::EndTable();
        }

        if (ImGui::Button("Add row", ImVec2(100.0f, 0.0f)))
        {
            m_tablePairs.push_back(TablePair{});
        }

        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(100.0f, 0.0f)))
        {
            SetRawBuffer(EncodeTable());
            m_subEditorType = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
        {
            m_subEditorType = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ConversionDialogManager::ParseTextReplace()
{
    m_textReplaceEntries.clear();
    m_textReplaceError.clear();

    int index{0};
    std::array<char, TEXTREP_ENUM_TEXT_MAXC> enumText{};
    int64_t from{0};
    int64_t to{0};
    int32_t color{-1};
    while (0 <= GetNextEnumFromEnumList(index, m_rawBuffer.data(), enumText.data(),
                                        static_cast<int>(enumText.size()), &from, &to, &color))
    {
        TextReplaceEntry entry{};
        if (INT64_MIN < from)
        {
            std::snprintf(entry.from.data(), entry.from.size(), "%lld",
                          static_cast<long long>(from));
        }
        else
        {
            std::snprintf(entry.from.data(), entry.from.size(), "*");
        }
        if (INT64_MAX > to)
        {
            std::snprintf(entry.to.data(), entry.to.size(), "%lld", static_cast<long long>(to));
        }
        else
        {
            std::snprintf(entry.to.data(), entry.to.size(), "*");
        }
        std::snprintf(entry.text.data(), entry.text.size(), "%s", enumText.data());
        entry.color = color;
        m_textReplaceEntries.push_back(entry);
        ++index;
    }
}

std::string ConversionDialogManager::EncodeTextReplace() const
{
    std::string result{};
    for (const TextReplaceEntry &entry : m_textReplaceEntries)
    {
        result += entry.from.data();
        result += " ";
        result += entry.to.data();
        result += " \"";
        if (0 <= entry.color)
        {
            std::array<char, 64> help{};
            std::snprintf(help.data(), help.size(), "RGB(0x%02X:0x%02X:0x%02X)",
                          (entry.color >> 0) & 0xFF, (entry.color >> 8) & 0xFF,
                          (entry.color >> 16) & 0xFF);
            result += help.data();
        }
        result += entry.text.data();
        result += "\";";
    }
    return result;
}

bool ConversionDialogManager::SyntaxCheckTextReplace(const std::string &par_String,
                                                     std::string &out_Error) const
{
    int index{0};
    std::array<char, TEXTREP_ENUM_TEXT_MAXC> enumText{};
    int64_t from{0};
    int64_t to{0};
    int32_t color{-1};
    std::array<char, 1024> errMsg{};
    while (true)
    {
        const int ret{GetNextEnumFromEnumListErrMsg(index, par_String.c_str(), enumText.data(),
                                                    static_cast<int>(enumText.size()), &from, &to,
                                                    &color, errMsg.data(),
                                                    static_cast<int>(errMsg.size()))};
        if (-1 == ret)
        {
            break; // End of list reached: all entries valid.
        }
        if (-2 == ret)
        {
            out_Error = errMsg.data();
            return false;
        }
        ++index;
    }
    return true;
}

void ConversionDialogManager::RenderTextReplaceEditor()
{
    if (BB_CONV_TEXTREP != m_subEditorType)
    {
        return;
    }
    if (m_subEditorOpenRequested)
    {
        ImGui::OpenPopup("Edit text replace###ConvTextReplace");
        m_subEditorOpenRequested = false;
    }
    ImGui::SetNextWindowSize(ImVec2(560.0f, 400.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Edit text replace###ConvTextReplace", nullptr,
                               ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::TextUnformatted("Ranges [from..to] map to a replacement text (\"*\" = open range).");
        if (ImGui::BeginTable("##convtexttbl", 6,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY,
                              ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 3.0f)))
        {
            ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("To", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Text");
            ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableSetupColumn("RGB", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableHeadersRow();

            int removeIndex{-1};
            for (int i{0}; i < static_cast<int>(m_textReplaceEntries.size()); ++i)
            {
                TextReplaceEntry &entry{m_textReplaceEntries[static_cast<std::size_t>(i)]};
                ImGui::TableNextRow();
                ImGui::PushID(i);
                ImGui::TableSetColumnIndex(0);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputText("##from", entry.from.data(), entry.from.size());
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputText("##to", entry.to.data(), entry.to.size());
                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputText("##text", entry.text.data(), entry.text.size());
                ImGui::TableSetColumnIndex(3);
                bool colorValid{0 <= entry.color};
                if (ImGui::Checkbox("##colorvalid", &colorValid))
                {
                    entry.color = colorValid ? 0 : -1;
                }
                ImGui::TableSetColumnIndex(4);
                if (0 <= entry.color)
                {
                    float rgb[3]{static_cast<float>((entry.color >> 0) & 0xFF) / 255.0f,
                                 static_cast<float>((entry.color >> 8) & 0xFF) / 255.0f,
                                 static_cast<float>((entry.color >> 16) & 0xFF) / 255.0f};
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::ColorEdit3("##rgb", rgb,
                                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel))
                    {
                        const int r{static_cast<int>(rgb[0] * 255.0f + 0.5f)};
                        const int g{static_cast<int>(rgb[1] * 255.0f + 0.5f)};
                        const int b{static_cast<int>(rgb[2] * 255.0f + 0.5f)};
                        entry.color = r | (g << 8) | (b << 16);
                    }
                }
                else
                {
                    ImGui::TextUnformatted("-");
                }
                ImGui::TableSetColumnIndex(5);
                if (ImGui::SmallButton("X"))
                {
                    removeIndex = i;
                }
                ImGui::PopID();
            }
            if (0 <= removeIndex)
            {
                m_textReplaceEntries.erase(m_textReplaceEntries.begin() + removeIndex);
            }
            ImGui::EndTable();
        }

        if (ImGui::Button("Add entry", ImVec2(100.0f, 0.0f)))
        {
            TextReplaceEntry entry{};
            std::snprintf(entry.from.data(), entry.from.size(), "0");
            std::snprintf(entry.to.data(), entry.to.size(), "0");
            std::snprintf(entry.text.data(), entry.text.size(), "x");
            entry.color = -1;
            m_textReplaceEntries.push_back(entry);
        }

        if (!m_textReplaceError.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", m_textReplaceError.c_str());
        }

        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(100.0f, 0.0f)))
        {
            const std::string encoded{EncodeTextReplace()};
            if (SyntaxCheckTextReplace(encoded, m_textReplaceError))
            {
                SetRawBuffer(encoded);
                m_subEditorType = -1;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
        {
            m_subEditorType = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace XilEnvImGui
