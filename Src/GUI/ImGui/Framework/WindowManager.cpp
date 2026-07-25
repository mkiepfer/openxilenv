#include "WindowManager.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include "imgui.h"

#include "WindowManagerBridge.h"
#include "WindowTypeRegistry.h"

extern "C" {
#include "IniDataBase.h"
}

namespace XilEnvImGui
{

namespace
{

constexpr const char *GUI_BASIC_SETTINGS_SECTION{"GUI/BasicSettings"};
constexpr const char *GUI_WIDGETS_SECTION_PREFIX{"GUI/Widgets/"};
constexpr const char *DEFAULT_SHEET_NAME{"Default"};
constexpr const char *DEFAULT_PLACEMENT{"0,0,300,300,0,-1,-1"};

/**
 * @brief Builds the "GUI/Widgets/<name>" section string for a window.
 */
std::string WidgetSection(const std::string &par_Name)
{
    std::string section{GUI_WIDGETS_SECTION_PREFIX};
    section.append(par_Name);
    return section;
}

} // namespace

WindowManager &WindowManager::GetInstance()
{
    static WindowManager s_instance;
    return s_instance;
}

void WindowManager::EnsureAtLeastOneSheet()
{
    if (m_sheets.empty())
    {
        Sheet sheet;
        sheet.name = DEFAULT_SHEET_NAME;
        m_sheets.push_back(std::move(sheet));
        m_activeSheet = 0;
    }
}

int WindowManager::FindSheetIndex(const char *par_Name) const
{
    if (nullptr == par_Name)
    {
        return -1;
    }
    for (int i{0}; i < static_cast<int>(m_sheets.size()); ++i)
    {
        if (0 == m_sheets[static_cast<size_t>(i)].name.compare(par_Name))
        {
            return i;
        }
    }
    return -1;
}

std::string WindowManager::MakeUniqueName(const char *par_BaseName) const
{
    const std::string base{(nullptr != par_BaseName) ? par_BaseName : "Window"};
    if (m_windowsByName.find(base) == m_windowsByName.end())
    {
        return base;
    }
    for (int i{1}; i < 100000; ++i)
    {
        std::string candidate{base};
        candidate.append(" ").append(std::to_string(i));
        if (m_windowsByName.find(candidate) == m_windowsByName.end())
        {
            return candidate;
        }
    }
    return base;
}

IWindowInstance *WindowManager::AddWindow(std::unique_ptr<IWindowInstance> par_Window, int par_SheetIndex,
                                          const std::string &par_Placement)
{
    if (nullptr == par_Window)
    {
        return nullptr;
    }
    if ((0 > par_SheetIndex) || (par_SheetIndex >= static_cast<int>(m_sheets.size())))
    {
        return nullptr;
    }

    IWindowInstance *raw{par_Window.get()};
    m_windowsByName[raw->GetName()] = raw;
    m_windows.push_back(std::move(par_Window));

    SheetWindow sheetWindow;
    sheetWindow.instance = raw;
    sheetWindow.placement = par_Placement;
    m_sheets[static_cast<size_t>(par_SheetIndex)].windows.push_back(std::move(sheetWindow));
    return raw;
}

void WindowManager::DestroyWindow(const std::string &par_Name)
{
    auto it = m_windowsByName.find(par_Name);
    if (it == m_windowsByName.end())
    {
        return;
    }
    IWindowInstance *target{it->second};

    for (Sheet &sheet : m_sheets)
    {
        for (size_t i{0}; i < sheet.windows.size(); ++i)
        {
            if (target == sheet.windows[i].instance)
            {
                sheet.windows.erase(sheet.windows.begin() + static_cast<long>(i));
                break;
            }
        }
    }

    m_windowsByName.erase(it);
    for (size_t i{0}; i < m_windows.size(); ++i)
    {
        if (target == m_windows[i].get())
        {
            m_windows.erase(m_windows.begin() + static_cast<long>(i));
            break;
        }
    }
}

// -------------------------------------------------------------------------
// Config load / save (Qt-compatible ini section layout)
// -------------------------------------------------------------------------

void WindowManager::LoadLayout(int par_FileDescriptor)
{
    char selectedSheet[INI_MAX_LINE_LENGTH];
    if (0 >= IniFileDataBaseReadString(GUI_BASIC_SETTINGS_SECTION, "SelectedSheet", DEFAULT_SHEET_NAME,
                                       selectedSheet, sizeof(selectedSheet), par_FileDescriptor))
    {
        std::snprintf(selectedSheet, sizeof(selectedSheet), "%s", DEFAULT_SHEET_NAME);
    }

    for (int sheetIdx{0};; ++sheetIdx)
    {
        char section[INI_MAX_SECTION_LENGTH];
        std::snprintf(section, sizeof(section), "GUI/OpenWindowsForSheet%d", sheetIdx);

        char sheetName[INI_MAX_LINE_LENGTH];
        if (0 == sheetIdx)
        {
            std::snprintf(sheetName, sizeof(sheetName), "%s", DEFAULT_SHEET_NAME);
            (void)IniFileDataBaseReadString(section, "SheetName", DEFAULT_SHEET_NAME, sheetName,
                                            sizeof(sheetName), par_FileDescriptor);
        }
        else
        {
            if (0 == IniFileDataBaseReadString(section, "SheetName", "", sheetName, sizeof(sheetName),
                                               par_FileDescriptor))
            {
                break;
            }
        }

        Sheet sheet;
        sheet.name = sheetName;
        m_sheets.push_back(std::move(sheet));
        const int currentSheet{static_cast<int>(m_sheets.size()) - 1};

        for (int w{0};; ++w)
        {
            char entry[INI_MAX_ENTRYNAME_LENGTH];
            std::snprintf(entry, sizeof(entry), "W%d", w);
            char windowName[INI_MAX_LINE_LENGTH];
            if (0 == IniFileDataBaseReadString(section, entry, "", windowName, sizeof(windowName),
                                               par_FileDescriptor))
            {
                break;
            }

            char placementEntry[INI_MAX_ENTRYNAME_LENGTH];
            std::snprintf(placementEntry, sizeof(placementEntry), "WP%d", w);
            char placement[INI_MAX_LINE_LENGTH];
            (void)IniFileDataBaseReadString(section, placementEntry, DEFAULT_PLACEMENT, placement,
                                            sizeof(placement), par_FileDescriptor);

            const std::string name{windowName};
            if (m_windowsByName.find(name) != m_windowsByName.end())
            {
                // Already opened by an earlier sheet: one instance, first sheet wins.
                continue;
            }

            const std::string widgetSection{WidgetSection(name)};
            char typeName[INI_MAX_LINE_LENGTH];
            if (0 == IniFileDataBaseReadString(widgetSection.c_str(), "type", "", typeName,
                                               sizeof(typeName), par_FileDescriptor))
            {
                continue;
            }

            IWindowType *type{WindowTypeRegistry::GetInstance().FindByTypeName(typeName)};
            if (nullptr == type)
            {
                // Unknown/unported window type (M4+); skip so the rest still loads.
                continue;
            }

            std::unique_ptr<IWindowInstance> instance{
                type->CreateFromConfig(par_FileDescriptor, widgetSection.c_str(), name)};
            (void)AddWindow(std::move(instance), currentSheet, placement);
        }
    }

    EnsureAtLeastOneSheet();

    const int selected{FindSheetIndex(selectedSheet)};
    m_activeSheet = (0 <= selected) ? selected : 0;
    m_pendingSelectSheet = m_activeSheet;
}

void WindowManager::SaveLayout(int par_FileDescriptor)
{
    // Clear every previous sheet section (mirrors Sheets::CleanIni()).
    for (int i{0};; ++i)
    {
        char section[INI_MAX_SECTION_LENGTH];
        std::snprintf(section, sizeof(section), "GUI/OpenWindowsForSheet%d", i);
        if (0 == IniFileDataBaseLookIfSectionExist(section, par_FileDescriptor))
        {
            break;
        }
        (void)IniFileDataBaseWriteString(section, nullptr, nullptr, par_FileDescriptor);
    }

    const std::string activeName{
        (m_activeSheet >= 0 && m_activeSheet < static_cast<int>(m_sheets.size()))
            ? m_sheets[static_cast<size_t>(m_activeSheet)].name
            : std::string{DEFAULT_SHEET_NAME}};
    (void)IniFileDataBaseWriteString(GUI_BASIC_SETTINGS_SECTION, "SelectedSheet", activeName.c_str(),
                                     par_FileDescriptor);

    for (int sheetIdx{0}; sheetIdx < static_cast<int>(m_sheets.size()); ++sheetIdx)
    {
        const Sheet &sheet{m_sheets[static_cast<size_t>(sheetIdx)]};
        char section[INI_MAX_SECTION_LENGTH];
        std::snprintf(section, sizeof(section), "GUI/OpenWindowsForSheet%d", sheetIdx);

        (void)IniFileDataBaseWriteString(section, nullptr, nullptr, par_FileDescriptor);
        (void)IniFileDataBaseWriteString(section, "SheetName", sheet.name.c_str(), par_FileDescriptor);

        for (int w{0}; w < static_cast<int>(sheet.windows.size()); ++w)
        {
            const SheetWindow &sheetWindow{sheet.windows[static_cast<size_t>(w)]};
            char entry[INI_MAX_ENTRYNAME_LENGTH];

            std::snprintf(entry, sizeof(entry), "W%d", w);
            (void)IniFileDataBaseWriteString(section, entry, sheetWindow.instance->GetName().c_str(),
                                             par_FileDescriptor);

            std::snprintf(entry, sizeof(entry), "WP%d", w);
            const std::string &placement{sheetWindow.placement.empty() ? std::string{DEFAULT_PLACEMENT}
                                                                       : sheetWindow.placement};
            (void)IniFileDataBaseWriteString(section, entry, placement.c_str(), par_FileDescriptor);

            const std::string widgetSection{WidgetSection(sheetWindow.instance->GetName())};
            sheetWindow.instance->WriteConfig(par_FileDescriptor, widgetSection.c_str());
        }
    }
}

// -------------------------------------------------------------------------
// Per-frame update + render
// -------------------------------------------------------------------------

void WindowManager::Update(const UpdateScheduler &par_Scheduler, const WindowContext &par_Context)
{
    if ((0 > m_activeSheet) || (m_activeSheet >= static_cast<int>(m_sheets.size())))
    {
        return;
    }
    for (const SheetWindow &sheetWindow : m_sheets[static_cast<size_t>(m_activeSheet)].windows)
    {
        IWindowType *type{sheetWindow.instance->GetType()};
        if (par_Scheduler.ShouldUpdate(type->GetUpdateRate()))
        {
            sheetWindow.instance->OnUpdateTick(par_Context);
        }
    }
}

void WindowManager::RenderNewWindowMenu()
{
    for (const std::unique_ptr<IWindowType> &type : WindowTypeRegistry::GetInstance().GetTypes())
    {
        if (ImGui::MenuItem(type->GetMenuEntryName()))
        {
            (void)CreateWindowOnActiveSheet(type.get());
        }
    }
}

IWindowInstance *WindowManager::CreateWindowOnActiveSheet(IWindowType *par_Type)
{
    if (nullptr == par_Type)
    {
        return nullptr;
    }
    EnsureAtLeastOneSheet();

    const std::string name{MakeUniqueName(par_Type->GetTypeName())};
    std::unique_ptr<IWindowInstance> instance{par_Type->CreateNew(name)};
    return AddWindow(std::move(instance), m_activeSheet, DEFAULT_PLACEMENT);
}

void WindowManager::Render(const WindowContext &par_Context)
{
    EnsureAtLeastOneSheet();

    ImGuiWindowFlags hostFlags{ImGuiWindowFlags_NoCollapse};
    if (ImGui::Begin("Workspaces", nullptr, hostFlags))
    {
        if (ImGui::BeginTabBar("##SheetTabs", ImGuiTabBarFlags_AutoSelectNewTabs))
        {
            for (int i{0}; i < static_cast<int>(m_sheets.size()); ++i)
            {
                ImGuiTabItemFlags itemFlags{ImGuiTabItemFlags_None};
                if (m_pendingSelectSheet == i)
                {
                    itemFlags |= ImGuiTabItemFlags_SetSelected;
                }
                const std::string label{m_sheets[static_cast<size_t>(i)].name + "###Sheet" + std::to_string(i)};
                if (ImGui::BeginTabItem(label.c_str(), nullptr, itemFlags))
                {
                    m_activeSheet = i;
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
        m_pendingSelectSheet = -1;

        // One dock space per sheet so each tab keeps an independent layout.
        const std::string dockName{"##Dock" + std::to_string(m_activeSheet)};
        const ImGuiID dockId{ImGui::GetID(dockName.c_str())};
        ImGui::DockSpace(dockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

        ImGui::End();

        // Render the active sheet's windows, docked into that sheet's dock space.
        std::string closeRequest;
        for (const SheetWindow &sheetWindow : m_sheets[static_cast<size_t>(m_activeSheet)].windows)
        {
            IWindowInstance *instance{sheetWindow.instance};
            const std::string title{instance->GetName() + "###" + instance->GetName()};

            ImGui::SetNextWindowDockID(dockId, ImGuiCond_FirstUseEver);
            bool open{true};
            if (ImGui::Begin(title.c_str(), &open))
            {
                instance->RenderContent(par_Context);

                if (ImGui::BeginPopupContextItem("##WindowCtx"))
                {
                    if (ImGui::BeginMenu("Settings"))
                    {
                        instance->RenderSettingsPopup();
                        ImGui::EndMenu();
                    }
                    if (ImGui::MenuItem("Close window"))
                    {
                        open = false;
                    }
                    ImGui::EndPopup();
                }
            }
            ImGui::End();

            if (!open)
            {
                closeRequest = instance->GetName();
            }
        }

        if (!closeRequest.empty())
        {
            DestroyWindow(closeRequest);
        }
    }
    else
    {
        ImGui::End();
    }
}

// -------------------------------------------------------------------------
// Name-based open/close (menu + command queue)
// -------------------------------------------------------------------------

int WindowManager::OpenWindowByName(const char *par_Name)
{
    if ((nullptr == par_Name) || ('\0' == par_Name[0]))
    {
        return 0;
    }
    const std::string name{par_Name};
    if (m_windowsByName.find(name) != m_windowsByName.end())
    {
        return 1; // Already open.
    }

    EnsureAtLeastOneSheet();

    const int fd{GetMainFileDescriptor()};
    const std::string widgetSection{WidgetSection(name)};
    char typeName[INI_MAX_LINE_LENGTH];
    if (0 == IniFileDataBaseReadString(widgetSection.c_str(), "type", "", typeName, sizeof(typeName), fd))
    {
        return 0; // No stored config for this window name.
    }

    IWindowType *type{WindowTypeRegistry::GetInstance().FindByTypeName(typeName)};
    if (nullptr == type)
    {
        return 0;
    }

    std::unique_ptr<IWindowInstance> instance{type->CreateFromConfig(fd, widgetSection.c_str(), name)};
    return (nullptr != AddWindow(std::move(instance), m_activeSheet, DEFAULT_PLACEMENT)) ? 1 : 0;
}

int WindowManager::CloseWindowByName(const char *par_Name)
{
    if ((nullptr == par_Name) || ('\0' == par_Name[0]))
    {
        return 0;
    }
    const std::string name{par_Name};
    if (m_windowsByName.find(name) == m_windowsByName.end())
    {
        return 0;
    }
    DestroyWindow(name);
    return 1;
}

int WindowManager::IsWindowOpen(const char *par_Name) const
{
    if (nullptr == par_Name)
    {
        return 0;
    }
    return (m_windowsByName.find(std::string{par_Name}) != m_windowsByName.end()) ? 1 : 0;
}

bool WindowManager::WindowNameAlreadyInUse(const char *par_Name) const
{
    return (0 != IsWindowOpen(par_Name));
}

int WindowManager::SelectSheet(const char *par_Name)
{
    const int idx{FindSheetIndex(par_Name)};
    if (0 > idx)
    {
        return 0;
    }
    m_activeSheet = idx;
    m_pendingSelectSheet = idx;
    return 1;
}

int WindowManager::AddSheet(const char *par_Name)
{
    if ((nullptr == par_Name) || ('\0' == par_Name[0]))
    {
        return 0;
    }
    if (0 <= FindSheetIndex(par_Name))
    {
        return 1; // Already exists.
    }
    Sheet sheet;
    sheet.name = par_Name;
    m_sheets.push_back(std::move(sheet));
    return 1;
}

int WindowManager::DeleteSheet(const char *par_Name)
{
    const int idx{FindSheetIndex(par_Name)};
    if (0 > idx)
    {
        return 0;
    }
    if (1 >= static_cast<int>(m_sheets.size()))
    {
        return 0; // Keep at least one sheet.
    }

    // Destroy the windows owned by that sheet.
    std::vector<std::string> names;
    names.reserve(m_sheets[static_cast<size_t>(idx)].windows.size());
    for (const SheetWindow &sheetWindow : m_sheets[static_cast<size_t>(idx)].windows)
    {
        names.push_back(sheetWindow.instance->GetName());
    }
    for (const std::string &name : names)
    {
        DestroyWindow(name);
    }

    m_sheets.erase(m_sheets.begin() + idx);
    if (m_activeSheet >= static_cast<int>(m_sheets.size()))
    {
        m_activeSheet = static_cast<int>(m_sheets.size()) - 1;
    }
    m_pendingSelectSheet = m_activeSheet;
    return 1;
}

int WindowManager::RenameSheet(const char *par_OldName, const char *par_NewName)
{
    const int idx{FindSheetIndex(par_OldName)};
    if ((0 > idx) || (nullptr == par_NewName) || ('\0' == par_NewName[0]))
    {
        return 0;
    }
    m_sheets[static_cast<size_t>(idx)].name = par_NewName;
    return 1;
}

int WindowManager::RenameWindow(const char *par_OldName, const char *par_NewName)
{
    if ((nullptr == par_OldName) || (nullptr == par_NewName) || ('\0' == par_NewName[0]))
    {
        return 0;
    }
    const std::string oldName{par_OldName};
    const std::string newName{par_NewName};
    if (oldName == newName)
    {
        return 1; // nothing to do
    }
    if (m_windowsByName.find(newName) != m_windowsByName.end())
    {
        return 0; // target name already in use
    }
    auto it = m_windowsByName.find(oldName);
    if (m_windowsByName.end() == it)
    {
        return 0;
    }
    IWindowInstance *instance{it->second};
    m_windowsByName.erase(it);
    instance->SetName(newName);
    m_windowsByName[newName] = instance;
    return 1;
}

// -------------------------------------------------------------------------
// Bridge free functions used by the command-queue seam.
// -------------------------------------------------------------------------

int WmOpenWindowByName(const char *par_Name)
{
    return WindowManager::GetInstance().OpenWindowByName(par_Name);
}

int WmCloseWindowByName(const char *par_Name)
{
    return WindowManager::GetInstance().CloseWindowByName(par_Name);
}

int WmIsWindowOpen(const char *par_Name)
{
    return WindowManager::GetInstance().IsWindowOpen(par_Name);
}

int WmSelectSheet(const char *par_Name)
{
    return WindowManager::GetInstance().SelectSheet(par_Name);
}

int WmAddSheet(const char *par_Name)
{
    return WindowManager::GetInstance().AddSheet(par_Name);
}

int WmDeleteSheet(const char *par_Name)
{
    return WindowManager::GetInstance().DeleteSheet(par_Name);
}

int WmRenameSheet(const char *par_OldName, const char *par_NewName)
{
    return WindowManager::GetInstance().RenameSheet(par_OldName, par_NewName);
}

int WmRenameWindow(const char *par_OldName, const char *par_NewName)
{
    return WindowManager::GetInstance().RenameWindow(par_OldName, par_NewName);
}

} // namespace XilEnvImGui
