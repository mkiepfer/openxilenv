/**
 * @file Src/GUI/ImGui/Widgets/UserControl/UserControlElements.h
 * @brief Concrete UserControl scene-graph elements (Root/Group containers + interactive Button)
 *        for the ImGui UserControl port.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_WIDGETS_USERCONTROL_USERCONTROLELEMENTS_H
#define SRC_GUI_IMGUI_WIDGETS_USERCONTROL_USERCONTROLELEMENTS_H

#include <memory>
#include <string>
#include <vector>

#include "imgui.h"

#include "Widgets/UserControl/UserControlElement.h"

namespace XilEnvImGui
{

/**
 * @class UserControlContainer
 * @brief Shared base for the group-like elements (Root, Group).
 * @details Owns an ordered list of child elements and lays them out in a uniform grid whose
 *          cell count is derived from the children's grid coordinates (mirrors QGridLayout:
 *          column = x, row = y, column span = width, row span = height).
 */
class UserControlContainer : public UserControlElement
{
public:
    [[nodiscard]] bool IsGroup() const override
    {
        return true;
    }
    void AddChild(std::unique_ptr<UserControlElement> par_Child) override;
    [[nodiscard]] int GetChildCount() const override;
    [[nodiscard]] UserControlElement *GetChild(int par_Index) const override;

protected:
    /**
     * @brief Lays out and renders all children in a uniform grid within the given rectangle.
     * @param[in] par_Origin Top-left of the layout rectangle in screen space.
     * @param[in] par_Size Size of the layout rectangle in pixels.
     * @param[in] par_ScriptRunning true while a script is running.
     */
    void RenderChildrenGrid(const ImVec2 &par_Origin, const ImVec2 &par_Size, bool par_ScriptRunning);

    std::vector<std::unique_ptr<UserControlElement>> m_children{}; /*!< Owned child elements. */
};

/**
 * @class UserControlRoot
 * @brief Invisible top-level container; lays its children across the whole content rect.
 */
class UserControlRoot : public UserControlContainer
{
public:
    void Render(const ImVec2 &par_Origin, const ImVec2 &par_Size, bool par_ScriptRunning) override;
    bool ParseOneParameter(const std::string &par_Value) override;
    [[nodiscard]] const char *GetTypeString() const override
    {
        return "Root";
    }
    /**
     * @brief Detects a "Root" line prefix.
     * @param[in] par_Line Config line.
     * @return Length of the matched keyword or 0.
     */
    [[nodiscard]] static int Is(const std::string &par_Line);
};

/**
 * @class UserControlGroup
 * @brief Titled, bordered container (port of the Qt QGroupBox) holding a child sub-grid.
 */
class UserControlGroup : public UserControlContainer
{
public:
    void ResetToDefault() override;
    void Render(const ImVec2 &par_Origin, const ImVec2 &par_Size, bool par_ScriptRunning) override;
    bool ParseOneParameter(const std::string &par_Value) override;
    [[nodiscard]] const char *GetTypeString() const override
    {
        return "Group";
    }
    [[nodiscard]] static int Is(const std::string &par_Line);

private:
    std::string m_text{}; /*!< Group box title. */
};

/**
 * @class UserControlButton
 * @brief Interactive push-button; on click starts a script and/or executes a blackboard equation.
 * @details Faithful port of the Qt UserControlButton. On click it starts the configured script
 *          via StartScript() and, if an equation is configured, writes to the blackboard via
 *          direct_solve_equation() - the exact core call the Qt ButtonPressed() slot uses. The
 *          button is disabled while a script is running (Qt ScriptStateChanged).
 */
class UserControlButton : public UserControlElement
{
public:
    void ResetToDefault() override;
    void Render(const ImVec2 &par_Origin, const ImVec2 &par_Size, bool par_ScriptRunning) override;
    bool ParseOneParameter(const std::string &par_Value) override;
    [[nodiscard]] const char *GetTypeString() const override
    {
        return "Button";
    }
    [[nodiscard]] static int Is(const std::string &par_Line);

private:
    /**
     * @brief Fires the configured script/equation actions (the blackboard write path).
     */
    void OnClick();

    std::string m_text{};     /*!< Button label. */
    std::string m_script{};   /*!< Script started on click (empty = none). */
    std::string m_equation{}; /*!< Equation solved on click (blackboard write; empty = none). */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_WIDGETS_USERCONTROL_USERCONTROLELEMENTS_H
