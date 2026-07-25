/**
 * @file Src/GUI/ImGui/Framework/SettingsDialogs.h
 * @brief Main-menu driven settings dialogs (BasicSettings form, placeholder info, config save/save-as).
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_FRAMEWORK_SETTINGSDIALOGS_H
#define SRC_GUI_IMGUI_FRAMEWORK_SETTINGSDIALOGS_H

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "GuiComponents.h"

namespace XilEnvImGui
{

/**
 * @brief Persists the current engine configuration plus the GUI layout to the loaded ini file.
 * @details Mirrors the Qt MainWindow::toolbarSaveIniFile path: writes the basic configuration and
 *          window layout back into the in-memory ini database and flushes the main descriptor to
 *          disk. On failure a message is appended to the error log.
 * @return true on success, false if the ini file could not be written.
 */
bool SaveConfiguration();

/**
 * @brief Persists the current configuration and layout under a new ini file name.
 * @details Mirrors the Qt MainWindow::menuSaveIniAs path: creates the target file, writes basic
 *          configuration and layout, renames the descriptor onto the new file and updates the
 *          active ini path. On failure a message is appended to the error log.
 * @param[in] par_Path Destination ini file path. Must not be nullptr or empty.
 * @return true on success, false otherwise.
 */
bool SaveConfigurationAs(const char *par_Path);

/**
 * @class SettingsDialogManager
 * @brief Owns and renders the settings dialogs reachable from the main menu bar.
 * @details Process-wide singleton holding the BasicSettings modal (a @ref FormField form editing
 *          @c s_main_ini_val), a generic "not yet implemented" info modal used by the CAN/XCP/CCP
 *          menu stubs, and the inline save-as file picker. All methods run on the render thread.
 * @warning Render-thread-only; not thread-safe.
 */
class SettingsDialogManager
{
public:
    /**
     * @brief Returns the process-wide singleton instance.
     * @return Reference to the single SettingsDialogManager.
     */
    static SettingsDialogManager &GetInstance();

    /**
     * @brief Snapshots @c s_main_ini_val into the edit buffers and opens the BasicSettings modal.
     */
    void OpenBasicSettings();

    /**
     * @brief Requests the "Save configuration as" file picker to be shown next frame.
     */
    void OpenSaveConfigurationAs();

    /**
     * @brief Shows a modal information popup (used by not-yet-implemented menu entries).
     * @param[in] par_Title Popup title. May be nullptr.
     * @param[in] par_Message Message body. May be nullptr.
     */
    void ShowInfo(const char *par_Title, const char *par_Message);

    /**
     * @brief Renders every managed modal for this frame.
     */
    void Render();

private:
    SettingsDialogManager() = default;

    /**
     * @struct BasicSettingsState
     * @brief Edit buffers backing the BasicSettings @ref FormField form.
     */
    struct BasicSettingsState
    {
        std::array<char, 1024> projectName{};       /*!< s_main_ini_val.ProjectName. */
        std::array<char, 4096> workDir{};           /*!< s_main_ini_val.WorkDir. */
        std::array<char, 4096> editor{};            /*!< s_main_ini_val.Editor / EditorX. */
        std::array<char, 128> priority{};           /*!< s_main_ini_val.Priority. */
        std::array<char, 1024> speedStatusbarCar{}; /*!< s_main_ini_val.SpeedStatusbarCar. */
        std::array<char, 4096> terminateScript{};   /*!< s_main_ini_val.TerminateScript. */
        std::array<char, 128> remoteMasterName{};   /*!< s_main_ini_val.RemoteMasterName. */
        std::array<char, 4096> remoteMasterExe{};   /*!< s_main_ini_val.RemoteMasterExecutable. */
        std::array<char, 4096> remoteMasterCopyTo{};/*!< s_main_ini_val.RemoteMasterCopyTo. */

        double schedulerPeriode{0.01};              /*!< s_main_ini_val.SchedulerPeriode. */
        int blackboardSize{1024};                   /*!< s_main_ini_val.BlackboardSize. */
        int oscilloscopeBufferDepth{4096};          /*!< s_main_ini_val.OscilloscopeDefaultBufferDepth. */
        int remoteMasterPort{0};                    /*!< s_main_ini_val.RemoteMasterPort. */
        int rpcSocketPort{0};                        /*!< s_main_ini_val.RpcSocketPort. */
        int externProcessLoginPort{0};              /*!< s_main_ini_val.ExternProcessLoginSocketPortIni. */

        int replaceNoneAsapChars{0};                /*!< Enum: 0 none / 1 ._[xx] / 2 ._[xx]._ . */
        int rpcTransport{0};                        /*!< Enum: 0 named pipe / 1 socket. */

        bool notFasterThanRealtime{false};          /*!< s_main_ini_val.NotFasterThanRealTime. */
        bool stopSchedulerWhileDialogOpen{false};   /*!< s_main_ini_val.ShouldStopSchedulerWhileDialogOpen. */
        bool separateCyclesRefInit{false};          /*!< s_main_ini_val.SeparateCyclesForRefAndInitFunction. */
        bool syncWithFlexray{false};                /*!< s_main_ini_val.SyncWithFlexray. */

        bool writeProtectProcessList{false};        /*!< s_main_ini_val.WriteProtectIniProcessList. */
        bool switchAutomaticSaveIniOff{false};      /*!< s_main_ini_val.SwitchAutomaticSaveIniOff. */
        bool askSaveIniAtExit{false};               /*!< s_main_ini_val.AskSaveIniAtExit. */
        bool saveSortedIniFiles{false};             /*!< s_main_ini_val.SaveSortedINIFiles. */
        bool makeBackupBeforeSave{false};           /*!< s_main_ini_val.MakeBackupBeforeSaveINIFiles. */
        bool dontWriteBbInfosToIni{false};          /*!< s_main_ini_val.DontWriteBlackbardVariableInfosToIni. */
        bool dontSaveBbInfosIniSection{false};      /*!< s_main_ini_val.DontSaveBlackbardVariableInfosIniSection. */

        bool rememberReferencedLabels{false};       /*!< s_main_ini_val.RememberReferencedLabels. */
        bool useRelativePaths{false};               /*!< s_main_ini_val.UseRelativePaths. */
        bool suppressNonExistValues{false};         /*!< s_main_ini_val.SuppressDisplayNonExistValues. */
        bool showUnitsForNonPhys{false};            /*!< DisplayUnitForNonePhysicalValues (global). */
        bool noCaseSensitiveFilters{false};         /*!< s_main_ini_val.NoCaseSensitiveFilters. */
        bool statusbarCar{false};                   /*!< s_main_ini_val.StatusbarCar. */
        bool asapCompatibleLabelnames{false};       /*!< s_main_ini_val.AsapCombatibleLabelnames. */

        bool viewStaticSymbols{false};              /*!< s_main_ini_val.ViewStaticSymbols. */
        bool extendStaticLabelsWithFile{false};     /*!< s_main_ini_val.ExtendStaticLabelsWithFilename. */

        bool dontMakeScriptDebugFile{false};        /*!< s_main_ini_val.DontMakeScriptDebugFile. */
        bool strictAtomicCheck{false};              /*!< s_main_ini_val.ScrictAtomicEndAtomicCheck. */
        bool storeReferenceInTempIni{false};        /*!< s_main_ini_val.UseTempIniForFunc.RefLists. */
        bool stopScriptIfLabelMissing{false};       /*!< s_main_ini_val.StopScriptIfLabelDosNotExistInsideSVLIni. */

        bool textShowUnitColumn{false};             /*!< s_main_ini_val.TextDefaultShowUnitColumn. */
        bool textShowDisplayTypeColumn{false};      /*!< s_main_ini_val.TextDefaultShowDispayTypeColumn. */

        bool connectToRemoteMaster{false};          /*!< s_main_ini_val.ShouldConnectToRemoteMaster. */
        bool copyAndStartRemoteMaster{false};       /*!< s_main_ini_val.CopyAndStartRemoteMaster. */

        bool rpcCommandName{false};                 /*!< RpcDebugLoggingFlags DEBUG_PRINT_COMMAND_NAME. */
        bool rpcParameterHex{false};                /*!< RpcDebugLoggingFlags DEBUG_PRINT_COMMAND_PARAMETER_AS_HEX. */
        bool rpcReturnHex{false};                   /*!< RpcDebugLoggingFlags DEBUG_PRINT_COMMAND_RETURN_AS_HEX. */
        bool rpcParameterDecoded{false};            /*!< RpcDebugLoggingFlags DEBUG_PRINT_COMMAND_PARAMETER_DECODED. */
        bool rpcReturnDecoded{false};               /*!< RpcDebugLoggingFlags DEBUG_PRINT_COMMAND_RETURN_DECODED. */

        bool externProcessLoginPortEnabled{false};  /*!< ExternProcessLoginSocketPortIni > 0. */

        bool darkMode{false};                       /*!< s_main_ini_val.ShouldUseDarkModeIni. */
    };

    void SnapshotBasicSettings();
    void BuildBasicSettingsForm();
    [[nodiscard]] bool ValidateBasicSettings(std::string &out_Error) const;
    void ApplyBasicSettings();

    void RenderBasicSettings();
    void RenderInfoModal();
    void RenderSaveAsPicker();

    BasicSettingsState m_basic{};                   /*!< Live edit state, snapshotted on open. */

    std::vector<FormField> m_generalFields{};       /*!< Project / paths / scheduler section. */
    std::vector<FormField> m_iniFields{};           /*!< Ini save behaviour section. */
    std::vector<FormField> m_displayFields{};       /*!< Display / labels section. */
    std::vector<FormField> m_scriptFields{};        /*!< Script section. */
    std::vector<FormField> m_windowFields{};        /*!< Text / oscilloscope defaults section. */
    std::vector<FormField> m_remoteFields{};        /*!< Remote master section. */
    std::vector<FormField> m_rpcFields{};           /*!< Remote procedure call section. */

    bool m_basicOpen{false};                        /*!< BasicSettings modal should be shown. */
    bool m_basicOpenRequested{false};               /*!< OpenPopup pending for the BasicSettings modal. */
    std::string m_basicError{};                     /*!< Validation error shown in the modal. */

    bool m_infoOpenRequested{false};                /*!< OpenPopup pending for the info modal. */
    std::string m_infoTitle{};                      /*!< Info modal title. */
    std::string m_infoMessage{};                    /*!< Info modal message body. */

    bool m_saveAsOpenRequested{false};              /*!< OpenPopup pending for the save-as picker. */
    std::string m_saveAsPath{};                     /*!< Current directory shown in the picker. */
    std::array<char, 4096> m_saveAsName{};          /*!< Edited file name in the picker. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_FRAMEWORK_SETTINGSDIALOGS_H
