#include <cstdint>
#include <cstdio>

#include "Platform.h"

#include "BlackboardObserver.h"
#include "ErrorDialog.h"
#include "IniFileDosNotExistDialog.h"
#include "MainWindow.h"
#include "MainWinowSyncWithOtherThreads.h"
#include "MessageWindow.h"
#include "ScriptDebuggingDialog.h"
#include "ScriptErrorDialog.h"

extern "C" {
#include "ConfigurablePrefix.h"
}

/*
 * GUI-side bridge stubs for the M2 ImGui skeleton.
 *
 * The C core (and the four non-NO_GUI border files linked into XilEnvImGui)
 * reference a handful of GUI symbols beyond the command-queue seam: the error
 * and message sinks, desktop load/save, the blackboard observer and the
 * A2L-calibration widget notifier. No windows/dialogs exist yet, so these are
 * headless-safe minimal implementations (print to the console where a user
 * would otherwise see a dialog). They are replaced by real ImGui widgets in
 * later migration steps.
 */

// Defined in Main.cpp (C++ linkage), wakes the main loop for shutdown.
void terminate_main_loop(int par_ExitCode, int par_ExitCodeValid);

namespace {
int g_mainThreadId{-1};
} // namespace

extern "C" {

// --- Error dialog ---------------------------------------------------------

void InitErrorDialog(void)
{
    g_mainThreadId = static_cast<int>(GetCurrentThreadId());
}

void TerminateErrorDialog(void)
{
}

void CheckErrorMessagesBeforeMainWindowIsOpen(void)
{
}

int IsMainThread(void)
{
    return (g_mainThreadId == static_cast<int>(GetCurrentThreadId())) ? 1 : 0;
}

int ErrorMessagesFromOtherThreadInst_Error(int par_Level, uint64_t par_Cycle, const char *par_HeaderString,
                                           const char *par_ErrorString, int par_IsGuiMainThreadFlag)
{
    (void)par_Level;
    (void)par_IsGuiMainThreadFlag;
    std::printf("Cycle %llu %s: %s\n", static_cast<unsigned long long>(par_Cycle),
                (nullptr != par_HeaderString) ? par_HeaderString : "",
                (nullptr != par_ErrorString) ? par_ErrorString : "");
    std::fflush(stdout);
    return IDOK;
}

// --- Message window -------------------------------------------------------

void AddMessages(const char *par_Messsage)
{
    if (nullptr != par_Messsage)
    {
        std::printf("%s\n", par_Messsage);
    }
}

void AddMessagesChar(char *par_Messsage)
{
    AddMessages(par_Messsage);
}

void CloseMessageWindow(void)
{
}

void InitMessageWindowCriticalSections(void)
{
}

// --- Main window / desktop ------------------------------------------------

void CloseFromOtherThread(int par_ExitCode, int par_ExitCodeValid)
{
    terminate_main_loop(par_ExitCode, par_ExitCodeValid);
}

void ClearDesktopFromOtherThread(void)
{
}

void LoadDesktopFromOtherThread(char *par_Filename)
{
    (void)par_Filename;
}

void SaveDesktopFromOtherThread(char *par_Filename)
{
    (void)par_Filename;
}

// --- Already-running gate -------------------------------------------------

int WaitUntilXilEnvCanStart(char *par_InstanceName, void *par_Application)
{
    (void)par_Application;
    const char *name = GetConfigurablePrefix(CONFIGURABLE_PREFIX_TYPE_PROGRAM_NAME);
    std::printf("%s with the instance \"%s\" already running. %s will be terminated immediately!\n",
                name, (nullptr != par_InstanceName) ? par_InstanceName : "", name);
    return 1;
}

// --- Ini-file-missing gate ------------------------------------------------

int IniFileDosNotExistDialog(char *SelectedIniFile, char *ret_NewSelectedIniFile, int par_MaxChars,
                             void *par_Application)
{
    (void)SelectedIniFile;
    (void)ret_NewSelectedIniFile;
    (void)par_MaxChars;
    (void)par_Application;
    // No file picker yet: reject, so XilEnv exits cleanly without a config.
    return -1;
}

// --- Blackboard observer --------------------------------------------------

void StartBlackboardObserver(void)
{
}

void StopBlackboardObserver(void)
{
}

// --- A2L calibration widget notifier --------------------------------------
// The two GlobalNotifiy* entry points are now provided by the real thread-marshalling
// bridge in Framework/A2LCalBridge.cpp (no longer stubbed here).

} // extern "C"

// --- Script debugging (C++ linkage, called from Script.cpp) ---------------

int IsScriptDebugWindowOpen(void)
{
    return 0;
}

int GetStopScriptAtStartDebuggingFlag(void)
{
    return 0;
}

// --- Script error sink (C++ linkage, called from the script interpreter) ---

void ScriptErrorDialog::ScriptErrorMsgDlgAddMsgFromOtherThread(int par_Level, int par_LineNr,
                                                               const char *par_Filename, const char *par_Message)
{
    AddNewScriptErrorMessageFromOtherThread(par_Level, par_LineNr, par_Filename, par_Message);
}
