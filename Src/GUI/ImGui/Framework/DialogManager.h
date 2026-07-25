/**
 * @file Src/GUI/ImGui/Framework/DialogManager.h
 * @brief Seam-driven modeless dialog surface: progress bars, script dialogs and the error/message log.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_FRAMEWORK_DIALOGMANAGER_H
#define SRC_GUI_IMGUI_FRAMEWORK_DIALOGMANAGER_H

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace XilEnvImGui
{

/**
 * @class DialogManager
 * @brief Owns and renders the GUI/core-seam driven dialogs of the ImGui front-end.
 * @details A process-wide singleton that holds three independent models:
 *          - active progress bars (created/updated/closed fire-and-forget from any thread),
 *          - the single active script-driven user dialog (blocking seam calls, render-thread only),
 *          - the accumulated error/warning/message log (fed from any thread).
 *          @ref Render is called once per frame from the render thread and draws all three.
 * @warning Progress-bar and message entry points are thread-safe (mutex guarded). The script-dialog
 *          entry points and @ref Render MUST run on the render thread only; they are serialized by the
 *          command queue that drains blocking seam calls between frames.
 */
class DialogManager
{
public:
    /**
     * @brief Returns the process-wide singleton instance.
     * @return Reference to the single DialogManager.
     */
    static DialogManager &GetInstance();

    // --- Progress bars (thread-safe, fire-and-forget) ----------------------

    /**
     * @brief Creates a new progress bar and returns its identifier.
     * @param[in] par_Name Display name shown next to the bar. May be nullptr.
     * @return A positive, unique progress-bar id.
     */
    int OpenProgressBar(const char *par_Name);

    /**
     * @brief Updates the value of an existing progress bar.
     * @param[in] par_Id Progress-bar id returned by @ref OpenProgressBar.
     * @param[in] par_Value New value, clamped to [0,100].
     */
    void SetProgressBar(int par_Id, int par_Value);

    /**
     * @brief Removes a progress bar.
     * @param[in] par_Id Progress-bar id returned by @ref OpenProgressBar.
     */
    void CloseProgressBar(int par_Id);

    // --- Error / message log (thread-safe) ---------------------------------

    /**
     * @brief Appends a ThrowError-style message to the log.
     * @param[in] par_Level ThrowError level (see Src/Global/ThrowError.h); selects the colour.
     * @param[in] par_Header Optional header/title. May be nullptr.
     * @param[in] par_Message Message body. May be nullptr.
     */
    void AddErrorMessage(int par_Level, const char *par_Header, const char *par_Message);

    /**
     * @brief Appends a script-interpreter error message to the log.
     * @param[in] par_Level Script error level; selects the colour.
     * @param[in] par_LineNr Source line number, or <=0 if unknown.
     * @param[in] par_Filename Script file name. May be nullptr.
     * @param[in] par_Message Message body. May be nullptr.
     */
    void AddScriptErrorMessage(int par_Level, int par_LineNr, const char *par_Filename,
                               const char *par_Message);

    // --- Script dialog (render thread only, driven by the command queue) ----

    /**
     * @brief Creates (or re-initialises) the single active script dialog.
     * @param[in] par_Headline Dialog title. May be nullptr.
     * @return 0 on success.
     */
    int ScriptCreateDialog(const char *par_Headline);

    /**
     * @brief Adds one item to the current dialog page.
     * @details Mirrors the Qt protocol: par_Label == "*NEWPAGE*" starts a new page (par_Element is its
     *          name), par_Label == "*HEADLINE*" adds a headline row (par_Element is the text), otherwise
     *          par_Element is the visible text and par_Label is a blackboard variable name that is
     *          attached, edited by the user and written back on accept.
     * @param[in] par_Element Element text/name. May be nullptr.
     * @param[in] par_Label Label / blackboard-variable name / marker. May be nullptr.
     * @return 0 on success, -1 if par_Label is empty or no dialog exists.
     */
    int ScriptAddDialogItem(const char *par_Element, const char *par_Label);

    /**
     * @brief Makes the dialog visible and samples the current blackboard values into the editors.
     * @return 0 on success, -1 if no dialog exists.
     */
    int ScriptShowDialog();

    /**
     * @brief Destroys the dialog, removing any still-attached blackboard variables.
     * @return 0 on success, -1 if no dialog exists.
     */
    int ScriptDestroyDialog();

    /**
     * @brief Reports whether the dialog was dismissed by the user.
     * @details Matches the Qt slot: returns 1 (and tears the dialog down) when no dialog exists or the
     *          user has closed it; returns 0 while it is still open.
     * @return 1 if closed/absent, 0 if still open.
     */
    int IsScriptDialogClosed();

    // --- Per-frame rendering (render thread) --------------------------------

    /**
     * @brief Draws all active progress bars, the script dialog and the error log for this frame.
     */
    void Render();

private:
    DialogManager() = default;

    /**
     * @enum class ScriptItemKind
     * @brief Kind of a script-dialog row.
     */
    enum class ScriptItemKind : std::uint8_t
    {
        Headline, /*!< Non-interactive headline row (text only). */
        Value     /*!< Editable value bound to a blackboard variable. */
    };

    /**
     * @struct ProgressBarEntry
     * @brief One active progress bar.
     */
    struct ProgressBarEntry
    {
        int id{0};          /*!< Unique id. */
        std::string name{}; /*!< Display name. */
        int value{0};       /*!< Current value, [0,100]. */
    };

    /**
     * @struct LogEntry
     * @brief One error/warning/message log line.
     */
    struct LogEntry
    {
        int level{0};       /*!< ThrowError/script level. */
        std::string text{}; /*!< Fully formatted line. */
    };

    /**
     * @struct ScriptItem
     * @brief One row of the active script dialog.
     */
    struct ScriptItem
    {
        ScriptItemKind kind{ScriptItemKind::Headline}; /*!< Row kind. */
        std::string text{};                            /*!< Visible text (Headline/Value). */
        std::string variable{};                        /*!< Blackboard variable name (Value). */
        std::int32_t vid{0};                           /*!< Attached VID, <=0 if not attached. */
        double value{0.0};                             /*!< Editor value (Value). */
        double minValue{0.0};                          /*!< Lower bound (Value). */
        double maxValue{0.0};                          /*!< Upper bound (Value); if <=min, unbounded. */
    };

    /**
     * @struct ScriptPage
     * @brief One tab page of the active script dialog.
     */
    struct ScriptPage
    {
        std::string name{};              /*!< Tab title. */
        std::vector<ScriptItem> items{}; /*!< Rows on this page. */
    };

    /**
     * @struct ScriptDialogModel
     * @brief The single active script dialog.
     */
    struct ScriptDialogModel
    {
        std::string headline{};             /*!< Window title. */
        std::vector<ScriptPage> pages{};    /*!< Tab pages (always at least one). */
        bool active{false};                 /*!< A dialog currently exists. */
        bool visible{false};                /*!< ShowDialog has been called. */
        bool closed{false};                 /*!< User dismissed it. */
        bool accepted{false};               /*!< Dismissed via OK (vs. Cancel). */
    };

    void RenderProgressBars();
    void RenderScriptDialog();
    void RenderErrorLog();

    void AttachScriptItemVariable(ScriptItem &par_Item);
    void SampleScriptValues();
    void FinishScriptDialog(bool par_Accepted);
    void ReleaseScriptVariables(bool par_WriteBack);

    std::mutex m_mutex{};                        /*!< Guards m_progressBars and m_log. */
    std::vector<ProgressBarEntry> m_progressBars{};
    std::vector<LogEntry> m_log{};
    int m_progressIdCounter{0};                  /*!< Monotonic progress-bar id source. */
    bool m_errorLogOpen{true};                   /*!< Error-log window visibility toggle. */

    ScriptDialogModel m_scriptDialog{};          /*!< Render-thread-only script dialog model. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_FRAMEWORK_DIALOGMANAGER_H
