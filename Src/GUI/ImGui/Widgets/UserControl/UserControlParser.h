/**
 * @file Src/GUI/ImGui/Widgets/UserControl/UserControlParser.h
 * @brief Builds the UserControl scene graph from the ini config (ImGui UserControl port).
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_WIDGETS_USERCONTROL_USERCONTROLPARSER_H
#define SRC_GUI_IMGUI_WIDGETS_USERCONTROL_USERCONTROLPARSER_H

#include <memory>
#include <string>

#include "Widgets/UserControl/UserControlElement.h"
#include "Widgets/UserControl/UserControlElements.h"

namespace XilEnvImGui
{

/**
 * @class UserControlParser
 * @brief Parses the recursive "x_0", "x_0_0", ... ini entries into an element tree.
 * @details Direct port of the Qt UserControlParser: element type is detected by the line's
 *          leading keyword (Group before Button); the trailing "(...)" is split into parameters
 *          and kept verbatim for round-trip. Group elements recurse into their child entries.
 */
class UserControlParser
{
public:
    /**
     * @brief Parses a whole UserControl window section into a root element.
     * @param[in] par_Section Window section (e.g. "GUI/Widgets/<name>"). Must not be nullptr.
     * @param[in] par_FileDescriptor Ini-database descriptor.
     * @return Owning root scene-graph node. Never nullptr.
     */
    [[nodiscard]] std::unique_ptr<UserControlRoot> Parse(const char *par_Section, int par_FileDescriptor);

    /**
     * @brief Creates and initializes one element from its config line.
     * @param[in] par_Line The element line (keyword + "(...)" parameters).
     * @return Owning element, or nullptr if the keyword is unknown.
     */
    [[nodiscard]] std::unique_ptr<UserControlElement> ParseOneLine(const std::string &par_Line);

private:
    void ParseGroupRecursive(UserControlElement *par_Parent, const char *par_Section,
                             const std::string &par_Entry, int par_FileDescriptor);
    void ParseParameters(UserControlElement *par_Element, const std::string &par_Line, int par_Pos);
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_WIDGETS_USERCONTROL_USERCONTROLPARSER_H
