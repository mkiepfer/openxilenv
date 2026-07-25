#include "Widgets/Text/TextWindow.h"

#include <cctype>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "imgui.h"

extern "C" {
#include "Blackboard.h"
#include "BlackboardAccess.h"
#include "IniDataBase.h"
}

namespace XilEnvImGui
{

namespace
{

constexpr int VALUE_BUFFER_SIZE{256};

/**
 * @brief Returns true for concrete numeric blackboard types formattable by bbvari_to_string.
 */
bool IsConcreteType(enum BB_DATA_TYPES par_Type)
{
    if ((BB_BYTE <= par_Type) && (BB_DOUBLE >= par_Type))
    {
        return true;
    }
    return (BB_QWORD == par_Type) || (BB_UQWORD == par_Type);
}

/**
 * @brief Trims leading and trailing ASCII whitespace.
 */
std::string Trim(const std::string &par_Text)
{
    size_t begin{0};
    size_t end{par_Text.size()};
    while ((begin < end) && (0 != std::isspace(static_cast<unsigned char>(par_Text[begin]))))
    {
        ++begin;
    }
    while ((end > begin) && (0 != std::isspace(static_cast<unsigned char>(par_Text[end - 1]))))
    {
        --end;
    }
    return par_Text.substr(begin, end - begin);
}

} // namespace

// -------------------------------------------------------------------------
// TextWindowType
// -------------------------------------------------------------------------

std::unique_ptr<IWindowInstance> TextWindowType::CreateNew(const std::string &par_Title)
{
    return std::make_unique<TextWindow>(this, par_Title);
}

std::unique_ptr<IWindowInstance> TextWindowType::CreateFromConfig(int par_FileDescriptor,
                                                                  const char *par_Section,
                                                                  const std::string &par_Name)
{
    auto window = std::make_unique<TextWindow>(this, par_Name);
    window->ReadConfig(par_FileDescriptor, par_Section);
    return window;
}

// -------------------------------------------------------------------------
// TextWindow
// -------------------------------------------------------------------------

TextWindow::TextWindow(IWindowType *par_Type, std::string par_Name) : m_pType{par_Type}
{
    SetName(std::move(par_Name));
}

TextWindow::~TextWindow()
{
    for (Variable &variable : m_variables)
    {
        DetachVariable(variable);
    }
}

void TextWindow::AttachVariable(Variable &par_Variable)
{
    if (0 < par_Variable.vid)
    {
        return;
    }
    const VID vid{add_bbvari(par_Variable.name.c_str(), BB_UNKNOWN_WAIT, nullptr)};
    par_Variable.vid = (0 < vid) ? vid : 0;
    if (0 < par_Variable.vid)
    {
        par_Variable.precision = get_bbvari_format_prec(par_Variable.vid);
    }
}

TextDisplayType TextWindow::DetectDisplayType(std::int32_t par_Vid) const
{
    if (0 >= par_Vid)
    {
        return TextDisplayType::Decimal;
    }
    // Only defined variables can report a meaningful conversion type.
    if (BB_UNKNOWN_WAIT == static_cast<enum BB_DATA_TYPES>(get_bbvaritype(par_Vid)))
    {
        return TextDisplayType::Decimal;
    }
    switch (get_bbvari_conversiontype(par_Vid))
    {
    case BB_CONV_FORMULA:
    case BB_CONV_TEXTREP:
    case BB_CONV_FACTOFF:
    case BB_CONV_OFFFACT:
    case BB_CONV_TAB_INTP:
    case BB_CONV_TAB_NOINTP:
    case BB_CONV_RAT_FUNC:
        // Variables with a conversion default to their physical representation.
        return TextDisplayType::Physical;
    default:
        return TextDisplayType::Decimal;
    }
}

void TextWindow::DetachVariable(Variable &par_Variable)
{
    if (0 < par_Variable.vid)
    {
        (void)remove_bbvari(par_Variable.vid);
        par_Variable.vid = 0;
    }
}

void TextWindow::AddVariable(const std::string &par_Name, TextDisplayType par_DisplayType,
                            bool par_AutoDetectDisplayType)
{
    const std::string name{Trim(par_Name)};
    if (name.empty())
    {
        return;
    }
    for (const Variable &existing : m_variables)
    {
        if (0 == existing.name.compare(name))
        {
            return; // Already present.
        }
    }
    Variable variable;
    variable.name = name;
    AttachVariable(variable);
    // Match the Qt default: variables with a conversion default to phys, others
    // to decimal (only when the caller did not specify an explicit type).
    variable.displayType = par_AutoDetectDisplayType ? DetectDisplayType(variable.vid) : par_DisplayType;
    m_variables.push_back(std::move(variable));
}

void TextWindow::RefreshVariable(Variable &par_Variable)
{
    if (0 >= par_Variable.vid)
    {
        par_Variable.exists = false;
        par_Variable.valueText = "---";
        return;
    }

    char buffer[VALUE_BUFFER_SIZE];

    union BB_VARI raw;
    const enum BB_DATA_TYPES type{read_bbvari_union_type(par_Variable.vid, &raw)};
    if (!IsConcreteType(type))
    {
        par_Variable.exists = false;
        par_Variable.valueText = "---";
        return;
    }
    par_Variable.exists = true;

    // Keep the display precision in sync (it may change once the variable is defined).
    par_Variable.precision = get_bbvari_format_prec(par_Variable.vid);
    const int precision{(0 <= par_Variable.precision) ? par_Variable.precision : 0};

    switch (par_Variable.displayType)
    {
    case TextDisplayType::Hex:
        (void)bbvari_to_string(type, raw, 16, buffer, sizeof(buffer));
        par_Variable.valueText = buffer;
        break;
    case TextDisplayType::Binary:
        (void)bbvari_to_string(type, raw, 2, buffer, sizeof(buffer));
        par_Variable.valueText = buffer;
        break;
    case TextDisplayType::Physical:
    {
        // Text-replacement conversions render their enum text; every other
        // conversion yields a physical value formatted at the signal precision
        // (matching QString::number(phys, 'f', precision)).
        int textColor{-1};
        if (0 <= read_bbvari_textreplace(par_Variable.vid, buffer, sizeof(buffer), &textColor))
        {
            par_Variable.valueText = buffer;
        }
        else
        {
            const double physValue{read_bbvari_convert_double(par_Variable.vid)};
            std::snprintf(buffer, sizeof(buffer), "%.*f", precision, physValue);
            par_Variable.valueText = buffer;
        }
        break;
    }
    case TextDisplayType::Decimal:
    default:
        // Floats honour the per-signal precision (QString::number(v, 'f', prec));
        // integers use the plain base-10 rendering from bbvari_to_string.
        if ((BB_FLOAT == type) || (BB_DOUBLE == type))
        {
            const double rawDouble{(BB_FLOAT == type) ? static_cast<double>(raw.f) : raw.d};
            std::snprintf(buffer, sizeof(buffer), "%.*f", precision, rawDouble);
            par_Variable.valueText = buffer;
        }
        else
        {
            (void)bbvari_to_string(type, raw, 10, buffer, sizeof(buffer));
            par_Variable.valueText = buffer;
        }
        break;
    }

    char unitBuffer[BBVARI_UNIT_SIZE];
    if (0 <= get_bbvari_unit(par_Variable.vid, unitBuffer, sizeof(unitBuffer)))
    {
        par_Variable.unit = unitBuffer;
    }
}

void TextWindow::OnUpdateTick(const WindowContext &par_Context)
{
    (void)par_Context;
    for (Variable &variable : m_variables)
    {
        RefreshVariable(variable);
    }
}

void TextWindow::OnAcceptVariableDrop(const std::string &par_VariableName)
{
    AddVariable(par_VariableName, TextDisplayType::Decimal, true);
}

void TextWindow::RenderContent(const WindowContext &par_Context)
{
    (void)par_Context;

    int columnCount{2};
    if (m_showUnitColumn)
    {
        ++columnCount;
    }
    if (m_showDisplayTypeColumn)
    {
        ++columnCount;
    }

    const ImGuiTableFlags tableFlags{ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                     ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY};

    const ImVec2 avail{ImGui::GetContentRegionAvail()};
    ImGui::BeginChild("##txtcontent", avail, false);

    if (ImGui::BeginTable("##txttable", columnCount, tableFlags))
    {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        if (m_showUnitColumn)
        {
            ImGui::TableSetupColumn("Unit", ImGuiTableColumnFlags_WidthFixed);
        }
        if (m_showDisplayTypeColumn)
        {
            ImGui::TableSetupColumn("Display", ImGuiTableColumnFlags_WidthFixed);
        }
        ImGui::TableHeadersRow();

        static const char *const DISPLAY_LABELS[]{"dec", "hex", "bin", "phys"};

        for (int row{0}; row < static_cast<int>(m_variables.size()); ++row)
        {
            Variable &variable{m_variables[static_cast<size_t>(row)]};
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(row);
            ImGui::Selectable(variable.name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
            {
                ImGui::SetDragDropPayload("BBVAR", variable.name.c_str(),
                                          variable.name.size() + 1u);
                ImGui::TextUnformatted(variable.name.c_str());
                ImGui::EndDragDropSource();
            }

            ImGui::TableSetColumnIndex(1);
            if (!variable.exists)
            {
                ImGui::TextDisabled("%s", variable.valueText.c_str());
            }
            else
            {
                ImGui::TextUnformatted(variable.valueText.c_str());
            }

            int nextColumn{2};
            if (m_showUnitColumn)
            {
                ImGui::TableSetColumnIndex(nextColumn);
                ImGui::TextUnformatted(variable.unit.c_str());
                ++nextColumn;
            }
            if (m_showDisplayTypeColumn)
            {
                ImGui::TableSetColumnIndex(nextColumn);
                int current{static_cast<int>(variable.displayType)};
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::Combo("##disp", &current, DISPLAY_LABELS, 4))
                {
                    variable.displayType = static_cast<TextDisplayType>(current);
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::EndChild();

    if (ImGui::BeginDragDropTarget())
    {
        const ImGuiPayload *payload{ImGui::AcceptDragDropPayload("BBVAR")};
        if (nullptr != payload)
        {
            const std::string dropped{static_cast<const char *>(payload->Data),
                                      static_cast<size_t>(payload->DataSize > 0 ? payload->DataSize - 1 : 0)};
            OnAcceptVariableDrop(dropped);
        }
        ImGui::EndDragDropTarget();
    }
}

void TextWindow::RenderSettingsPopup()
{
    ImGui::Checkbox("Show unit column", &m_showUnitColumn);
    ImGui::Checkbox("Show display-type column", &m_showDisplayTypeColumn);
    ImGui::Separator();

    ImGui::TextUnformatted("Add variable:");
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("##addvar", m_addVariableBuffer, sizeof(m_addVariableBuffer));
    ImGui::SameLine();
    if (ImGui::Button("Add"))
    {
        AddVariable(m_addVariableBuffer, TextDisplayType::Decimal, true);
        m_addVariableBuffer[0] = '\0';
    }

    ImGui::Separator();
    static const char *const DISPLAY_LABELS[]{"dec", "hex", "bin", "phys"};
    int removeIndex{-1};
    for (int row{0}; row < static_cast<int>(m_variables.size()); ++row)
    {
        Variable &variable{m_variables[static_cast<size_t>(row)]};
        ImGui::PushID(1000 + row);
        ImGui::TextUnformatted(variable.name.c_str());
        ImGui::SameLine(220.0f);
        int current{static_cast<int>(variable.displayType)};
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::Combo("##dispcfg", &current, DISPLAY_LABELS, 4))
        {
            variable.displayType = static_cast<TextDisplayType>(current);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
        {
            removeIndex = row;
        }
        ImGui::PopID();
    }
    if (0 <= removeIndex)
    {
        DetachVariable(m_variables[static_cast<size_t>(removeIndex)]);
        m_variables.erase(m_variables.begin() + removeIndex);
    }
}

// -------------------------------------------------------------------------
// Config round-trip (Qt "GUI/Widgets/<name>" layout)
// -------------------------------------------------------------------------

void TextWindow::WriteConfig(int par_FileDescriptor, const char *par_Section)
{
    (void)IniFileDataBaseWriteString(par_Section, "type", m_pType->GetTypeName(), par_FileDescriptor);

    if (!m_fontString.empty())
    {
        (void)IniFileDataBaseWriteString(par_Section, "Font", m_fontString.c_str(), par_FileDescriptor);
    }
    (void)IniFileDataBaseWriteInt(par_Section, "icon", m_icon, par_FileDescriptor);

    char colorString[32];
    std::snprintf(colorString, sizeof(colorString), "0x%x", m_backgroundColor);
    (void)IniFileDataBaseWriteString(par_Section, "BgColor", colorString, par_FileDescriptor);

    int index{0};
    for (; index < static_cast<int>(m_variables.size()); ++index)
    {
        const Variable &variable{m_variables[static_cast<size_t>(index)]};
        char entry[32];
        std::snprintf(entry, sizeof(entry), "E%d", index);
        char line[600];
        std::snprintf(line, sizeof(line), "%d,%s", static_cast<int>(variable.displayType),
                      variable.name.c_str());
        (void)IniFileDataBaseWriteString(par_Section, entry, line, par_FileDescriptor);
    }
    // Remove stale entries left over from a previously larger variable list.
    for (;; ++index)
    {
        char entry[32];
        std::snprintf(entry, sizeof(entry), "E%d", index);
        char existing[600];
        if (0 == IniFileDataBaseReadString(par_Section, entry, "", existing, sizeof(existing),
                                           par_FileDescriptor))
        {
            break;
        }
        (void)IniFileDataBaseWriteString(par_Section, entry, nullptr, par_FileDescriptor);
    }

    (void)IniFileDataBaseWriteYesNo(par_Section, "ShowUnitColumn", m_showUnitColumn ? 1 : 0,
                                    par_FileDescriptor);
    (void)IniFileDataBaseWriteYesNo(par_Section, "ShowDisplayTypeColumn", m_showDisplayTypeColumn ? 1 : 0,
                                    par_FileDescriptor);

    (void)IniFileDataBaseWriteInt(par_Section, "ColumnAlignmentName", m_columnAlignmentName,
                                  par_FileDescriptor);
    (void)IniFileDataBaseWriteInt(par_Section, "ColumnAlignmentValue", m_columnAlignmentValue,
                                  par_FileDescriptor);
    (void)IniFileDataBaseWriteInt(par_Section, "ColumnAlignmentUnit", m_columnAlignmentUnit,
                                  par_FileDescriptor);
    (void)IniFileDataBaseWriteInt(par_Section, "ColumnAlignmentDisplayType", m_columnAlignmentDisplayType,
                                  par_FileDescriptor);
}

void TextWindow::ReadConfig(int par_FileDescriptor, const char *par_Section)
{
    // Drop any previously attached variables before reloading.
    for (Variable &variable : m_variables)
    {
        DetachVariable(variable);
    }
    m_variables.clear();

    char fontBuffer[256];
    if (0 < IniFileDataBaseReadString(par_Section, "Font", "", fontBuffer, sizeof(fontBuffer),
                                      par_FileDescriptor))
    {
        m_fontString = fontBuffer;
    }

    m_icon = IniFileDataBaseReadInt(par_Section, "icon", 0, par_FileDescriptor);
    m_backgroundColor =
        static_cast<std::uint32_t>(IniFileDataBaseReadInt(par_Section, "BgColor", 0xFFFFFF, par_FileDescriptor));

    for (int i{0};; ++i)
    {
        char entry[32];
        std::snprintf(entry, sizeof(entry), "E%d", i);
        char line[600];
        if (0 == IniFileDataBaseReadString(par_Section, entry, "", line, sizeof(line), par_FileDescriptor))
        {
            break;
        }
        const std::string lineStr{line};
        const size_t comma{lineStr.find(',')};
        if (std::string::npos == comma)
        {
            continue;
        }
        const int typeValue{std::atoi(lineStr.substr(0, comma).c_str())};
        const std::string name{Trim(lineStr.substr(comma + 1))};
        // Config stores an explicit per-variable display type (E<i>="<type>,<name>").
        AddVariable(name, static_cast<TextDisplayType>(typeValue), false);
    }

    m_columnAlignmentName =
        IniFileDataBaseReadInt(par_Section, "ColumnAlignmentName", 0, par_FileDescriptor);
    m_columnAlignmentValue =
        IniFileDataBaseReadInt(par_Section, "ColumnAlignmentValue", 0, par_FileDescriptor);
    m_columnAlignmentUnit =
        IniFileDataBaseReadInt(par_Section, "ColumnAlignmentUnit", 0, par_FileDescriptor);
    m_columnAlignmentDisplayType =
        IniFileDataBaseReadInt(par_Section, "ColumnAlignmentDisplayType", 0, par_FileDescriptor);

    m_showUnitColumn =
        (0 != IniFileDataBaseReadYesNo(par_Section, "ShowUnitColumn", 1, par_FileDescriptor));
    m_showDisplayTypeColumn =
        (0 != IniFileDataBaseReadYesNo(par_Section, "ShowDisplayTypeColumn", 0, par_FileDescriptor));
}

} // namespace XilEnvImGui
