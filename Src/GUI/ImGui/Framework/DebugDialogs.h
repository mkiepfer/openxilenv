/**
 * @file Src/GUI/ImGui/Framework/DebugDialogs.h
 * @brief Read-only debug/inspection dialogs (internals, debug infos, equations, barriers, copy lists).
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_FRAMEWORK_DEBUGDIALOGS_H
#define SRC_GUI_IMGUI_FRAMEWORK_DEBUGDIALOGS_H

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace XilEnvImGui
{

/**
 * @class DebugDialogManager
 * @brief Owns and renders the special/debug inspection dialogs of the ImGui front-end.
 * @details A process-wide singleton mirroring the Qt special dialogs (RemoteMasterCallStatistics,
 *          Internals, LoadedDebugInfos, ActiveEquations, ExtError, BarrierHistoryLogging,
 *          ExternProcessCopyList, ReferencedLabels). All views are modeless windows toggled by the
 *          menu bar; most render live from a core accessor every frame. @ref Render is called once
 *          per frame from the render thread.
 * @warning All entry points except @ref AddExtErrorMessage are render-thread-only and not thread-safe.
 *          @ref AddExtErrorMessage is mutex-guarded so it can be fed from the error seam on any thread.
 */
class DebugDialogManager
{
public:
    /**
     * @brief Returns the process-wide singleton instance.
     * @return Reference to the single DebugDialogManager.
     */
    static DebugDialogManager &GetInstance();

    // --- Window open requests (menu bar) -----------------------------------

    /** @brief Opens the remote-master RPC call-statistics table. */
    void OpenRemoteMasterCallStatistics();
    /** @brief Opens the internal engine-state view (open files, ini files, processes, memory). */
    void OpenInternals();
    /** @brief Opens the loaded debug-info modules view (connections/processes/debug infos). */
    void OpenLoadedDebugInfos();
    /** @brief Opens the currently-active equations list. */
    void OpenActiveEquations();
    /** @brief Opens the extended (non-modal) realtime error list. */
    void OpenExtError();
    /** @brief Opens the scheduler-barrier history + current-state view. */
    void OpenBarrierHistory();
    /** @brief Opens the external-process blackboard copy-list viewer. */
    void OpenExternProcessCopyList();
    /** @brief Opens the referenced-labels editor for a process. */
    void OpenReferencedLabels();

    // --- Error seam feed (thread-safe) -------------------------------------

    /**
     * @brief Appends one message to the extended (realtime) error list and shows the window.
     * @details Mirrors the Qt ExtendedErrorDialog::AddMessage path fed by the no-stop / realtime
     *          ThrowError levels. Safe to call from any thread.
     * @param[in] par_Level ThrowError level (see Src/Global/ThrowError.h); selects the icon/colour.
     * @param[in] par_Cycle Scheduler cycle the message was raised in.
     * @param[in] par_Pid Raising process id, or 0 if unknown.
     * @param[in] par_Message Message body. May be nullptr.
     */
    void AddExtErrorMessage(int par_Level, std::uint64_t par_Cycle, int par_Pid, const char *par_Message);

    /**
     * @brief Renders every managed window for this frame.
     */
    void Render();

private:
    DebugDialogManager() = default;

    /**
     * @struct ExtErrorEntry
     * @brief One extended-error list row.
     */
    struct ExtErrorEntry
    {
        int level{0};            /*!< ThrowError level. */
        std::uint64_t cycle{0};  /*!< Scheduler cycle. */
        int pid{0};              /*!< Raising process id. */
        std::string message{};   /*!< Whitespace-normalised message body. */
        bool selected{false};    /*!< Row selection state (for delete). */
    };

    /**
     * @struct RefLabelRow
     * @brief One referenced-label editor row (label + optional display-name rename).
     */
    struct RefLabelRow
    {
        std::string label{};                 /*!< Referenced blackboard label (ini "Ref" entry). */
        std::array<char, 512> renameTo{};    /*!< Editable display name (ini "Dsp" entry). */
    };

    // -- Renderers ----------------------------------------------------------
    void RenderRemoteMasterCallStatistics();
    void RenderInternals();
    void RenderLoadedDebugInfos();
    void RenderActiveEquations();
    void RenderExtError();
    void RenderBarrierHistory();
    void RenderExternProcessCopyList();
    void RenderReferencedLabels();

    // -- Shared helpers -----------------------------------------------------
    void RefreshExternalProcessNames();
    void FillReferencedLabels(const std::string &par_Process);
    void WriteReferencedLabelsToIni(const std::string &par_Process);

    // -- Remote-master statistics -------------------------------------------
    bool m_rmStatsOpen{false};                       /*!< Window visible. */

    // -- Internals ----------------------------------------------------------
    bool m_internalsOpen{false};                     /*!< Window visible. */
    int m_internalsProcessSel{0};                    /*!< Selected process combo index (0 == all). */

    // -- Loaded debug infos -------------------------------------------------
    bool m_debugInfosOpen{false};                    /*!< Window visible. */

    // -- Active equations ---------------------------------------------------
    bool m_activeEqOpen{false};                      /*!< Window visible. */
    int m_activeEqTypeMask{0xFF};                    /*!< EQU_TYPE_* filter mask. */
    std::array<char, 256> m_activeEqFilter{};        /*!< Wildcard main filter. */
    void *m_pEqSnapshot{nullptr};                    /*!< Persistent snapshot buffer (ACTIVE_EQUATION_ELEM*). */
    int m_eqSnapshotSize{0};                         /*!< Snapshot buffer capacity (elements). */
    char *m_pEqStringBuffer{nullptr};                /*!< Persistent string backing store. */
    int m_eqStringBufferSize{0};                     /*!< String buffer capacity (bytes). */

    // -- Extended error list ------------------------------------------------
    bool m_extErrorOpen{false};                      /*!< Window visible. */
    std::mutex m_extErrorMutex{};                    /*!< Guards m_extErrorEntries/m_extErrorOpen feed. */
    std::vector<ExtErrorEntry> m_extErrorEntries{};  /*!< Accumulated realtime error rows. */

    // -- Barrier history ----------------------------------------------------
    bool m_barrierHistoryOpen{false};                /*!< Window visible. */

    // -- External-process copy list -----------------------------------------
    bool m_copyListOpen{false};                      /*!< Window visible. */
    std::vector<std::string> m_externalProcesses{};  /*!< Non-internal process names. */
    int m_copyListProcessSel{-1};                    /*!< Selected process index. */
    int m_copyListType{0};                           /*!< 0 BB<->Pipe, 1 BB->Pipe, 2 BB<-Pipe. */
    std::array<char, 256> m_copyListFilter{};        /*!< Wildcard name filter. */
    std::array<char, 32> m_copyListAddrFrom{};       /*!< Hex address lower bound. */
    std::array<char, 32> m_copyListAddrTo{};         /*!< Hex address upper bound. */

    // -- Referenced labels --------------------------------------------------
    bool m_refLabelsOpen{false};                     /*!< Window visible. */
    int m_refLabelsProcessSel{-1};                   /*!< Selected process index. */
    std::string m_refLabelsProcess{};                /*!< Process the current rows belong to. */
    std::vector<RefLabelRow> m_refLabelsRows{};      /*!< Editable referenced-label rows. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_FRAMEWORK_DEBUGDIALOGS_H
