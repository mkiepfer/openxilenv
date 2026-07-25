/**
 * @file Src/GUI/ImGui/Framework/WindowManagerBridge.h
 * @brief Thin free-function bridge letting the command-queue seam drive the WindowManager.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_FRAMEWORK_WINDOWMANAGERBRIDGE_H
#define SRC_GUI_IMGUI_FRAMEWORK_WINDOWMANAGERBRIDGE_H

namespace XilEnvImGui
{

/**
 * @brief Opens a window by name on behalf of a drained cross-thread command.
 * @param[in] par_Name Window name. Must not be nullptr.
 * @return 1 if open afterwards, 0 otherwise.
 * @note Must be invoked on the render thread (from DrainGuiCommandQueue).
 */
int WmOpenWindowByName(const char *par_Name);

/**
 * @brief Closes a window by name on behalf of a drained cross-thread command.
 * @param[in] par_Name Window name. Must not be nullptr.
 * @return 1 if closed, 0 otherwise.
 */
int WmCloseWindowByName(const char *par_Name);

/**
 * @brief Reports whether a window is open on behalf of a drained cross-thread command.
 * @param[in] par_Name Window name. Must not be nullptr.
 * @return 1 if open, 0 otherwise.
 */
int WmIsWindowOpen(const char *par_Name);

/**
 * @brief Selects the named sheet on behalf of a drained cross-thread command.
 * @param[in] par_Name Sheet name. Must not be nullptr.
 * @return 1 if selected, 0 otherwise.
 */
int WmSelectSheet(const char *par_Name);

/**
 * @brief Adds a sheet on behalf of a drained cross-thread command.
 * @param[in] par_Name Sheet name. Must not be nullptr.
 * @return 1 if added/present, 0 otherwise.
 */
int WmAddSheet(const char *par_Name);

/**
 * @brief Deletes a sheet on behalf of a drained cross-thread command.
 * @param[in] par_Name Sheet name. Must not be nullptr.
 * @return 1 if deleted, 0 otherwise.
 */
int WmDeleteSheet(const char *par_Name);

/**
 * @brief Renames a sheet on behalf of a drained cross-thread command.
 * @param[in] par_OldName Existing sheet name. Must not be nullptr.
 * @param[in] par_NewName New sheet name. Must not be nullptr.
 * @return 1 if renamed, 0 otherwise.
 */
int WmRenameSheet(const char *par_OldName, const char *par_NewName);

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_FRAMEWORK_WINDOWMANAGERBRIDGE_H
