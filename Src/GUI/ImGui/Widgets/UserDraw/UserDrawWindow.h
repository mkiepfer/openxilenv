/**
 * @file Src/GUI/ImGui/Widgets/UserDraw/UserDrawWindow.h
 * @brief ImGui port of the Qt UserDraw window: a config-defined 2D drawing surface.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_WIDGETS_USERDRAW_USERDRAWWINDOW_H
#define SRC_GUI_IMGUI_WIDGETS_USERDRAW_USERDRAWWINDOW_H

#include <memory>
#include <string>

#include "Framework/IWindowInstance.h"
#include "Framework/IWindowType.h"
#include "Framework/WindowContext.h"
#include "Widgets/UserDraw/UserDrawElements.h"

namespace XilEnvImGui
{

/**
 * @class UserDrawWindowType
 * @brief Window type/factory for UserDraw windows (mirrors the Qt UserDrawType, Fast update).
 */
class UserDrawWindowType : public IWindowType
{
public:
    [[nodiscard]] UpdateRate GetUpdateRate() const override
    {
        return UpdateRate::Fast;
    }
    [[nodiscard]] const char *GetTypeName() const override
    {
        // Persisted "type=" string; must equal the Qt UserDrawType WindowTypeName so a window
        // saved by either GUI dispatches to the right loader.
        return "UserDraw";
    }
    [[nodiscard]] const char *GetIniListName() const override
    {
        return "GUI/AllUserDrawWindows";
    }
    [[nodiscard]] const char *GetMenuEntryName() const override
    {
        return "User draw";
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
 * @class UserDrawWindow
 * @brief One UserDraw surface: owns a scene graph re-evaluated and drawn every frame.
 * @details Reproduces the Qt UserDrawWidget: the config defines a tree of drawable elements
 *          whose geometry/color/visibility are equations over blackboard variables, drawn via
 *          ImDrawList in a logical coordinate system mapped to the content rect (Y-up, origin
 *          bottom-left). Config round-trips verbatim to the Qt "GUI/Widgets/<name>" section.
 */
class UserDrawWindow : public IWindowInstance
{
public:
    /**
     * @brief Constructs a UserDraw window bound to its type and name.
     * @param[in] par_Type Owning window type. Must not be nullptr.
     * @param[in] par_Name Unique window name.
     */
    UserDrawWindow(IWindowType *par_Type, std::string par_Name);
    ~UserDrawWindow() override = default;

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
    IWindowType *m_pType{nullptr};             /*!< Owning window type. */
    std::unique_ptr<UserDrawRoot> m_root{};    /*!< Scene-graph root. */
    bool m_fixedRatio{false};                  /*!< Keep a 1:1 aspect ratio. */
    bool m_antialiasing{false};                /*!< Antialiasing hint (ImGui always antialiased). */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_WIDGETS_USERDRAW_USERDRAWWINDOW_H
