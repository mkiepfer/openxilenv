#include "Widgets/Lamps/LampsWindow.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

#include "Framework/BlackboardDragDrop.h"
#include "Framework/GuiComponents.h"

extern "C" {
#include "Blackboard.h"
#include "BlackboardAccess.h"
#include "IniDataBase.h"
}

namespace XilEnvImGui
{

namespace
{

constexpr const char *DEFAULT_STENCIL{"Round"};

/**
 * @brief Packs a 0x00bbggrr blackboard color into an ImGui ABGR color.
 */
ImU32 BlackboardColorToImU32(std::uint32_t par_Color)
{
    const int red{static_cast<int>(par_Color & 0x000000FFu)};
    const int green{static_cast<int>((par_Color & 0x0000FF00u) >> 8)};
    const int blue{static_cast<int>((par_Color & 0x00FF0000u) >> 16)};
    return IM_COL32(red, green, blue, 255);
}

} // namespace

std::unique_ptr<IWindowInstance> LampsWindowType::CreateNew(const std::string &par_Title)
{
    return std::make_unique<LampsWindow>(this, par_Title);
}

std::unique_ptr<IWindowInstance> LampsWindowType::CreateFromConfig(int par_FileDescriptor,
                                                                  const char *par_Section,
                                                                  const std::string &par_Name)
{
    auto window = std::make_unique<LampsWindow>(this, par_Name);
    window->ReadConfig(par_FileDescriptor, par_Section);
    return window;
}

LampsWindow::LampsWindow(IWindowType *par_Type, std::string par_Name) : m_pType{par_Type}
{
    SetName(std::move(par_Name));
}

LampsWindow::~LampsWindow()
{
    for (Lamp &lamp : m_lamps)
    {
        if (0 < lamp.vid)
        {
            (void)remove_bbvari(lamp.vid);
        }
    }
}

void LampsWindow::RefreshLamp(Lamp &par_Lamp)
{
    par_Lamp.valid = false;
    if (0 >= par_Lamp.vid)
    {
        return;
    }
    if ((BB_UNKNOWN_WAIT == static_cast<enum BB_DATA_TYPES>(get_bbvaritype(par_Lamp.vid))) ||
        (BB_CONV_TEXTREP != get_bbvari_conversiontype(par_Lamp.vid)))
    {
        return;
    }
    char stateText[BBVARI_ENUM_NAME_SIZE];
    int color{0};
    if (0 == read_bbvari_textreplace(par_Lamp.vid, stateText, sizeof(stateText), &color))
    {
        par_Lamp.color = static_cast<std::uint32_t>(color);
        par_Lamp.stateText = stateText;
        par_Lamp.valid = true;
    }
}

void LampsWindow::AddLamp(const std::string &par_Name)
{
    if (par_Name.empty())
    {
        return;
    }
    Lamp lamp;
    lamp.name = par_Name;
    const VID vid{add_bbvari(par_Name.c_str(), BB_UNKNOWN_WAIT, nullptr)};
    lamp.vid = (0 < vid) ? vid : 0;
    lamp.stencil = DEFAULT_STENCIL;

    // Place at the next free grid cell (row-major).
    const int slot{static_cast<int>(m_lamps.size())};
    const int columns{(0 < m_columns) ? m_columns : 1};
    lamp.row = slot / columns;
    lamp.column = slot % columns;
    if ((lamp.row + 1) > m_rows)
    {
        m_rows = lamp.row + 1;
    }
    RefreshLamp(lamp);
    m_lamps.push_back(std::move(lamp));
}

void LampsWindow::RemoveLamp(std::size_t par_Index)
{
    if (par_Index >= m_lamps.size())
    {
        return;
    }
    if (0 < m_lamps[par_Index].vid)
    {
        (void)remove_bbvari(m_lamps[par_Index].vid);
    }
    m_lamps.erase(m_lamps.begin() + static_cast<long>(par_Index));
}

void LampsWindow::OnUpdateTick(const WindowContext &par_Context)
{
    (void)par_Context;
    for (Lamp &lamp : m_lamps)
    {
        RefreshLamp(lamp);
    }
}

void LampsWindow::OnAcceptVariableDrop(const std::string &par_VariableName)
{
    AddLamp(par_VariableName);
}

void LampsWindow::RenderContent(const WindowContext &par_Context)
{
    (void)par_Context;

    const ImVec2 avail{ImGui::GetContentRegionAvail()};
    const ImVec2 origin{ImGui::GetCursorScreenPos()};
    ImDrawList *drawList{ImGui::GetWindowDrawList()};

    // Paint the scene background.
    if ((avail.x > 1.0f) && (avail.y > 1.0f))
    {
        drawList->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y),
                                BlackboardColorToImU32(m_backgroundColor));
    }

    const int columns{(0 < m_columns) ? m_columns : 1};
    const int rows{(0 < m_rows) ? m_rows : 1};
    const float cellWidth{avail.x / static_cast<float>(columns)};
    const float cellHeight{avail.y / static_cast<float>(rows)};

    for (const Lamp &lamp : m_lamps)
    {
        const float spanCols{static_cast<float>(std::max(1, lamp.columnSpan))};
        const float spanRows{static_cast<float>(std::max(1, lamp.rowSpan))};
        const ImVec2 cellMin{origin.x + static_cast<float>(lamp.column) * cellWidth,
                             origin.y + static_cast<float>(lamp.row) * cellHeight};
        const ImVec2 cellMax{cellMin.x + cellWidth * spanCols, cellMin.y + cellHeight * spanRows};
        const ImVec2 center{(cellMin.x + cellMax.x) * 0.5f, (cellMin.y + cellMax.y) * 0.5f};
        const float radius{std::max(4.0f, std::min(cellWidth, cellHeight) * 0.35f)};

        const ImU32 fill{lamp.valid ? BlackboardColorToImU32(lamp.color)
                                     : BlackboardColorToImU32(m_backgroundColor)};
        drawList->AddCircleFilled(center, radius, fill, 32);
        drawList->AddCircle(center, radius, ImGui::GetColorU32(ImGuiCol_Border), 32);

        // Label (variable name and current enum text) under the lamp.
        const char *label{lamp.valid && !lamp.stateText.empty() ? lamp.stateText.c_str()
                                                                 : lamp.name.c_str()};
        const ImVec2 textSize{ImGui::CalcTextSize(label)};
        drawList->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y + radius + 2.0f),
                          ImGui::GetColorU32(ImGuiCol_Text), label);
    }

    // Reserve the drawn area so the drop target covers the whole grid.
    ImGui::Dummy(avail);

    if (ImGui::BeginDragDropTarget())
    {
        BlackboardVariablePayload payload;
        if (AcceptBlackboardVariableDrop(payload))
        {
            AddLamp(payload.name);
        }
        ImGui::EndDragDropTarget();
    }
}

void LampsWindow::RenderSettingsPopup()
{
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("Columns", &m_columns);
    if (1 > m_columns)
    {
        m_columns = 1;
    }
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("Rows", &m_rows);
    if (1 > m_rows)
    {
        m_rows = 1;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Add lamp variable:");
    (void)DrawBlackboardSelector("##add", m_addBuffer, sizeof(m_addBuffer));
    ImGui::SameLine();
    if (ImGui::Button("Add"))
    {
        AddLamp(m_addBuffer);
        m_addBuffer[0] = '\0';
    }

    ImGui::Separator();
    int removeIndex{-1};
    for (std::size_t i{0}; i < m_lamps.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        Lamp &lamp{m_lamps[i]};
        ImGui::TextUnformatted(lamp.name.c_str());
        ImGui::SameLine(200.0f);
        ImGui::SetNextItemWidth(60.0f);
        ImGui::InputInt("r##row", &lamp.row);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60.0f);
        ImGui::InputInt("c##col", &lamp.column);
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
        {
            removeIndex = static_cast<int>(i);
        }
        ImGui::PopID();
    }
    if (0 <= removeIndex)
    {
        RemoveLamp(static_cast<std::size_t>(removeIndex));
    }
}

void LampsWindow::WriteConfig(int par_FileDescriptor, const char *par_Section)
{
    (void)IniFileDataBaseWriteString(par_Section, "type", m_pType->GetTypeName(), par_FileDescriptor);
    (void)IniFileDataBaseWriteInt(par_Section, "Columns", m_columns, par_FileDescriptor);
    (void)IniFileDataBaseWriteInt(par_Section, "Rows", m_rows, par_FileDescriptor);

    char bkColor[32];
    std::snprintf(bkColor, sizeof(bkColor), "0x%08X", static_cast<unsigned int>(m_backgroundColor));
    (void)IniFileDataBaseWriteString(par_Section, "BkColor", bkColor, par_FileDescriptor);

    int index{0};
    for (; index < static_cast<int>(m_lamps.size()); ++index)
    {
        const Lamp &lamp{m_lamps[static_cast<std::size_t>(index)]};
        char entry[32];
        std::snprintf(entry, sizeof(entry), "Lamp_%d", index);
        char line[BBVARI_NAME_SIZE + 128];
        std::snprintf(line, sizeof(line), "%s, %d, %d, %d, %d, %s, 0", lamp.name.c_str(), lamp.row,
                      lamp.column, lamp.rowSpan, lamp.columnSpan,
                      lamp.stencil.empty() ? DEFAULT_STENCIL : lamp.stencil.c_str());
        (void)IniFileDataBaseWriteString(par_Section, entry, line, par_FileDescriptor);
    }
    // Remove stale Lamp_<n> entries left from a previously longer list.
    for (int i{index};; ++i)
    {
        char entry[32];
        std::snprintf(entry, sizeof(entry), "Lamp_%d", i);
        char existing[BBVARI_NAME_SIZE + 128];
        if (0 == IniFileDataBaseReadString(par_Section, entry, "", existing, sizeof(existing),
                                           par_FileDescriptor))
        {
            break;
        }
        (void)IniFileDataBaseWriteString(par_Section, entry, nullptr, par_FileDescriptor);
    }
}

void LampsWindow::ReadConfig(int par_FileDescriptor, const char *par_Section)
{
    for (Lamp &lamp : m_lamps)
    {
        if (0 < lamp.vid)
        {
            (void)remove_bbvari(lamp.vid);
        }
    }
    m_lamps.clear();

    m_backgroundColor =
        static_cast<std::uint32_t>(IniFileDataBaseReadInt(par_Section, "BkColor", 0, par_FileDescriptor));
    m_columns = IniFileDataBaseReadInt(par_Section, "Columns", 2, par_FileDescriptor);
    m_rows = IniFileDataBaseReadInt(par_Section, "Rows", 2, par_FileDescriptor);
    if (1 > m_columns)
    {
        m_columns = 1;
    }
    if (1 > m_rows)
    {
        m_rows = 1;
    }

    for (int i{0};; ++i)
    {
        char entry[32];
        std::snprintf(entry, sizeof(entry), "Lamp_%d", i);
        char line[BBVARI_NAME_SIZE + 256];
        if (0 == IniFileDataBaseReadString(par_Section, entry, "", line, sizeof(line), par_FileDescriptor))
        {
            break;
        }
        // Format: "name, row, column, rowSpan, columnSpan, stencil, convFlag"
        std::string text{line};
        std::vector<std::string> fields;
        std::size_t pos{0};
        while (pos <= text.size())
        {
            const std::size_t comma{text.find(',', pos)};
            const std::size_t end{(std::string::npos == comma) ? text.size() : comma};
            std::string field{text.substr(pos, end - pos)};
            // Trim.
            const std::size_t b{field.find_first_not_of(" \t")};
            const std::size_t e{field.find_last_not_of(" \t")};
            fields.push_back((std::string::npos == b) ? std::string{} : field.substr(b, e - b + 1));
            if (std::string::npos == comma)
            {
                break;
            }
            pos = comma + 1;
        }
        if (7 != fields.size())
        {
            continue;
        }
        Lamp lamp;
        lamp.name = fields[0];
        const VID vid{add_bbvari(lamp.name.c_str(), BB_UNKNOWN_WAIT, nullptr)};
        lamp.vid = (0 < vid) ? vid : 0;
        lamp.row = std::atoi(fields[1].c_str());
        lamp.column = std::atoi(fields[2].c_str());
        lamp.rowSpan = std::max(1, std::atoi(fields[3].c_str()));
        lamp.columnSpan = std::max(1, std::atoi(fields[4].c_str()));
        lamp.stencil = fields[5].empty() ? DEFAULT_STENCIL : fields[5];
        RefreshLamp(lamp);
        m_lamps.push_back(std::move(lamp));
    }
}

} // namespace XilEnvImGui
