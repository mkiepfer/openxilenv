#include "Widgets/Enum/EnumWindow.h"

#include <cstdint>
#include <cstdio>
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
#include "TextReplace.h"
}

namespace XilEnvImGui
{

namespace
{

constexpr int ENUM_ENTRY_BUFFER_SIZE{512};

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

std::unique_ptr<IWindowInstance> EnumWindowType::CreateNew(const std::string &par_Title)
{
    return std::make_unique<EnumWindow>(this, par_Title);
}

std::unique_ptr<IWindowInstance> EnumWindowType::CreateFromConfig(int par_FileDescriptor,
                                                                 const char *par_Section,
                                                                 const std::string &par_Name)
{
    auto window = std::make_unique<EnumWindow>(this, par_Name);
    window->ReadConfig(par_FileDescriptor, par_Section);
    return window;
}

EnumWindow::EnumWindow(IWindowType *par_Type, std::string par_Name) : m_pType{par_Type}
{
    SetName(std::move(par_Name));
}

EnumWindow::~EnumWindow()
{
    for (EnumVariable &variable : m_variables)
    {
        if (0 < variable.vid)
        {
            (void)remove_bbvari(variable.vid);
        }
    }
}

void EnumWindow::RefreshEntries(EnumVariable &par_Variable)
{
    par_Variable.entries.clear();
    if (0 >= par_Variable.vid)
    {
        return;
    }
    if (get_bbvaritype(par_Variable.vid) > BB_DOUBLE)
    {
        return; // only concrete data types have a usable enum
    }
    if (BB_CONV_TEXTREP != get_bbvari_conversiontype(par_Variable.vid))
    {
        return; // no enum conversion
    }

    // Fetch the conversion string, growing the buffer if the fixed one is too small.
    char stackBuffer[1024];
    std::vector<char> heapBuffer;
    char *conversion{stackBuffer};
    int size{get_bbvari_conversion(par_Variable.vid, conversion, static_cast<int>(sizeof(stackBuffer)))};
    if (0 > size)
    {
        heapBuffer.resize(static_cast<std::size_t>(-size));
        conversion = heapBuffer.data();
        if (0 >= get_bbvari_conversion(par_Variable.vid, conversion, -size))
        {
            return;
        }
    }

    int index{0};
    char entryText[ENUM_ENTRY_BUFFER_SIZE];
    std::int64_t low{0};
    std::int64_t high{0};
    std::int32_t color{0};
    while (0 < (index = GetNextEnumFromEnumList(index, conversion, entryText, sizeof(entryText), &low,
                                                &high, &color)))
    {
        EnumEntry entry;
        entry.label = entryText;
        entry.lowValue = static_cast<double>(low);
        entry.highValue = static_cast<double>(high);
        entry.color = static_cast<std::uint32_t>(color);
        par_Variable.entries.push_back(std::move(entry));
    }
}

void EnumWindow::AddVariable(const std::string &par_Name)
{
    if (par_Name.empty())
    {
        return;
    }
    for (const EnumVariable &existing : m_variables)
    {
        if (0 == existing.name.compare(par_Name))
        {
            return;
        }
    }
    EnumVariable variable;
    variable.name = par_Name;
    const VID vid{add_bbvari(par_Name.c_str(), BB_UNKNOWN_WAIT, nullptr)};
    variable.vid = (0 < vid) ? vid : 0;
    RefreshEntries(variable);
    m_variables.push_back(std::move(variable));
}

void EnumWindow::RemoveVariable(std::size_t par_Index)
{
    if (par_Index >= m_variables.size())
    {
        return;
    }
    if (0 < m_variables[par_Index].vid)
    {
        (void)remove_bbvari(m_variables[par_Index].vid);
    }
    m_variables.erase(m_variables.begin() + static_cast<long>(par_Index));
}

void EnumWindow::OnUpdateTick(const WindowContext &par_Context)
{
    (void)par_Context;
    for (EnumVariable &variable : m_variables)
    {
        if (0 >= variable.vid)
        {
            continue;
        }
        if (variable.entries.empty())
        {
            // Retry decoding once the variable becomes defined (type/conversion known).
            RefreshEntries(variable);
        }
        variable.currentValue = read_bbvari_convert_double(variable.vid);
    }
}

void EnumWindow::OnAcceptVariableDrop(const std::string &par_VariableName)
{
    AddVariable(par_VariableName);
}

void EnumWindow::RenderContent(const WindowContext &par_Context)
{
    (void)par_Context;

    if (m_variables.empty())
    {
        ImGui::TextDisabled("Drop a blackboard variable here");
    }

    for (std::size_t v{0}; v < m_variables.size(); ++v)
    {
        EnumVariable &variable{m_variables[v]};
        ImGui::PushID(static_cast<int>(v));
        ImGui::TextUnformatted(variable.name.c_str());

        for (std::size_t e{0}; e < variable.entries.size(); ++e)
        {
            const EnumEntry &entry{variable.entries[e]};
            const bool active{(variable.currentValue >= entry.lowValue) &&
                              (variable.currentValue <= entry.highValue)};

            ImGui::PushID(static_cast<int>(e));
            // Colored state indicator, then the clickable state button.
            const ImU32 dotColor{BlackboardColorToImU32(entry.color)};
            const float dotRadius{ImGui::GetFontSize() * 0.35f};
            const ImVec2 cursor{ImGui::GetCursorScreenPos()};
            ImGui::Dummy(ImVec2(dotRadius * 2.0f + 4.0f, ImGui::GetFrameHeight()));
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(cursor.x + dotRadius + 2.0f, cursor.y + ImGui::GetFrameHeight() * 0.5f), dotRadius,
                dotColor);
            ImGui::SameLine();

            if (active)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }
            if (ImGui::Button(entry.label.c_str()))
            {
                if (0 < variable.vid)
                {
                    write_bbvari_minmax_check(variable.vid, entry.lowValue);
                }
            }
            if (active)
            {
                ImGui::PopStyleColor();
            }
            ImGui::PopID();
        }

        ImGui::Separator();
        ImGui::PopID();
    }

    if (ImGui::BeginDragDropTarget())
    {
        BlackboardVariablePayload payload;
        if (AcceptBlackboardVariableDrop(payload))
        {
            AddVariable(payload.name);
        }
        ImGui::EndDragDropTarget();
    }
}

void EnumWindow::RenderSettingsPopup()
{
    ImGui::TextUnformatted("Add variable:");
    if (DrawBlackboardSelector("##add", m_addBuffer, sizeof(m_addBuffer)))
    {
        // selection only fills the buffer; the Add button commits it
    }
    ImGui::SameLine();
    if (ImGui::Button("Add"))
    {
        AddVariable(m_addBuffer);
        m_addBuffer[0] = '\0';
    }

    ImGui::Separator();
    int removeIndex{-1};
    for (std::size_t v{0}; v < m_variables.size(); ++v)
    {
        ImGui::PushID(static_cast<int>(v));
        ImGui::TextUnformatted(m_variables[v].name.c_str());
        ImGui::SameLine(260.0f);
        if (ImGui::SmallButton("x"))
        {
            removeIndex = static_cast<int>(v);
        }
        ImGui::PopID();
    }
    if (0 <= removeIndex)
    {
        RemoveVariable(static_cast<std::size_t>(removeIndex));
    }
}

void EnumWindow::WriteConfig(int par_FileDescriptor, const char *par_Section)
{
    (void)IniFileDataBaseWriteString(par_Section, "type", m_pType->GetTypeName(), par_FileDescriptor);
    int count{0};
    for (; count < static_cast<int>(m_variables.size()); ++count)
    {
        char entry[32];
        std::snprintf(entry, sizeof(entry), "Variable%d", count);
        (void)IniFileDataBaseWriteString(par_Section, entry, m_variables[static_cast<std::size_t>(count)].name.c_str(),
                                         par_FileDescriptor);
    }
    (void)IniFileDataBaseWriteInt(par_Section, "VariableCount", count, par_FileDescriptor);

    // Remove stale Variable<n> entries left from a previously longer list.
    for (int i{count};; ++i)
    {
        char entry[32];
        std::snprintf(entry, sizeof(entry), "Variable%d", i);
        char existing[BBVARI_NAME_SIZE];
        if (0 == IniFileDataBaseReadString(par_Section, entry, "", existing, sizeof(existing),
                                           par_FileDescriptor))
        {
            break;
        }
        (void)IniFileDataBaseWriteString(par_Section, entry, nullptr, par_FileDescriptor);
    }
}

void EnumWindow::ReadConfig(int par_FileDescriptor, const char *par_Section)
{
    for (EnumVariable &variable : m_variables)
    {
        if (0 < variable.vid)
        {
            (void)remove_bbvari(variable.vid);
        }
    }
    m_variables.clear();

    const int count{IniFileDataBaseReadInt(par_Section, "VariableCount", 0, par_FileDescriptor)};
    for (int i{0}; i < count; ++i)
    {
        char entry[32];
        std::snprintf(entry, sizeof(entry), "Variable%d", i);
        char name[BBVARI_NAME_SIZE];
        if (0 < IniFileDataBaseReadString(par_Section, entry, "", name, sizeof(name), par_FileDescriptor))
        {
            AddVariable(name);
        }
    }
}

} // namespace XilEnvImGui
