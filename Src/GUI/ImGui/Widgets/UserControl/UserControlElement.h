/**
 * @file Src/GUI/ImGui/Widgets/UserControl/UserControlElement.h
 * @brief Scene-graph base element for the ImGui UserControl port: static grid geometry,
 *        verbatim parameter round-trip and the recursive ini write.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_WIDGETS_USERCONTROL_USERCONTROLELEMENT_H
#define SRC_GUI_IMGUI_WIDGETS_USERCONTROL_USERCONTROLELEMENT_H

#include <cstdint>
#include <memory>
#include <string>

#include "imgui.h"

namespace XilEnvImGui
{

/**
 * @brief Case-insensitive ASCII string equality (mirrors Qt::CaseInsensitive compares).
 * @param[in] par_Value Left operand.
 * @param[in] par_Literal Right operand (ASCII literal). Must not be nullptr.
 * @return true if the strings are equal ignoring ASCII case.
 */
[[nodiscard]] bool UserControlIEqualsAscii(const std::string &par_Value, const char *par_Literal);

/**
 * @class UserControlElement
 * @brief Abstract scene-graph node of an interactive UserControl scene.
 * @details Faithful port of the Qt UserControlElement. Unlike UserDraw, UserControl geometry
 *          (x, y, width, height) are plain integer grid coordinates for a uniform cell layout
 *          (x = column, y = row, width = column span, height = row span); they are never
 *          equation-bound. The raw "(...)" parameter tail is kept verbatim for a byte-identical
 *          config round-trip. Concrete elements implement Render()/ParseOneParameter().
 */
class UserControlElement
{
public:
    virtual ~UserControlElement() = default;

    /**
     * @brief Draws this element into the given screen rectangle.
     * @param[in] par_Origin Top-left of the element rectangle in screen space.
     * @param[in] par_Size Size of the element rectangle in pixels.
     * @param[in] par_ScriptRunning true while a script is running (interactive controls disabled).
     */
    virtual void Render(const ImVec2 &par_Origin, const ImVec2 &par_Size, bool par_ScriptRunning) = 0;

    /**
     * @brief Parses one element-specific "key=value" parameter token.
     * @param[in] par_Value The parameter token.
     * @return true if the parameter was recognized.
     */
    virtual bool ParseOneParameter(const std::string &par_Value) = 0;

    /**
     * @brief Persisted type keyword (e.g. "Button", "Group", "Root").
     * @return Type keyword. Never nullptr.
     */
    [[nodiscard]] virtual const char *GetTypeString() const = 0;

    /**
     * @brief Whether this element is a container (Root/Group).
     * @return true for group-like elements.
     */
    [[nodiscard]] virtual bool IsGroup() const
    {
        return false;
    }

    /**
     * @brief Appends a child (group-like elements only).
     * @param[in] par_Child Child to take ownership of.
     */
    virtual void AddChild(std::unique_ptr<UserControlElement> par_Child)
    {
        (void)par_Child;
    }

    /**
     * @brief Number of direct children.
     * @return Child count (0 for leaf elements).
     */
    [[nodiscard]] virtual int GetChildCount() const
    {
        return 0;
    }

    /**
     * @brief Returns a child by index.
     * @param[in] par_Index Child index.
     * @return Non-owning child pointer or nullptr.
     */
    [[nodiscard]] virtual UserControlElement *GetChild(int par_Index) const
    {
        (void)par_Index;
        return nullptr;
    }

    /**
     * @brief Resets all base properties to their defaults (matches Qt ResetToDefault).
     */
    virtual void ResetToDefault();

    /**
     * @brief Writes this element and its subtree to the ini section (verbatim round-trip).
     * @param[in] par_Section Window section (e.g. "GUI/Widgets/<name>"). Must not be nullptr.
     * @param[in] par_Entry Entry name for this element (e.g. "x_0").
     * @param[in] par_FileDescriptor Ini-database descriptor.
     */
    void WriteToIni(const char *par_Section, const std::string &par_Entry, int par_FileDescriptor);

    /**
     * @brief Sets the raw parameter line (the "(...)" tail) used for config round-trip.
     * @param[in] par_Line Raw parameter line.
     */
    void SetParameterLine(std::string par_Line)
    {
        m_parameterLine = std::move(par_Line);
    }

    [[nodiscard]] int GetX() const
    {
        return m_x;
    }
    [[nodiscard]] int GetY() const
    {
        return m_y;
    }
    [[nodiscard]] int GetWidth() const
    {
        return m_width;
    }
    [[nodiscard]] int GetHeight() const
    {
        return m_height;
    }

protected:
    /**
     * @brief Parses one of the shared base properties (x, y, width, height, color).
     * @param[in] par_Key Property key.
     * @param[in] par_Value Value string (integer literal).
     * @return true if the key matched a base property.
     */
    bool ParseOneBaseParameter(const std::string &par_Key, const std::string &par_Value);

    std::string m_parameterLine{}; /*!< Raw "(...)" parameter tail for round-trip. */

    int m_color{0};   /*!< Optional color property (parsed for round-trip). */
    int m_x{0};       /*!< Grid column of this element. */
    int m_y{0};       /*!< Grid row of this element. */
    int m_width{1};   /*!< Column span of this element. */
    int m_height{1};  /*!< Row span of this element. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_WIDGETS_USERCONTROL_USERCONTROLELEMENT_H
