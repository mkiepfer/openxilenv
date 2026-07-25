/**
 * @file Src/GUI/ImGui/Framework/ControlDialogs.h
 * @brief Engine-control configuration dialogs (process, scheduler, barrier, hotkeys, about).
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_FRAMEWORK_CONTROLDIALOGS_H
#define SRC_GUI_IMGUI_FRAMEWORK_CONTROLDIALOGS_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace XilEnvImGui
{

/**
 * @class ControlDialogManager
 * @brief Owns and renders the engine-control configuration dialogs reachable from the menu bar.
 * @details Process-wide singleton mirroring the Qt ConfigureProcessDialog, ConfigureSchedulersDialog,
 *          ConfigureBarrierDialog, HotkeysDialog and AboutDialog. Every dialog round-trips its state
 *          through the ini database (@ref IniDataBase.h) using the exact section/entry keys of the Qt
 *          front-end. All methods run on the render thread.
 * @warning Render-thread-only; not thread-safe.
 */
class ControlDialogManager
{
public:
    /**
     * @brief Returns the process-wide singleton instance.
     * @return Reference to the single ControlDialogManager.
     */
    static ControlDialogManager &GetInstance();

    /**
     * @brief Snapshots all process configurations from the scheduler/ini and opens the process modal.
     */
    void OpenConfigureProcess();

    /**
     * @brief Snapshots all scheduler configurations from the ini and opens the scheduler modal.
     */
    void OpenConfigureSchedulers();

    /**
     * @brief Snapshots the barrier list from the ini and opens the barrier modal.
     */
    void OpenConfigureBarriers();

    /**
     * @brief Reads the function-hotkey table from the ini and opens the hotkeys modal.
     */
    void OpenHotkeys();

    /**
     * @brief Builds the version/about text and opens the about modal.
     */
    void OpenAbout();

    /**
     * @brief Renders every managed modal for this frame.
     */
    void Render();

private:
    ControlDialogManager() = default;

    /**
     * @struct ProcessConfigEntry
     * @brief Edit state for a single process (mirrors ConfigureProcessDialog::DialogData).
     */
    struct ProcessConfigEntry
    {
        std::string name{};              /*!< Clean process name (ini entry key). */
        bool running{false};             /*!< Process is currently scheduled. */
        bool changed{false};             /*!< Marked when any field was edited (drives store). */
        int pid{0};                      /*!< Process id (display only). */
        int state{0};                    /*!< PROCESS_AKTIV..EX_PROCESS_* (display only). */
        int type{0};                     /*!< 0 internal, 2 external, 1/3 realtime (display). */
        int priority{0};                 /*!< Scheduling order. */
        int timeSteps{0};                /*!< Repetition cycle. */
        int delay{0};                    /*!< Start delay. */
        int timeout{0};                  /*!< Login timeout (round-tripped verbatim). */
        std::uint64_t mask{0};           /*!< Blackboard access mask (display only). */
        std::uint64_t cycleCount{0};     /*!< Cycle counter (display only). */
        std::uint64_t cycleTime{0};      /*!< Current cycle runtime (display only). */
        std::uint64_t cycleTimeMax{0};   /*!< Maximum cycle runtime (display only). */
        std::uint64_t cycleTimeMin{0};   /*!< Minimum cycle runtime (display only). */
        std::string longName{};          /*!< Full executable path (display only). */

        bool rcBeforeActive{false};      /*!< RANGE_CONTROL_BEFORE_ACTIVE_FLAG. */
        bool rcBeforeDeactive{false};    /*!< RANGE_CONTROL_BEFORE_INIT_AS_DEACTIVE. */
        bool rcBehindActive{false};      /*!< RANGE_CONTROL_BEHIND_ACTIVE_FLAG. */
        bool rcBehindDeactive{false};    /*!< RANGE_CONTROL_BEHIND_INIT_AS_DEACTIVE. */
        bool rcErrorCounterFlag{false};  /*!< RANGE_CONTROL_COUNTER_VARIABLE_FLAG. */
        bool rcVarFlag{false};           /*!< RANGE_CONTROL_CONTROL_VARIABLE_FLAG. */
        bool rcPhysFlag{false};          /*!< RANGE_CONTROL_PHYSICAL_FLAG. */
        bool rcStopSchedFlag{false};     /*!< RANGE_CONTROL_STOP_SCHEDULER_FLAG. */
        bool rcLimitValuesFlag{false};   /*!< RANGE_CONTROL_LIMIT_VALUES_FLAG. */
        int rcOutput{0};                 /*!< 0 none / 1 message box / 2 write to file. */
        std::string rcErrorCounter{};    /*!< Error-counter variable name. */
        std::string rcControl{};         /*!< Range-control variable name. */

        std::string equationBefore{};    /*!< TRI equation run before the process. */
        std::string equationBehind{};    /*!< TRI equation run behind the process. */
        std::string svlFile{};           /*!< SVL loaded before init. */
        std::string a2lFile{};           /*!< Associated A2L file. */
        int a2lFlags{0};                 /*!< A2L_LINK_* update flags. */
        std::string bbPrefix{};          /*!< Blackboard variable prefix. */
        std::string schedulerName{};     /*!< Assigned scheduler. */

        std::string barBeforeOnly{};     /*!< Barriers before, signal only (';' list). */
        std::string barBeforeWait{};     /*!< Barriers before, signal and wait (';' list). */
        std::string barBehindOnly{};     /*!< Barriers behind, signal only (';' list). */
        std::string barBehindWait{};     /*!< Barriers behind, signal and wait (';' list). */
        std::string barLoopBeforeOnly{}; /*!< Loop-out barriers before, signal only. */
        std::string barLoopBeforeWait{}; /*!< Loop-out barriers before, signal and wait. */
        std::string barLoopBehindOnly{}; /*!< Loop-out barriers behind, signal only. */
        std::string barLoopBehindWait{}; /*!< Loop-out barriers behind, signal and wait. */
    };

    /**
     * @struct SchedulerConfigEntry
     * @brief Edit state for a single scheduler (mirrors ConfigureSchedulersDialog::DialogData).
     */
    struct SchedulerConfigEntry
    {
        std::string name{};              /*!< Scheduler name ("Scheduler" is the always-present main). */
        bool changed{false};             /*!< Marked when any field was edited. */
        int priority{0};                 /*!< THREAD_PRIORITY_* thread priority. */
        std::string barBeforeOnly{};     /*!< Barriers before cycle, signal only. */
        std::string barBeforeWait{};     /*!< Barriers before cycle, signal and wait. */
        std::string barBehindOnly{};     /*!< Barriers behind cycle, signal only. */
        std::string barBehindWait{};     /*!< Barriers behind cycle, signal and wait. */
    };

    /**
     * @struct BarrierConfigEntry
     * @brief One configured scheduler barrier.
     */
    struct BarrierConfigEntry
    {
        std::string name{};              /*!< Barrier name. */
        bool waitIfAlone{false};         /*!< WaitIfAlone flag. */
    };

    /**
     * @struct HotkeyEntry
     * @brief One function-hotkey mapping (mirrors a shortcutStringList row).
     */
    struct HotkeyEntry
    {
        std::string hotkey{};            /*!< Display key sequence, e.g. "Ctrl+F6". */
        int function{0};                 /*!< Function number 0..8. */
        std::string formula{};           /*!< Formula / file / argument for the function. */
    };

    // -- Process dialog -----------------------------------------------------
    void SnapshotProcesses();
    ProcessConfigEntry ReadProcessInfos(const std::string &par_Name) const;
    void StoreProcessInfos(const ProcessConfigEntry &par_Entry) const;
    void SaveAllProcesses();
    void RenderConfigureProcess();

    // -- Scheduler dialog ---------------------------------------------------
    void SnapshotSchedulers();
    SchedulerConfigEntry ReadSchedulerInfos(const std::string &par_Name) const;
    void StoreSchedulerInfos(const SchedulerConfigEntry &par_Entry) const;
    void SaveAllSchedulers();
    void RenderConfigureSchedulers();

    // -- Barrier dialog -----------------------------------------------------
    void SnapshotBarriers();
    void SaveAllBarriers();
    void RenderConfigureBarriers();

    // -- Hotkeys dialog -----------------------------------------------------
    void SnapshotHotkeys();
    void SaveAllHotkeys();
    void RenderHotkeys();

    // -- About dialog -------------------------------------------------------
    void RenderAbout();

    // -- Shared helpers -----------------------------------------------------
    void LoadAvailableBarriers();
    void LoadSchedulerNames();
    void RenderBarrierListEditor(const char *par_Id, std::string &par_List);
    void RenderFilePicker();
    void OpenFilePicker(std::string *par_pTarget, const char *par_Extension);

    // -- Process state ------------------------------------------------------
    std::vector<ProcessConfigEntry> m_processes{};      /*!< Live process edit list. */
    std::vector<std::string> m_processesSave{};         /*!< Names present at open (delete detection). */
    int m_processSelected{-1};                          /*!< Index of the selected process. */
    bool m_processShowNonRunning{false};                /*!< Include non-running processes from the ini. */
    bool m_processOpen{false};                          /*!< Process modal is shown. */
    bool m_processOpenRequested{false};                 /*!< OpenPopup pending. */

    // -- Scheduler state ----------------------------------------------------
    std::vector<SchedulerConfigEntry> m_schedulers{};   /*!< Live scheduler edit list. */
    std::vector<std::string> m_schedulersSave{};        /*!< Names present at open. */
    int m_schedulerSelected{-1};                        /*!< Index of the selected scheduler. */
    bool m_schedulerOpen{false};                        /*!< Scheduler modal is shown. */
    bool m_schedulerOpenRequested{false};               /*!< OpenPopup pending. */

    // -- Barrier state ------------------------------------------------------
    std::vector<BarrierConfigEntry> m_barriers{};       /*!< Live barrier list. */
    std::vector<std::string> m_barriersSave{};          /*!< Names present at open. */
    std::array<char, 128> m_barrierNewName{};           /*!< New-barrier name buffer. */
    bool m_barrierNewWaitIfAlone{false};                /*!< New-barrier WaitIfAlone flag. */
    bool m_barrierOpen{false};                          /*!< Barrier modal is shown. */
    bool m_barrierOpenRequested{false};                 /*!< OpenPopup pending. */

    // -- Hotkey state -------------------------------------------------------
    std::vector<HotkeyEntry> m_hotkeys{};               /*!< Live hotkey list. */
    int m_hotkeySelected{-1};                           /*!< Index of the selected hotkey. */
    std::array<char, 64> m_hotkeyEditKey{};             /*!< Editor key-sequence buffer. */
    int m_hotkeyEditFunction{0};                        /*!< Editor function number. */
    std::array<char, 1024> m_hotkeyEditFormula{};       /*!< Editor formula buffer. */
    bool m_hotkeyEditIsNew{false};                      /*!< Editor targets a new entry. */
    bool m_hotkeyEditorOpen{false};                     /*!< Editor sub-modal is shown. */
    bool m_hotkeyEditorOpenRequested{false};            /*!< OpenPopup pending for the editor. */
    std::string m_hotkeyError{};                        /*!< Validation error in the editor. */
    bool m_hotkeyOpen{false};                           /*!< Hotkeys modal is shown. */
    bool m_hotkeyOpenRequested{false};                  /*!< OpenPopup pending. */

    // -- About state --------------------------------------------------------
    std::string m_aboutText{};                          /*!< Assembled about text. */
    bool m_aboutOpen{false};                            /*!< About modal is shown. */
    bool m_aboutOpenRequested{false};                   /*!< OpenPopup pending. */

    // -- Shared lookups / file picker --------------------------------------
    std::vector<std::string> m_availableBarriers{};     /*!< Configured barrier names. */
    std::vector<std::string> m_schedulerNames{};        /*!< Scheduler names for the assignment combo. */
    std::string *m_pFilePickerTarget{nullptr};          /*!< Field the file picker writes back into. */
    std::string m_filePickerExt{};                      /*!< Extension filter (lower-case, e.g. ".a2l"). */
    std::string m_filePickerPath{};                     /*!< Current directory in the file picker. */
    std::array<char, 4096> m_filePickerName{};          /*!< Edited file name in the picker. */
    bool m_filePickerOpenRequested{false};              /*!< OpenPopup pending for the file picker. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_FRAMEWORK_CONTROLDIALOGS_H
