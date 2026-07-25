#include "Widgets/VariableList/VariableListWindow.h"

#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "imgui.h"

#include "Framework/BlackboardDragDrop.h"
#include "Framework/GuiComponents.h"

extern "C" {
#include "Blackboard.h"
#include "IniDataBase.h"
}

namespace XilEnvImGui
{

namespace
{

/**
 * @brief Case-insensitive substring test for the row filter.
 */
bool MatchesFilter(const std::string &par_Name, const std::string &par_Filter)
{
    if (par_Filter.empty())
    {
        return true;
    }
    std::string name{par_Name};
    std::string filter{par_Filter};
    for (char &c : name)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (char &c : filter)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return (std::string::npos != name.find(filter));
}

} // namespace

std::unique_ptr<IWindowInstance> VariableListWindowType::CreateNew(const std::string &par_Title)
{
    return std::make_unique<VariableListWindow>(this, par_Title);
}

std::unique_ptr<IWindowInstance> VariableListWindowType::CreateFromConfig(int par_FileDescriptor,
                                                                          const char *par_Section,
                                                                          const std::string &par_Name)
{
    auto window = std::make_unique<VariableListWindow>(this, par_Name);
    window->ReadConfig(par_FileDescriptor, par_Section);
    return window;
}

VariableListWindow::VariableListWindow(IWindowType *par_Type, std::string par_Name) : m_pType{par_Type}
{
    SetName(std::move(par_Name));
    EnumerateBlackboardVariables(m_names);
}

void VariableListWindow::OnUpdateTick(const WindowContext &par_Context)
{
    (void)par_Context;
    EnumerateBlackboardVariables(m_names);
}

void VariableListWindow::OnAcceptVariableDrop(const std::string &par_VariableName)
{
    // A pure source window; dropping onto it is a no-op.
    (void)par_VariableName;
}

void VariableListWindow::RenderContent(const WindowContext &par_Context)
{
    (void)par_Context;

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "filter...", m_filterBuffer, sizeof(m_filterBuffer));

    const std::string filter{m_filterBuffer};

    if (ImGui::BeginChild("##varlist", ImGui::GetContentRegionAvail(), false))
    {
        int rowId{0};
        for (const std::string &name : m_names)
        {
            if (!MatchesFilter(name, filter))
            {
                continue;
            }
            ImGui::PushID(rowId++);
            ImGui::Selectable(name.c_str());
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
            {
                // Attach to fetch min/max/color, then detach immediately so the
                // list itself never keeps a reference on the variable.
                const VID vid{add_bbvari(name.c_str(), BB_UNKNOWN_WAIT, nullptr)};
                const BlackboardVariablePayload payload{MakeBlackboardVariablePayloadFromVid(
                    name, (0 < vid) ? vid : 0, DragDropDisplayMode::Unknown)};
                SetBlackboardVariableDragPayload(payload);
                if (0 < vid)
                {
                    (void)remove_bbvari(vid);
                }
                ImGui::EndDragDropSource();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

void VariableListWindow::RenderSettingsPopup()
{
    ImGui::TextUnformatted("Filter:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("##filtercfg", m_filterBuffer, sizeof(m_filterBuffer));
    ImGui::Text("Variables: %d", static_cast<int>(m_names.size()));
    if (ImGui::Button("Refresh"))
    {
        EnumerateBlackboardVariables(m_names);
    }
}

void VariableListWindow::WriteConfig(int par_FileDescriptor, const char *par_Section)
{
    (void)IniFileDataBaseWriteString(par_Section, "type", m_pType->GetTypeName(), par_FileDescriptor);
    (void)IniFileDataBaseWriteString(par_Section, "Filter", m_filterBuffer, par_FileDescriptor);
}

void VariableListWindow::ReadConfig(int par_FileDescriptor, const char *par_Section)
{
    char filter[256];
    if (0 < IniFileDataBaseReadString(par_Section, "Filter", "", filter, sizeof(filter), par_FileDescriptor))
    {
        std::snprintf(m_filterBuffer, sizeof(m_filterBuffer), "%s", filter);
    }
    EnumerateBlackboardVariables(m_names);
}

} // namespace XilEnvImGui
