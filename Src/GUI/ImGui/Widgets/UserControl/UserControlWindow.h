/**
 * @file Src/GUI/ImGui/Widgets/UserControl/UserControlWindow.h
 * @brief ImGui port of the Qt UserControl window: a config-defined interactive control panel.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_WIDGETS_USERCONTROL_USERCONTROLWINDOW_H
#define SRC_GUI_IMGUI_WIDGETS_USERCONTROL_USERCONTROLWINDOW_H

#include <memory>
#include <string>

#include "Framework/IWindowInstance.h"
#include "Framework/IWindowType.h"
#include "Framework/WindowContext.h"
#include "Widgets/UserControl/UserControlElements.h"

namespace XilEnvImGui
{

/**
 * @class UserControlWindowType
 * @brief Window type/factory for UserControl windows (mirrors the Qt UserControlType, Medium update).
 */
class UserControlWindowType : public IWindowType
{
public:
    [[nodiscard]] UpdateRate GetUpdateRate() const override
    {
        return UpdateRate::Medium;
    }
    [[nodiscard]] const char *GetTypeName() const override
    {
        // Persisted "type=" string; must equal the Qt UserControlType WindowTypeName so a window
        // saved by either GUI dispatches to the right loader.
        return "UserControl";
    }
    [[nodiscard]] const char *GetIniListName() const override
    {
        return "GUI/AllUserControlWindows";
    }
    [[nodiscard]] const char *GetMenuEntryName() const override
    {
        return "User control";
    }
    [[nodiscard]] bool HasStorage() const override
    {
        return false;
    }
    [[nodiscard]] std::unique_ptr<IWindowInstance> CreateNew(const std::string &par_Title) override;
    [[nodiscard]] std::unique_ptr<IWindowInstance> CreateFromConfig(int par_FileDescriptor,
                                                                    const char *par_Section,
                                                                    const std::string &par_Name) override;
};

/**
 * @class UserControlWindow
 * @brief One UserControl panel: owns a scene graph of interactive controls laid out in a grid.
 * @details Reproduces the Qt UserControlWidget: the config defines a tree of Root/Group containers
 *          and Button controls. Buttons write to the blackboard on click (via direct_solve_equation)
 *          and/or start a script; all buttons are disabled while a script is running. Config
 *          round-trips verbatim to the Qt "GUI/Widgets/<name>" section (x_0, x_1, ... entries).
 */
class UserControlWindow : public IWindowInstance
{
public:
    /**
     * @brief Constructs a UserControl window bound to its type and name.
     * @param[in] par_Type Owning window type. Must not be nullptr.
     * @param[in] par_Name Unique window name.
     */
    UserControlWindow(IWindowType *par_Type, std::string par_Name);
    ~UserControlWindow() override = default;

    [[nodiscard]] IWindowType *GetType() const override
    {
        return m_pType;
    }
    void RenderContent(const WindowContext &par_Context) override;
    void OnUpdateTick(const WindowContext &par_Context) override;
    void RenderSettingsPopup() override;
    void WriteConfig(int par_FileDescriptor, const char *par_Section) override;
    void ReadConfig(int par_FileDescriptor, const char *par_Section) override;
    void OnAcceptVariableDrop(const std::string &par_VariableName) override;

private:
    IWindowType *m_pType{nullptr};              /*!< Owning window type. */
    std::unique_ptr<UserControlRoot> m_root{};  /*!< Scene-graph root. */
    bool m_scriptRunning{false};                /*!< Cached script-running state (disables buttons). */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_WIDGETS_USERCONTROL_USERCONTROLWINDOW_H
