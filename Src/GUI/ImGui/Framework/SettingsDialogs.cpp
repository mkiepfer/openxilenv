#include "Framework/SettingsDialogs.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "imgui.h"

#include "Framework/DialogManager.h"
#include "Framework/WindowManager.h"

#include "MainWinowSyncWithOtherThreads.h"

extern "C" {
#include "MainValues.h"
#include "IniDataBase.h"
#include "InitProcess.h"
#include "InterfaceToScript.h"
#include "Scheduler.h"
#include "RpcSocketServer.h"
}

namespace XilEnvImGui
{

namespace
{

constexpr double SCHEDULER_PERIOD_MIN{0.000000001};
constexpr double SCHEDULER_PERIOD_MAX{1.0};
constexpr int BLACKBOARD_SIZE_MIN{1024};
constexpr int BLACKBOARD_SIZE_MAX{1024 * 1024};

/**
 * @brief Bounded copy of a C string into a fixed-size destination buffer.
 */
void CopyToArray(char *par_Dst, std::size_t par_DstSize, const char *par_Src)
{
    if ((nullptr != par_Dst) && (0u < par_DstSize))
    {
        const char *source{(nullptr != par_Src) ? par_Src : ""};
        std::strncpy(par_Dst, source, par_DstSize - 1u);
        par_Dst[par_DstSize - 1u] = '\0';
    }
}

/**
 * @brief Sets or clears a single flag bit in a mask.
 */
void SetFlag(int &par_Mask, int par_Bit, bool par_On)
{
    if (par_On)
    {
        par_Mask |= par_Bit;
    }
    else
    {
        par_Mask &= ~par_Bit;
    }
}

/**
 * @brief Trims leading/trailing ASCII whitespace in place.
 */
void TrimInPlace(char *par_Text)
{
    if (nullptr == par_Text)
    {
        return;
    }
    char *begin{par_Text};
    while ((0 != *begin) && (0 != std::isspace(static_cast<unsigned char>(*begin))))
    {
        ++begin;
    }
    char *end{begin};
    char *last{begin};
    while (0 != *end)
    {
        if (0 == std::isspace(static_cast<unsigned char>(*end)))
        {
            last = end;
        }
        ++end;
    }
    const std::size_t length{(begin == end) ? 0u : static_cast<std::size_t>(last - begin) + 1u};
    std::memmove(par_Text, begin, length);
    par_Text[length] = '\0';
}

/**
 * @brief Appends one boolean form field bound to the given member.
 */
void AddBool(std::vector<FormField> &par_Fields, const char *par_Label, bool *par_Value,
             const char *par_Tooltip = "")
{
    FormField field;
    field.label = par_Label;
    field.kind = FormFieldKind::Bool;
    field.pBoolValue = par_Value;
    field.tooltip = par_Tooltip;
    par_Fields.push_back(std::move(field));
}

/**
 * @brief Appends one integer form field bound to the given member.
 */
void AddInt(std::vector<FormField> &par_Fields, const char *par_Label, int *par_Value,
            const char *par_Tooltip = "")
{
    FormField field;
    field.label = par_Label;
    field.kind = FormFieldKind::Int;
    field.pIntValue = par_Value;
    field.tooltip = par_Tooltip;
    par_Fields.push_back(std::move(field));
}

/**
 * @brief Appends one text form field bound to the given buffer.
 */
void AddText(std::vector<FormField> &par_Fields, const char *par_Label, char *par_Buffer,
             std::size_t par_BufferSize, const char *par_Tooltip = "")
{
    FormField field;
    field.label = par_Label;
    field.kind = FormFieldKind::Text;
    field.pTextBuffer = par_Buffer;
    field.textBufferSize = par_BufferSize;
    field.tooltip = par_Tooltip;
    par_Fields.push_back(std::move(field));
}

/**
 * @brief Renders a titled section of form fields with a unique table id.
 */
void RenderSection(const char *par_Title, std::vector<FormField> &par_Fields)
{
    ImGui::SeparatorText(par_Title);
    ImGui::PushID(par_Title);
    (void)RenderForm(par_Fields);
    ImGui::PopID();
}

/**
 * @brief True if @p par_Str ends with @p par_Suffix (case-insensitive).
 */
bool EndsWithCaseInsensitive(const std::string &par_Str, const std::string &par_Suffix)
{
    if (par_Str.size() < par_Suffix.size())
    {
        return false;
    }
    const std::string::size_type off{par_Str.size() - par_Suffix.size()};
    for (std::string::size_type i{0}; i < par_Suffix.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(par_Str[off + i])) !=
            std::tolower(static_cast<unsigned char>(par_Suffix[i])))
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Normalizes a user-typed Save-As target into a clean "<base>.toml" main config path.
 * @details Strips trailing dots/spaces and a trailing ".toml" and/or ".layout" so a name typed as
 *          "foo.", "foo.toml" or even "foo.layout.toml" all collapse to base "foo", then appends a
 *          single ".toml". Guarantees the derived layout path (@ref DeriveLayoutPath) and the
 *          startup derivation stay in sync — preventing "foo..toml" / "foo.layout.layout.toml".
 */
std::string NormalizeMainConfigPath(const std::string &par_Raw)
{
    std::string path{par_Raw};
    while (!path.empty() && (('.' == path.back()) || (' ' == path.back())))
    {
        path.pop_back();
    }
    if (EndsWithCaseInsensitive(path, ".toml"))
    {
        path.erase(path.size() - 5);
    }
    if (EndsWithCaseInsensitive(path, ".layout"))
    {
        path.erase(path.size() - 7);
    }
    while (!path.empty() && ('.' == path.back()))
    {
        path.pop_back();
    }
    path.append(".toml");
    return path;
}

/**
 * @brief Derives the GUI layout file path from a main config path (foo.toml -> foo.layout.toml).
 * @details Mirrors StartupInit's derivation: strip the last extension, append ".layout.toml".
 *          Expects an already-normalized main path (see @ref NormalizeMainConfigPath).
 */
std::string DeriveLayoutPath(const std::string &par_MainPath)
{
    std::string layout{par_MainPath};
    const std::string::size_type dot{layout.find_last_of('.')};
    if (std::string::npos != dot)
    {
        layout.erase(dot);
    }
    layout.append(".layout.toml");
    return layout;
}

} // namespace

// -------------------------------------------------------------------------
// Config persistence (menu "Save configuration" / "Save configuration as")
// -------------------------------------------------------------------------

bool SaveConfiguration()
{
    const int fd{GetMainFileDescriptor()};

    WriteBasicConfigurationToIni(&s_main_ini_val);
    WindowManager::GetInstance().SaveLayout(fd);
    (void)write_process_list2ini();

    // The GUI layout (sheets/windows, "GUI/" sections) is routed to a separate layout descriptor
    // by the ini facade. Flush it too, else Save persists only the engine config and the window
    // arrangement is lost on the next load. (StartupInit's shutdown saves both the same way.)
    const int layoutFd{GetLayoutFileDescriptor()};
    if (0 <= layoutFd)
    {
        (void)IniFileDataBaseSave(layoutFd, nullptr, INIFILE_DATABAE_OPERATION_WRITE_ONLY);
    }

    if (0 != IniFileDataBaseSave(fd, nullptr, INIFILE_DATABAE_OPERATION_WRITE_ONLY))
    {
        DialogManager::GetInstance().AddErrorMessage(2, "Save configuration",
                                                     "cannot save the configuration file (maybe write protected)");
        return false;
    }
    return true;
}

bool SaveConfigurationAs(const char *par_Path)
{
    if ((nullptr == par_Path) || ('\0' == par_Path[0]))
    {
        return false;
    }

    const int fd{GetMainFileDescriptor()};

    // Normalize the user-typed target to a clean "<base>.toml" so the engine and derived layout
    // paths stay in sync and reopening finds the layout (avoids foo..toml / foo.layout.layout.toml).
    const std::string mainPath{NormalizeMainConfigPath(par_Path)};

    if (0 != CreateNewEmptyIniFile(mainPath.c_str()))
    {
        DialogManager::GetInstance().AddErrorMessage(2, "Save configuration as",
                                                     "cannot create the target file (maybe write protected)");
        return false;
    }

    WriteBasicConfigurationToIni(&s_main_ini_val);
    WindowManager::GetInstance().SaveLayout(fd);
    (void)write_process_list2ini();

    // Save the GUI layout descriptor to the matching "<base>.layout.toml" and rename the descriptor
    // to it — else the window arrangement stays orphaned in the OLD layout file and reopening the
    // new config shows an empty desktop. Layout is saved before the engine file, matching
    // StartupInit's shutdown order.
    const int layoutFd{GetLayoutFileDescriptor()};
    if (0 <= layoutFd)
    {
        const std::string layoutPath{DeriveLayoutPath(mainPath)};
        (void)CreateNewEmptyIniFile(layoutPath.c_str());
        if (0 != IniFileDataBaseSave(layoutFd, layoutPath.c_str(), INIFILE_DATABAE_OPERATION_RENAME))
        {
            DialogManager::GetInstance().AddErrorMessage(2, "Save configuration as",
                                                         "cannot write the layout file (.layout.toml)");
            // Engine file is still saved below so the config is not lost entirely.
        }
    }

    if (0 != IniFileDataBaseSave(fd, mainPath.c_str(), INIFILE_DATABAE_OPERATION_RENAME))
    {
        DialogManager::GetInstance().AddErrorMessage(2, "Save configuration as",
                                                     "cannot write the target configuration file");
        return false;
    }
    (void)set_ini_path(mainPath.c_str());
    return true;
}

// -------------------------------------------------------------------------
// SettingsDialogManager
// -------------------------------------------------------------------------

SettingsDialogManager &SettingsDialogManager::GetInstance()
{
    static SettingsDialogManager s_instance;
    return s_instance;
}

void SettingsDialogManager::SnapshotBasicSettings()
{
    BasicSettingsState &s{m_basic};

    CopyToArray(s.projectName.data(), s.projectName.size(), s_main_ini_val.ProjectName);
    CopyToArray(s.workDir.data(), s.workDir.size(), s_main_ini_val.WorkDir);
#ifdef _WIN32
    CopyToArray(s.editor.data(), s.editor.size(), s_main_ini_val.Editor);
#else
    CopyToArray(s.editor.data(), s.editor.size(), s_main_ini_val.EditorX);
#endif
    CopyToArray(s.priority.data(), s.priority.size(), s_main_ini_val.Priority);
    CopyToArray(s.speedStatusbarCar.data(), s.speedStatusbarCar.size(), s_main_ini_val.SpeedStatusbarCar);
    CopyToArray(s.terminateScript.data(), s.terminateScript.size(), s_main_ini_val.TerminateScript);
    CopyToArray(s.remoteMasterName.data(), s.remoteMasterName.size(), s_main_ini_val.RemoteMasterName);
    CopyToArray(s.remoteMasterExe.data(), s.remoteMasterExe.size(), s_main_ini_val.RemoteMasterExecutable);
    CopyToArray(s.remoteMasterCopyTo.data(), s.remoteMasterCopyTo.size(), s_main_ini_val.RemoteMasterCopyTo);

    s.schedulerPeriode = s_main_ini_val.SchedulerPeriode;
    s.blackboardSize = s_main_ini_val.BlackboardSize;
    s.oscilloscopeBufferDepth = s_main_ini_val.OscilloscopeDefaultBufferDepth;
    s.remoteMasterPort = s_main_ini_val.RemoteMasterPort;
    s.rpcSocketPort = s_main_ini_val.RpcSocketPort;
    s.externProcessLoginPort = static_cast<int>(s_main_ini_val.ExternProcessLoginSocketPortIni);

    s.replaceNoneAsapChars = s_main_ini_val.ReplaceAllNoneAsapCombatibleChars;
    s.rpcTransport = (0 != s_main_ini_val.RpcOverSocketOrNamedPipe) ? 1 : 0;

    s.notFasterThanRealtime = (0 != s_main_ini_val.NotFasterThanRealTime);
    s.stopSchedulerWhileDialogOpen = (0 != s_main_ini_val.ShouldStopSchedulerWhileDialogOpen);
    s.separateCyclesRefInit = (0 != s_main_ini_val.SeparateCyclesForRefAndInitFunction);
    s.syncWithFlexray = (0 != s_main_ini_val.SyncWithFlexray);

    s.writeProtectProcessList = (0 != s_main_ini_val.WriteProtectIniProcessList);
    s.switchAutomaticSaveIniOff = (0 != s_main_ini_val.SwitchAutomaticSaveIniOff);
    s.askSaveIniAtExit = (0 != s_main_ini_val.AskSaveIniAtExit);
    s.saveSortedIniFiles = (0 != s_main_ini_val.SaveSortedINIFiles);
    s.makeBackupBeforeSave = (0 != s_main_ini_val.MakeBackupBeforeSaveINIFiles);
    s.dontWriteBbInfosToIni = (0 != s_main_ini_val.DontWriteBlackbardVariableInfosToIni);
    s.dontSaveBbInfosIniSection = (0 != s_main_ini_val.DontSaveBlackbardVariableInfosIniSection);

    s.rememberReferencedLabels = (0 != s_main_ini_val.RememberReferencedLabels);
    s.useRelativePaths = (0 != s_main_ini_val.UseRelativePaths);
    s.suppressNonExistValues = (0 != s_main_ini_val.SuppressDisplayNonExistValues);
    s.showUnitsForNonPhys = (0 != DisplayUnitForNonePhysicalValues);
    s.noCaseSensitiveFilters = (0 != s_main_ini_val.NoCaseSensitiveFilters);
    s.statusbarCar = (0 != s_main_ini_val.StatusbarCar);
    s.asapCompatibleLabelnames = (0 != s_main_ini_val.AsapCombatibleLabelnames);

    s.viewStaticSymbols = (0 != s_main_ini_val.ViewStaticSymbols);
    s.extendStaticLabelsWithFile = (0 != s_main_ini_val.ExtendStaticLabelsWithFilename);

    s.dontMakeScriptDebugFile = (0 != s_main_ini_val.DontMakeScriptDebugFile);
    s.strictAtomicCheck = (0 != s_main_ini_val.ScrictAtomicEndAtomicCheck);
    s.storeReferenceInTempIni = (0 != s_main_ini_val.UseTempIniForFunc.RefLists);
    s.stopScriptIfLabelMissing = (0 != s_main_ini_val.StopScriptIfLabelDosNotExistInsideSVLIni);

    s.textShowUnitColumn = (0 != s_main_ini_val.TextDefaultShowUnitColumn);
    s.textShowDisplayTypeColumn = (0 != s_main_ini_val.TextDefaultShowDispayTypeColumn);

    s.connectToRemoteMaster = (0 != s_main_ini_val.ShouldConnectToRemoteMaster);
    s.copyAndStartRemoteMaster = (0 != s_main_ini_val.CopyAndStartRemoteMaster);

    const int rpcFlags{s_main_ini_val.RpcDebugLoggingFlags};
    s.rpcCommandName = (0 != (rpcFlags & DEBUG_PRINT_COMMAND_NAME));
    s.rpcParameterHex = (0 != (rpcFlags & DEBUG_PRINT_COMMAND_PARAMETER_AS_HEX));
    s.rpcReturnHex = (0 != (rpcFlags & DEBUG_PRINT_COMMAND_RETURN_AS_HEX));
    s.rpcParameterDecoded = (0 != (rpcFlags & DEBUG_PRINT_COMMAND_PARAMETER_DECODED));
    s.rpcReturnDecoded = (0 != (rpcFlags & DEBUG_PRINT_COMMAND_RETURN_DECODED));

    s.externProcessLoginPortEnabled = (0 < s_main_ini_val.ExternProcessLoginSocketPortIni);

    s.darkMode = (0 != s_main_ini_val.ShouldUseDarkModeIni);
}

void SettingsDialogManager::BuildBasicSettingsForm()
{
    BasicSettingsState &s{m_basic};

    m_generalFields.clear();
    AddText(m_generalFields, "Project name", s.projectName.data(), s.projectName.size());
    AddText(m_generalFields, "Working directory", s.workDir.data(), s.workDir.size(),
            "Applied to s_main_ini_val; the process working directory is not changed at runtime.");
    AddText(m_generalFields, "Editor", s.editor.data(), s.editor.size());
    AddText(m_generalFields, "Priority", s.priority.data(), s.priority.size());
    {
        FormField field;
        field.label = "Scheduler period [s]";
        field.kind = FormFieldKind::Double;
        field.pDoubleValue = &s.schedulerPeriode;
        field.minValue = SCHEDULER_PERIOD_MIN;
        field.maxValue = SCHEDULER_PERIOD_MAX;
        field.precision = 9;
        field.tooltip = "Between 1.0s and 1.0ns (resolution 1ns).";
        m_generalFields.push_back(std::move(field));
    }
    AddInt(m_generalFields, "Blackboard elements", &s.blackboardSize,
           "1024...1048576; a change is applied after restarting.");
    AddBool(m_generalFields, "Not faster than realtime", &s.notFasterThanRealtime);
    AddBool(m_generalFields, "Stop scheduler while dialog open", &s.stopSchedulerWhileDialogOpen);
    AddBool(m_generalFields, "Separate cycles for ref/init function", &s.separateCyclesRefInit);
    AddBool(m_generalFields, "Sync with FlexRay", &s.syncWithFlexray);

    m_iniFields.clear();
    AddBool(m_iniFields, "Write-protect process list", &s.writeProtectProcessList);
    AddBool(m_iniFields, "Switch automatic INI save off", &s.switchAutomaticSaveIniOff);
    AddBool(m_iniFields, "Ask to save INI at exit", &s.askSaveIniAtExit);
    AddBool(m_iniFields, "Save sorted INI files", &s.saveSortedIniFiles);
    AddBool(m_iniFields, "Make backup before saving INI", &s.makeBackupBeforeSave);
    AddBool(m_iniFields, "Don't write BB variable infos to INI", &s.dontWriteBbInfosToIni);
    AddBool(m_iniFields, "Don't save BB variable infos section", &s.dontSaveBbInfosIniSection);

    m_displayFields.clear();
    AddBool(m_displayFields, "Remember referenced labels", &s.rememberReferencedLabels);
    AddBool(m_displayFields, "Use relative paths", &s.useRelativePaths);
    AddBool(m_displayFields, "Suppress display of non-existing values", &s.suppressNonExistValues);
    AddBool(m_displayFields, "Show units for non-physical values", &s.showUnitsForNonPhys);
    AddBool(m_displayFields, "No case-sensitive filters", &s.noCaseSensitiveFilters);
    AddBool(m_displayFields, "Show status bar (car)", &s.statusbarCar);
    AddText(m_displayFields, "Status bar speed", s.speedStatusbarCar.data(), s.speedStatusbarCar.size());
    AddBool(m_displayFields, "ASAP2 compatible label names", &s.asapCompatibleLabelnames);
    {
        FormField field;
        field.label = "Replace non-ASAP2 chars";
        field.kind = FormFieldKind::Enum;
        field.pIntValue = &s.replaceNoneAsapChars;
        field.enumLabels = {"off", "._[xx]", "._[xx]._"};
        m_displayFields.push_back(std::move(field));
    }
    AddBool(m_displayFields, "Display static symbols", &s.viewStaticSymbols);
    AddBool(m_displayFields, "Extend static labels with filename", &s.extendStaticLabelsWithFile);

    m_scriptFields.clear();
    AddBool(m_scriptFields, "Don't make script debug file", &s.dontMakeScriptDebugFile);
    AddText(m_scriptFields, "Terminate script", s.terminateScript.data(), s.terminateScript.size());
    AddBool(m_scriptFields, "Strict ATOMIC/END_ATOMIC check", &s.strictAtomicCheck);
    AddBool(m_scriptFields, "Store reference list in temp INI", &s.storeReferenceInTempIni);
    AddBool(m_scriptFields, "Stop script if SVL label missing", &s.stopScriptIfLabelMissing);

    m_windowFields.clear();
    AddBool(m_windowFields, "Text: show unit column", &s.textShowUnitColumn);
    AddBool(m_windowFields, "Text: show display-type column", &s.textShowDisplayTypeColumn);
    AddInt(m_windowFields, "Oscilloscope buffer depth", &s.oscilloscopeBufferDepth);

    m_remoteFields.clear();
    AddBool(m_remoteFields, "Enable remote master", &s.connectToRemoteMaster);
    AddText(m_remoteFields, "Remote master name", s.remoteMasterName.data(), s.remoteMasterName.size());
    AddInt(m_remoteFields, "Remote master port", &s.remoteMasterPort);
    AddBool(m_remoteFields, "Copy and start remote master", &s.copyAndStartRemoteMaster);
    AddText(m_remoteFields, "Remote master executable", s.remoteMasterExe.data(), s.remoteMasterExe.size());
    AddText(m_remoteFields, "Copy remote master to", s.remoteMasterCopyTo.data(), s.remoteMasterCopyTo.size());

    m_rpcFields.clear();
    {
        FormField field;
        field.label = "RPC transport";
        field.kind = FormFieldKind::Enum;
        field.pIntValue = &s.rpcTransport;
        field.enumLabels = {"named pipe", "socket"};
        m_rpcFields.push_back(std::move(field));
    }
    AddInt(m_rpcFields, "RPC socket port", &s.rpcSocketPort);
    AddBool(m_rpcFields, "Log command name", &s.rpcCommandName);
    AddBool(m_rpcFields, "Log parameter (hex)", &s.rpcParameterHex);
    AddBool(m_rpcFields, "Log return (hex)", &s.rpcReturnHex);
    AddBool(m_rpcFields, "Log parameter (decoded)", &s.rpcParameterDecoded);
    AddBool(m_rpcFields, "Log return (decoded)", &s.rpcReturnDecoded);
    AddBool(m_rpcFields, "Enable extern process login port", &s.externProcessLoginPortEnabled);
    AddInt(m_rpcFields, "Extern process login port", &s.externProcessLoginPort);
    AddBool(m_rpcFields, "Use dark mode (after restart)", &s.darkMode);
}

void SettingsDialogManager::OpenBasicSettings()
{
    SnapshotBasicSettings();
    BuildBasicSettingsForm();
    m_basicError.clear();
    m_basicOpen = true;
    m_basicOpenRequested = true;
}

void SettingsDialogManager::OpenSaveConfigurationAs()
{
    if (m_saveAsPath.empty())
    {
        std::error_code ec;
        m_saveAsPath = std::filesystem::current_path(ec).string();
        if (ec)
        {
            m_saveAsPath = ".";
        }
    }
    m_saveAsName[0] = '\0';
    m_saveAsOpenRequested = true;
}

void SettingsDialogManager::ShowInfo(const char *par_Title, const char *par_Message)
{
    m_infoTitle = (nullptr != par_Title) ? par_Title : "Information";
    m_infoMessage = (nullptr != par_Message) ? par_Message : "";
    m_infoOpenRequested = true;
}

bool SettingsDialogManager::ValidateBasicSettings(std::string &out_Error) const
{
    if ((m_basic.schedulerPeriode > SCHEDULER_PERIOD_MAX) ||
        (m_basic.schedulerPeriode < SCHEDULER_PERIOD_MIN))
    {
        out_Error = "Scheduling period has to be between 1.0s and 1.0ns.";
        return false;
    }
    if ((m_basic.blackboardSize > BLACKBOARD_SIZE_MAX) || (m_basic.blackboardSize < BLACKBOARD_SIZE_MIN))
    {
        out_Error = "Blackboard elements count must be in the range 1024...1048576.";
        return false;
    }
    return true;
}

void SettingsDialogManager::ApplyBasicSettings()
{
    const BasicSettingsState &s{m_basic};

    // Persist the current process list first (matches the Qt accept ordering).
    (void)write_process_list2ini();

    // A running script may depend on settings we are about to change.
    if (RUNNING == script_status_flag)
    {
        script_status_flag = STOP;
        DialogManager::GetInstance().AddErrorMessage(
            1, "Basic settings", "Script stopped because of possible changes in the basic settings.");
    }

    CopyToArray(s_main_ini_val.ProjectName, sizeof(s_main_ini_val.ProjectName), s.projectName.data());
    if ('\0' != s.workDir[0])
    {
        CopyToArray(s_main_ini_val.WorkDir, sizeof(s_main_ini_val.WorkDir), s.workDir.data());
    }
    else
    {
        CopyToArray(s_main_ini_val.WorkDir, sizeof(s_main_ini_val.WorkDir), ".");
    }
#ifdef _WIN32
    CopyToArray(s_main_ini_val.Editor, sizeof(s_main_ini_val.Editor), s.editor.data());
#else
    CopyToArray(s_main_ini_val.EditorX, sizeof(s_main_ini_val.EditorX), s.editor.data());
#endif

    // Scheduler period is stored with a 1ns resolution.
    char periodText[64];
    std::snprintf(periodText, sizeof(periodText), "%.9f", s.schedulerPeriode);
    s_main_ini_val.SchedulerPeriode = std::atof(periodText);

    if (s_main_ini_val.BlackboardSize != s.blackboardSize)
    {
        DialogManager::GetInstance().AddErrorMessage(
            1, "Basic settings",
            "You have changed the blackboard size. The modification will be accepted after restarting.");
    }
    s_main_ini_val.BlackboardSize = s.blackboardSize;

    CopyToArray(s_main_ini_val.Priority, sizeof(s_main_ini_val.Priority), s.priority.data());
    s_main_ini_val.DontCallSleep = SetPriority(s_main_ini_val.Priority);

    s_main_ini_val.NotFasterThanRealTime = s.notFasterThanRealtime ? 1 : 0;
    SetNotFasterThanRealtimeState(s_main_ini_val.NotFasterThanRealTime);
    s_main_ini_val.SeparateCyclesForRefAndInitFunction = s.separateCyclesRefInit ? 1 : 0;
    s_main_ini_val.SyncWithFlexray = s.syncWithFlexray ? 1 : 0;

    s_main_ini_val.WriteProtectIniProcessList = s.writeProtectProcessList ? 1 : 0;
    s_main_ini_val.SwitchAutomaticSaveIniOff = s.switchAutomaticSaveIniOff ? 1 : 0;
    s_main_ini_val.AskSaveIniAtExit = s.askSaveIniAtExit ? 1 : 0;
    s_main_ini_val.SaveSortedINIFiles = s.saveSortedIniFiles ? 1 : 0;
    s_main_ini_val.MakeBackupBeforeSaveINIFiles = s.makeBackupBeforeSave ? 1 : 0;
    s_main_ini_val.DontWriteBlackbardVariableInfosToIni = s.dontWriteBbInfosToIni ? 1 : 0;
    s_main_ini_val.DontSaveBlackbardVariableInfosIniSection = s.dontSaveBbInfosIniSection ? 1 : 0;

    s_main_ini_val.RememberReferencedLabels = s.rememberReferencedLabels ? 1 : 0;
    s_main_ini_val.UseRelativePaths = s.useRelativePaths ? 1 : 0;
    s_main_ini_val.SuppressDisplayNonExistValues = s.suppressNonExistValues ? 1 : 0;
    SetSuppressDisplayNonExistValuesState(s_main_ini_val.SuppressDisplayNonExistValues);
    DisplayUnitForNonePhysicalValues = s.showUnitsForNonPhys ? 1 : 0;
    s_main_ini_val.NoCaseSensitiveFilters = s.noCaseSensitiveFilters ? 1 : 0;
    s_main_ini_val.StatusbarCar = s.statusbarCar ? 1 : 0;
    CopyToArray(s_main_ini_val.SpeedStatusbarCar, sizeof(s_main_ini_val.SpeedStatusbarCar),
                s.speedStatusbarCar.data());
    s_main_ini_val.AsapCombatibleLabelnames = s.asapCompatibleLabelnames ? 1 : 0;
    s_main_ini_val.ReplaceAllNoneAsapCombatibleChars = s.replaceNoneAsapChars;

    s_main_ini_val.ViewStaticSymbols = s.viewStaticSymbols ? 1 : 0;
    s_main_ini_val.ExtendStaticLabelsWithFilename = s.extendStaticLabelsWithFile ? 1 : 0;

    s_main_ini_val.DontMakeScriptDebugFile = s.dontMakeScriptDebugFile ? 1 : 0;
    CopyToArray(s_main_ini_val.TerminateScript, sizeof(s_main_ini_val.TerminateScript),
                s.terminateScript.data());
    TrimInPlace(s_main_ini_val.TerminateScript);
    s_main_ini_val.TerminateScriptFlag = (0 != s_main_ini_val.TerminateScript[0]) ? 1 : 0;
    s_main_ini_val.ScrictAtomicEndAtomicCheck = s.strictAtomicCheck ? 1 : 0;
    s_main_ini_val.UseTempIniForFunc.RefLists = s.storeReferenceInTempIni ? 1 : 0;
    s_main_ini_val.StopScriptIfLabelDosNotExistInsideSVL = s.stopScriptIfLabelMissing ? 1 : 0;
    s_main_ini_val.StopScriptIfLabelDosNotExistInsideSVLIni = s.stopScriptIfLabelMissing ? 1 : 0;

    // Scheduler start/stop transition (only when no remote master owns the scheduler).
    if (0 == s_main_ini_val.ConnectToRemoteMaster)
    {
        if (s.stopSchedulerWhileDialogOpen && (0 == s_main_ini_val.StopSchedulerWhileDialogOpen))
        {
            (void)disable_scheduler_at_end_of_cycle(SCHEDULER_CONTROLED_BY_SYSTEM, nullptr, nullptr);
        }
        if (!s.stopSchedulerWhileDialogOpen && (1 == s_main_ini_val.StopSchedulerWhileDialogOpen))
        {
            enable_scheduler(SCHEDULER_CONTROLED_BY_SYSTEM);
        }
    }
    s_main_ini_val.ShouldStopSchedulerWhileDialogOpen = s.stopSchedulerWhileDialogOpen ? 1 : 0;
    if (0 == s_main_ini_val.ConnectToRemoteMaster)
    {
        s_main_ini_val.StopSchedulerWhileDialogOpen = s_main_ini_val.ShouldStopSchedulerWhileDialogOpen;
    }

    s_main_ini_val.TextDefaultShowUnitColumn = s.textShowUnitColumn ? 1 : 0;
    s_main_ini_val.TextDefaultShowDispayTypeColumn = s.textShowDisplayTypeColumn ? 1 : 0;
    s_main_ini_val.OscilloscopeDefaultBufferDepth = s.oscilloscopeBufferDepth;

    // Remote master.
    s_main_ini_val.ShouldConnectToRemoteMaster = s.connectToRemoteMaster ? 1 : 0;
    CopyToArray(s_main_ini_val.RemoteMasterName, sizeof(s_main_ini_val.RemoteMasterName),
                s.remoteMasterName.data());
    s_main_ini_val.RemoteMasterPort = s.remoteMasterPort;
    s_main_ini_val.CopyAndStartRemoteMaster = s.copyAndStartRemoteMaster ? 1 : 0;
    CopyToArray(s_main_ini_val.RemoteMasterExecutable, sizeof(s_main_ini_val.RemoteMasterExecutable),
                s.remoteMasterExe.data());
    CopyToArray(s_main_ini_val.RemoteMasterCopyTo, sizeof(s_main_ini_val.RemoteMasterCopyTo),
                s.remoteMasterCopyTo.data());

    // Remote procedure calls.
    s_main_ini_val.RpcOverSocketOrNamedPipe = (1 == s.rpcTransport) ? 1 : 0;
    int rpcFlags{0};
    SetFlag(rpcFlags, DEBUG_PRINT_COMMAND_NAME, s.rpcCommandName);
    SetFlag(rpcFlags, DEBUG_PRINT_COMMAND_PARAMETER_AS_HEX, s.rpcParameterHex);
    SetFlag(rpcFlags, DEBUG_PRINT_COMMAND_RETURN_AS_HEX, s.rpcReturnHex);
    SetFlag(rpcFlags, DEBUG_PRINT_COMMAND_PARAMETER_DECODED, s.rpcParameterDecoded);
    SetFlag(rpcFlags, DEBUG_PRINT_COMMAND_RETURN_DECODED, s.rpcReturnDecoded);
    s_main_ini_val.RpcDebugLoggingFlags = rpcFlags;
    s_main_ini_val.RpcSocketPort = s.rpcSocketPort;

    if (s.externProcessLoginPortEnabled && (0 < s.externProcessLoginPort))
    {
        s_main_ini_val.ExternProcessLoginSocketPortIni =
            static_cast<unsigned int>(s.externProcessLoginPort);
    }
    else
    {
        s_main_ini_val.ExternProcessLoginSocketPortIni = 0u;
    }

    s_main_ini_val.ShouldUseDarkModeIni = s.darkMode ? 1 : 0;

    // Write the basic configuration back into the in-memory ini database so it is
    // serialized on the next "Save configuration" (matches the Qt save path).
    WriteBasicConfigurationToIni(&s_main_ini_val);
}

// -------------------------------------------------------------------------
// Rendering (render thread)
// -------------------------------------------------------------------------

void SettingsDialogManager::Render()
{
    RenderBasicSettings();
    RenderSaveAsPicker();
    RenderInfoModal();
}

void SettingsDialogManager::RenderBasicSettings()
{
    if (m_basicOpenRequested)
    {
        ImGui::OpenPopup("Basic settings###BasicSettings");
        m_basicOpenRequested = false;
    }
    if (!m_basicOpen)
    {
        return;
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560.0f, 640.0f), ImGuiCond_Appearing);

    bool open{true};
    if (ImGui::BeginPopupModal("Basic settings###BasicSettings", &open,
                               ImGuiWindowFlags_NoSavedSettings))
    {
        if (ImGui::BeginChild("##basicscroll", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() - 8.0f)))
        {
            RenderSection("General / scheduler", m_generalFields);
            RenderSection("INI file handling", m_iniFields);
            RenderSection("Display / labels", m_displayFields);
            RenderSection("Script", m_scriptFields);
            RenderSection("Window defaults", m_windowFields);
            RenderSection("Remote master", m_remoteFields);
            RenderSection("Remote procedure calls", m_rpcFields);
        }
        ImGui::EndChild();

        if (!m_basicError.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", m_basicError.c_str());
        }

        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(100.0f, 0.0f)))
        {
            std::string error;
            if (ValidateBasicSettings(error))
            {
                ApplyBasicSettings();
                m_basicOpen = false;
                ImGui::CloseCurrentPopup();
            }
            else
            {
                m_basicError = error;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
        {
            m_basicOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (!open)
    {
        m_basicOpen = false;
    }
}

void SettingsDialogManager::RenderInfoModal()
{
    if (m_infoOpenRequested)
    {
        ImGui::OpenPopup("###SettingsInfo");
        m_infoOpenRequested = false;
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    const std::string title{m_infoTitle + "###SettingsInfo"};
    if (ImGui::BeginPopupModal(title.c_str(), nullptr,
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(m_infoMessage.c_str());
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120.0f, 0.0f)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void SettingsDialogManager::RenderSaveAsPicker()
{
    namespace fs = std::filesystem;

    if (m_saveAsOpenRequested)
    {
        ImGui::OpenPopup("Save configuration as###SaveConfigAs");
        m_saveAsOpenRequested = false;
    }

    ImGui::SetNextWindowSize(ImVec2(560.0f, 460.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Save configuration as###SaveConfigAs", nullptr,
                                ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }

    std::error_code ec;
    ImGui::TextUnformatted(m_saveAsPath.c_str());
    if (ImGui::Button(".. (up)"))
    {
        const fs::path parent{fs::path{m_saveAsPath}.parent_path()};
        if (!parent.empty())
        {
            m_saveAsPath = parent.string();
        }
    }

    ImGui::BeginChild("##saveaslist", ImVec2(0.0f, 300.0f), true);
    std::vector<std::string> dirs;
    std::vector<std::string> files;
    for (fs::directory_iterator it{m_saveAsPath, fs::directory_options::skip_permission_denied, ec};
         !ec && (it != fs::directory_iterator{}); it.increment(ec))
    {
        std::error_code typeEc;
        if (it->is_directory(typeEc))
        {
            dirs.push_back(it->path().filename().string());
        }
        else if (it->is_regular_file(typeEc))
        {
            files.push_back(it->path().filename().string());
        }
    }
    std::sort(dirs.begin(), dirs.end());
    std::sort(files.begin(), files.end());
    for (const std::string &name : dirs)
    {
        const std::string label{"[DIR] " + name};
        if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            m_saveAsPath = (fs::path{m_saveAsPath} / name).string();
        }
    }
    for (const std::string &name : files)
    {
        if (ImGui::Selectable(name.c_str()))
        {
            std::snprintf(m_saveAsName.data(), m_saveAsName.size(), "%s", name.c_str());
        }
    }
    ImGui::EndChild();

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##saveasname", m_saveAsName.data(), m_saveAsName.size());

    if (ImGui::Button("Save", ImVec2(100.0f, 0.0f)) && ('\0' != m_saveAsName[0]))
    {
        const fs::path chosen{fs::path{m_saveAsPath} / m_saveAsName.data()};
        (void)SaveConfigurationAs(chosen.string().c_str());
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace XilEnvImGui
