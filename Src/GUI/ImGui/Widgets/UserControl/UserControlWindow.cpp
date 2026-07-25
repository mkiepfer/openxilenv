#include "Widgets/UserControl/UserControlWindow.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "imgui.h"

#include "Widgets/UserControl/UserControlElement.h"
#include "Widgets/UserControl/UserControlParser.h"

extern "C" {
#include "IniDataBase.h"
#include "InterfaceToScript.h"
}

namespace XilEnvImGui
{

std::unique_ptr<IWindowInstance> UserControlWindowType::CreateNew(const std::string &par_Title)
{
    return std::make_unique<UserControlWindow>(this, par_Title);
}

std::unique_ptr<IWindowInstance> UserControlWindowType::CreateFromConfig(int par_FileDescriptor,
                                                                         const char *par_Section,
                                                                         const std::string &par_Name)
{
    auto window = std::make_unique<UserControlWindow>(this, par_Name);
    window->ReadConfig(par_FileDescriptor, par_Section);
    return window;
}

UserControlWindow::UserControlWindow(IWindowType *par_Type, std::string par_Name) : m_pType{par_Type}
{
    SetName(std::move(par_Name));
    UserControlParser parser;
    m_root = parser.Parse("", GetMainFileDescriptor()); // empty tree until ReadConfig()
}

void UserControlWindow::OnUpdateTick(const WindowContext &par_Context)
{
    (void)par_Context;
    // Mirror the Qt CyclicUpdate: buttons are disabled while a script is running.
    m_scriptRunning = (0 != GetScriptState());
}

void UserControlWindow::OnAcceptVariableDrop(const std::string &par_VariableName)
{
    // The Qt UserControlWidget dropEvent ignores the drop (it only pre-checks enum variables in
    // dragEnter but never binds them), so there is no drop-driven behaviour to reproduce.
    (void)par_VariableName;
}

void UserControlWindow::RenderContent(const WindowContext &par_Context)
{
    (void)par_Context;

    const ImVec2 origin{ImGui::GetCursorScreenPos()};
    const ImVec2 avail{ImGui::GetContentRegionAvail()};

    // Reserve the whole content rectangle so docking/resize behave; buttons are then positioned
    // absolutely on top via SetCursorScreenPos.
    ImGui::Dummy(ImVec2(std::max(avail.x, 1.0f), std::max(avail.y, 1.0f)));

    if ((avail.x > 0.0f) && (avail.y > 0.0f) && (nullptr != m_root))
    {
        m_root->Render(origin, avail, m_scriptRunning);
    }
}

void UserControlWindow::RenderSettingsPopup()
{
    ImGui::TextDisabled("Controls are defined in the configuration file.");
}

void UserControlWindow::WriteConfig(int par_FileDescriptor, const char *par_Section)
{
    // Never clear the section: the type key plus verbatim element lines round-trip Qt-identically.
    (void)IniFileDataBaseWriteString(par_Section, "type", m_pType->GetTypeName(), par_FileDescriptor);

    if (nullptr != m_root)
    {
        for (int index{0}; index < m_root->GetChildCount(); ++index)
        {
            UserControlElement *child{m_root->GetChild(index)};
            if (nullptr != child)
            {
                std::string entry{"x_"};
                entry.append(std::to_string(index));
                child->WriteToIni(par_Section, entry, par_FileDescriptor);
            }
        }
    }
}

void UserControlWindow::ReadConfig(int par_FileDescriptor, const char *par_Section)
{
    UserControlParser parser;
    m_root = parser.Parse(par_Section, par_FileDescriptor);
}

} // namespace XilEnvImGui
