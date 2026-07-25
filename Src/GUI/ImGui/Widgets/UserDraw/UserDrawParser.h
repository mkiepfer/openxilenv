/**
 * @file Src/GUI/ImGui/Widgets/UserDraw/UserDrawParser.h
 * @brief Builds the UserDraw scene graph from the ini config (ImGui UserDraw port).
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_WIDGETS_USERDRAW_USERDRAWPARSER_H
#define SRC_GUI_IMGUI_WIDGETS_USERDRAW_USERDRAWPARSER_H

#include <memory>
#include <string>

#include "Widgets/UserDraw/UserDrawElement.h"
#include "Widgets/UserDraw/UserDrawElements.h"

namespace XilEnvImGui
{

/**
 * @class UserDrawParser
 * @brief Parses the recursive "x_0", "x_0_0", ... ini entries into an element tree.
 * @details Direct port of the Qt UserDrawParser: element type is detected by the line's
 *          leading keyword; the trailing "(...)" is split into parameters and kept verbatim
 *          for round-trip. Group-like elements recurse into their child entries.
 */
class UserDrawParser
{
public:
    /**
     * @brief Parses a whole UserDraw window section into a root element.
     * @param[in] par_Section Window section (e.g. "GUI/Widgets/<name>"). Must not be nullptr.
     * @param[in] par_FileDescriptor Ini-database descriptor.
     * @return Owning root scene-graph node. Never nullptr.
     */
    [[nodiscard]] std::unique_ptr<UserDrawRoot> Parse(const char *par_Section, int par_FileDescriptor);

    /**
     * @brief Creates and initializes one element from its config line.
     * @param[in] par_Line The element line (keyword + "(...)" parameters).
     * @return Owning element, or nullptr if the keyword is unknown.
     */
    [[nodiscard]] std::unique_ptr<UserDrawElement> ParseOneLine(const std::string &par_Line);

private:
    void ParseGroupRecursive(UserDrawElement *par_Parent, const char *par_Section,
                             const std::string &par_Entry, int par_FileDescriptor);
    void ParseParameters(UserDrawElement *par_Element, const std::string &par_Line, int par_Pos);
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_WIDGETS_USERDRAW_USERDRAWPARSER_H
