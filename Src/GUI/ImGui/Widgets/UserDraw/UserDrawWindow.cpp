#include "Widgets/UserDraw/UserDrawWindow.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "imgui.h"

#include "Widgets/UserDraw/UserDrawElement.h"
#include "Widgets/UserDraw/UserDrawParser.h"

extern "C" {
#include "IniDataBase.h"
}

namespace XilEnvImGui
{

std::unique_ptr<IWindowInstance> UserDrawWindowType::CreateNew(const std::string &par_Title)
{
    return std::make_unique<UserDrawWindow>(this, par_Title);
}

std::unique_ptr<IWindowInstance> UserDrawWindowType::CreateFromConfig(int par_FileDescriptor,
                                                                      const char *par_Section,
                                                                      const std::string &par_Name)
{
    auto window = std::make_unique<UserDrawWindow>(this, par_Name);
    window->ReadConfig(par_FileDescriptor, par_Section);
    return window;
}

UserDrawWindow::UserDrawWindow(IWindowType *par_Type, std::string par_Name) : m_pType{par_Type}
{
    SetName(std::move(par_Name));
    UserDrawParser parser;
    m_root = parser.Parse("", GetMainFileDescriptor()); // empty tree until ReadConfig()
}

void UserDrawWindow::OnUpdateTick(const WindowContext &par_Context)
{
    // Equations are re-evaluated directly in RenderContent every frame (Fast update);
    // there is no separate cached-value refresh step.
    (void)par_Context;
}

void UserDrawWindow::OnAcceptVariableDrop(const std::string &par_VariableName)
{
    // UserDraw is entirely config-driven and has no single-variable binding (the Qt widget's
    // changeVariable slot is likewise a no-op).
    (void)par_VariableName;
}

void UserDrawWindow::RenderContent(const WindowContext &par_Context)
{
    (void)par_Context;

    const ImVec2 avail{ImGui::GetContentRegionAvail()};
    const ImVec2 origin{ImGui::GetCursorScreenPos()};

    float width{avail.x};
    float height{avail.y};
    if (m_fixedRatio)
    {
        if (width < height)
        {
            height = width;
        }
        else if (height < width)
        {
            width = height;
        }
    }

    if ((width > 0.0f) && (height > 0.0f) && (nullptr != m_root))
    {
        ImDrawList *drawList{ImGui::GetWindowDrawList()};
        drawList->PushClipRect(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), true);
        DrawParameter draw{0.0, 0.0, 1.0, 0.0, static_cast<double>(width), static_cast<double>(height)};
        m_root->Render(drawList, draw, origin);
        drawList->PopClipRect();
    }

    // Reserve the content rect so docking/resize behave and the surface stays interactive.
    ImGui::InvisibleButton("##userdraw", ImVec2(std::max(avail.x, 1.0f), std::max(avail.y, 1.0f)));
}

void UserDrawWindow::RenderSettingsPopup()
{
    ImGui::Checkbox("Fixed ratio", &m_fixedRatio);
    if (nullptr != m_root)
    {
        m_root->m_fixedRatio = m_fixedRatio;
    }
    ImGui::Checkbox("Antialiasing", &m_antialiasing);
    ImGui::Separator();
    ImGui::TextDisabled("Scene is defined in the configuration file.");
}

void UserDrawWindow::WriteConfig(int par_FileDescriptor, const char *par_Section)
{
    // Never clear the section: non-rendered Qt keys (Image_*, deferred elements) survive.
    (void)IniFileDataBaseWriteString(par_Section, "type", m_pType->GetTypeName(), par_FileDescriptor);
    (void)IniFileDataBaseWriteYesNo(par_Section, "FixedRatio", m_fixedRatio ? 1 : 0, par_FileDescriptor);
    (void)IniFileDataBaseWriteYesNo(par_Section, "Antialiasing", m_antialiasing ? 1 : 0,
                                    par_FileDescriptor);

    if (nullptr != m_root)
    {
        for (int index{0}; index < m_root->GetChildCount(); ++index)
        {
            UserDrawElement *child{m_root->GetChild(index)};
            if (nullptr != child)
            {
                std::string entry{"x_"};
                entry.append(std::to_string(index));
                child->WriteToIni(par_Section, entry, par_FileDescriptor);
            }
        }
    }
}

void UserDrawWindow::ReadConfig(int par_FileDescriptor, const char *par_Section)
{
    m_fixedRatio = (0 != IniFileDataBaseReadYesNo(par_Section, "FixedRatio", 0, par_FileDescriptor));
    m_antialiasing = (0 != IniFileDataBaseReadYesNo(par_Section, "Antialiasing", 0, par_FileDescriptor));

    UserDrawParser parser;
    m_root = parser.Parse(par_Section, par_FileDescriptor);
    m_root->m_fixedRatio = m_fixedRatio;
    m_root->m_antialiasing = m_antialiasing;
}

} // namespace XilEnvImGui
