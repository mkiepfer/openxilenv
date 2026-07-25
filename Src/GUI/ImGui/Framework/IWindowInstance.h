/**
 * @file Src/GUI/ImGui/Framework/IWindowInstance.h
 * @brief Abstract instance of an open ImGui window (replaces the Qt InterfaceWidgetElement).
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_FRAMEWORK_IWINDOWINSTANCE_H
#define SRC_GUI_IMGUI_FRAMEWORK_IWINDOWINSTANCE_H

#include <string>
#include <utility>

#include "WindowContext.h"

namespace XilEnvImGui
{

class IWindowType;

/**
 * @class IWindowInstance
 * @brief One live window shown inside a sheet's dock space.
 * @details Immediate-mode counterpart of the Qt MdiWindowWidget. The WindowManager
 *          owns each instance, calls RenderContent() between ImGui::Begin/End every
 *          frame and OnUpdateTick() at the type's configured update rate. Config is
 *          persisted through the IniDataBase facade (routed to layout.toml for
 *          "GUI/" sections).
 * @warning All methods are called on the render thread only; instances are not
 *          thread-safe.
 */
class IWindowInstance
{
public:
    virtual ~IWindowInstance() = default;

    /**
     * @brief Returns the window type that created this instance.
     * @return Non-owning pointer to the owning IWindowType. Never nullptr.
     */
    [[nodiscard]] virtual IWindowType *GetType() const = 0;

    /**
     * @brief Returns the unique window name (also used as the ImGui "###" identity).
     * @return Const reference to the window name.
     */
    [[nodiscard]] const std::string &GetName() const
    {
        return m_name;
    }

    /**
     * @brief Sets the unique window name.
     * @param[in] par_Name New window name. Must be unique among all open windows.
     */
    void SetName(std::string par_Name)
    {
        m_name = std::move(par_Name);
    }

    /**
     * @brief Draws the window body; called between ImGui::Begin and ImGui::End.
     * @param[in] par_Context Per-frame render context.
     */
    virtual void RenderContent(const WindowContext &par_Context) = 0;

    /**
     * @brief Refreshes cached signal values at the type's update rate.
     * @param[in] par_Context Per-frame update context.
     */
    virtual void OnUpdateTick(const WindowContext &par_Context) = 0;

    /**
     * @brief Draws the per-window settings UI inside an already-open popup.
     */
    virtual void RenderSettingsPopup() = 0;

    /**
     * @brief Writes the window configuration into the given ini section.
     * @param[in] par_FileDescriptor Ini-database descriptor (main descriptor).
     * @param[in] par_Section Section name, e.g. "GUI/Widgets/<name>". Must not be nullptr.
     */
    virtual void WriteConfig(int par_FileDescriptor, const char *par_Section) = 0;

    /**
     * @brief Reads the window configuration from the given ini section.
     * @param[in] par_FileDescriptor Ini-database descriptor (main descriptor).
     * @param[in] par_Section Section name, e.g. "GUI/Widgets/<name>". Must not be nullptr.
     */
    virtual void ReadConfig(int par_FileDescriptor, const char *par_Section) = 0;

    /**
     * @brief Handles a blackboard variable dropped onto the window.
     * @param[in] par_VariableName Name of the dropped blackboard variable.
     */
    virtual void OnAcceptVariableDrop(const std::string &par_VariableName) = 0;

protected:
    std::string m_name{}; /*!< Unique window name / ImGui identity suffix. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_FRAMEWORK_IWINDOWINSTANCE_H
