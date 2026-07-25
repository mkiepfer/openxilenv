/**
 * @file Src/GUI/ImGui/Framework/WindowManager.h
 * @brief Tabbed sheet/dock-space windowing model + layout persistence (replaces Qt Sheets/QMdiArea).
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_FRAMEWORK_WINDOWMANAGER_H
#define SRC_GUI_IMGUI_FRAMEWORK_WINDOWMANAGER_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "IWindowInstance.h"
#include "IWindowType.h"
#include "UpdateScheduler.h"
#include "WindowContext.h"

namespace XilEnvImGui
{

/**
 * @class WindowManager
 * @brief Owns every open window, groups them into tabbed sheets, renders and persists them.
 * @details Immediate-mode replacement for the Qt Sheets/QMdiArea/QTabWidget stack.
 *          Each sheet is an ImGui dock space; windows are ImGui::Begin("Name###Name")
 *          docked into the active sheet. Config round-trips through the IniDataBase
 *          facade using the Qt-compatible "GUI/OpenWindowsForSheet<N>" and
 *          "GUI/Widgets/<name>" section layout so files stay interchangeable.
 * @warning Render-thread-only; not thread-safe. Cross-thread opens arrive pre-serialized
 *          through the command queue and are executed here on the render thread.
 */
class WindowManager
{
public:
    /**
     * @brief Returns the process-wide window manager instance.
     * @return Reference to the singleton.
     */
    static WindowManager &GetInstance();

    /**
     * @brief Loads sheets and windows from the ini database (Qt-compatible sections).
     * @param[in] par_FileDescriptor Main ini descriptor ("GUI/" routes to layout.toml).
     */
    void LoadLayout(int par_FileDescriptor);

    /**
     * @brief Writes the current sheets and windows back to the ini database.
     * @param[in] par_FileDescriptor Main ini descriptor ("GUI/" routes to layout.toml).
     */
    void SaveLayout(int par_FileDescriptor);

    /**
     * @brief Discards all in-memory sheets/windows and rebuilds them from the ini database.
     * @details Used after an out-of-band ini mutation (e.g. sheet import) has added new
     *          "GUI/OpenWindowsForSheet<N>"/"GUI/Widgets/<name>" sections to the descriptor.
     *          Destroys every current window instance and re-runs @ref LoadLayout so the new
     *          sheets become visible.
     * @param[in] par_FileDescriptor Main ini descriptor ("GUI/" routes to layout.toml).
     */
    void ReloadFromIni(int par_FileDescriptor);

    /**
     * @brief Refreshes cached values of the active sheet's windows at their update rate.
     * @param[in] par_Scheduler Frame update scheduler (already advanced this frame).
     * @param[in] par_Context Per-frame context.
     */
    void Update(const UpdateScheduler &par_Scheduler, const WindowContext &par_Context);

    /**
     * @brief Renders the sheet tab bar, the active dock space and all its windows.
     * @param[in] par_Context Per-frame render context.
     */
    void Render(const WindowContext &par_Context);

    /**
     * @brief Draws the "new window" entries into an already-open ImGui menu.
     * @details Called on the render thread from the main menu bar.
     */
    void RenderNewWindowMenu();

    /**
     * @brief Creates a new window of the given type on the active sheet (render thread).
     * @param[in] par_Type Window type to instantiate. Must not be nullptr.
     * @return Non-owning pointer to the new instance, or nullptr on failure.
     */
    IWindowInstance *CreateWindowOnActiveSheet(IWindowType *par_Type);

    /**
     * @brief Opens (or re-shows) a window by name, creating it from config if needed.
     * @param[in] par_Name Window name. Must not be nullptr.
     * @return 1 if the window is open afterwards, 0 otherwise.
     */
    int OpenWindowByName(const char *par_Name);

    /**
     * @brief Closes and destroys a window by name.
     * @param[in] par_Name Window name. Must not be nullptr.
     * @return 1 if a window was closed, 0 if none matched.
     */
    int CloseWindowByName(const char *par_Name);

    /**
     * @brief Reports whether a window with the given name is currently open.
     * @param[in] par_Name Window name. Must not be nullptr.
     * @return 1 if open, 0 otherwise.
     */
    int IsWindowOpen(const char *par_Name) const;

    /**
     * @brief Reports whether a window name is already taken.
     * @param[in] par_Name Window name. Must not be nullptr.
     * @return true if the name is in use, false otherwise.
     */
    [[nodiscard]] bool WindowNameAlreadyInUse(const char *par_Name) const;

    /**
     * @brief Selects the sheet with the given name as active.
     * @param[in] par_Name Sheet name. Must not be nullptr.
     * @return 1 if selected, 0 if no such sheet.
     */
    int SelectSheet(const char *par_Name);

    /**
     * @brief Adds a new (empty) sheet unless one with the name already exists.
     * @param[in] par_Name Sheet name. Must not be nullptr.
     * @return 1 if added or already present, 0 on error.
     */
    int AddSheet(const char *par_Name);

    /**
     * @brief Deletes a sheet and closes the windows it owns.
     * @param[in] par_Name Sheet name. Must not be nullptr.
     * @return 1 if deleted, 0 if not found or it is the last sheet.
     */
    int DeleteSheet(const char *par_Name);

    /**
     * @brief Renames a sheet.
     * @param[in] par_OldName Existing sheet name. Must not be nullptr.
     * @param[in] par_NewName New sheet name. Must not be nullptr.
     * @return 1 if renamed, 0 otherwise.
     */
    int RenameSheet(const char *par_OldName, const char *par_NewName);

    /**
     * @brief Renames a window and re-keys the name->instance lookup.
     * @details Updates the instance's name (title, config section and "###" identity) AND the
     *          name->instance map so OpenWindowByName / dedup keep working after a rename.
     * @param[in] par_OldName Current window name. Must not be nullptr.
     * @param[in] par_NewName Desired new name. Must be non-empty and not already in use.
     * @return 1 on success, 0 if par_OldName is unknown or par_NewName is empty / already taken.
     * @note The ImGui dock slot is tied to the name ("###"), so a renamed window loses its dock
     *       position until re-docked — decoupling a stable dock id from the display name is a
     *       separate follow-up.
     */
    int RenameWindow(const char *par_OldName, const char *par_NewName);

private:
    /**
     * @struct SheetWindow
     * @brief A window placed on a sheet, with its Qt-compatible placement string.
     */
    struct SheetWindow
    {
        IWindowInstance *instance{nullptr}; /*!< Non-owning window pointer. */
        std::string placement{};            /*!< "left,top,width,height,is_icon,ml,mr" round-trip blob. */
    };

    /**
     * @struct Sheet
     * @brief A named tabbed workspace holding the windows shown in it.
     */
    struct Sheet
    {
        std::string name;                  /*!< Sheet display name. */
        std::vector<SheetWindow> windows;  /*!< Windows shown on this sheet. */
    };

    IWindowInstance *AddWindow(std::unique_ptr<IWindowInstance> par_Window, int par_SheetIndex,
                               const std::string &par_Placement);
    void DestroyWindow(const std::string &par_Name);
    int FindSheetIndex(const char *par_Name) const;
    std::string MakeUniqueName(const char *par_BaseName) const;
    void EnsureAtLeastOneSheet();

    std::vector<std::unique_ptr<IWindowInstance>> m_windows{};        /*!< Owning list of all windows. */
    std::unordered_map<std::string, IWindowInstance *> m_windowsByName{}; /*!< Name -> instance lookup. */
    std::vector<Sheet> m_sheets{};                                    /*!< Ordered sheets/tabs. */
    int m_activeSheet{0};                                             /*!< Index of the active sheet. */
    int m_pendingSelectSheet{-1};                                     /*!< Sheet the tab bar should switch to. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_FRAMEWORK_WINDOWMANAGER_H
