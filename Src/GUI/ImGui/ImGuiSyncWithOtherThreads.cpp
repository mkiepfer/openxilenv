#include "MainWinowSyncWithOtherThreads.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <utility>

#include "Platform.h"

#include "Framework/WindowManagerBridge.h"

extern "C" {
#include "Scheduler.h"
#include "RpcSocketServer.h"
}

namespace {

/**
 * @enum class GuiCommandType
 * @brief Tag identifying which GUI/core-seam request a queued command carries.
 */
enum class GuiCommandType : std::uint8_t
{
    OpenWindowByName,          /*!< Blocking, returns int. */
    IsWindowOpen,              /*!< Blocking, returns int. */
    CloseWindowByName,         /*!< Blocking, returns int. */
    SaveAllConfigToIniDataBase,/*!< Blocking, void. */
    SelectSheet,               /*!< Blocking, returns int. */
    AddSheet,                  /*!< Blocking, returns int. */
    DeleteSheet,               /*!< Blocking, returns int. */
    RenameSheet,               /*!< Blocking, returns int. */
    ImportHotkey,              /*!< Blocking, returns int. */
    ExportHotkey,              /*!< Blocking, returns int. */
    AddNewScriptErrorMessage,  /*!< Blocking, void. */
    ScriptCreateDialog,        /*!< Blocking, returns int. */
    ScriptAddDialogItem,       /*!< Blocking, returns int. */
    ScriptShowDialog,          /*!< Blocking, returns int. */
    ScriptDestroyDialog,       /*!< Blocking, returns int. */
    IsScriptDialogClosed,      /*!< Blocking, returns int. */
    RemoteProcedureCall,       /*!< Blocking, returns int (executes RPC decode). */
    SchedulerStateChanged,     /*!< Fire-and-forget. */
    SetNotFasterThanRealtime,  /*!< Fire-and-forget. */
    SetSuppressDisplayNonExist,/*!< Fire-and-forget. */
    DeactivateBreakpoint,      /*!< Fire-and-forget. */
    StopOscilloscope           /*!< Fire-and-forget. */
};

/**
 * @struct GuiCommand
 * @brief Self-contained, tagged GUI request placed on the command queue.
 * @details String arguments are copied into an owning std::string so the
 *          enqueuing (scheduler/RPC) thread's buffers need not outlive the call.
 *          The RPC variant keeps raw pointers because the caller blocks until the
 *          command is executed, so the pointed-to buffers stay valid.
 */
struct GuiCommand
{
    GuiCommandType type{};
    int threadId{-1};
    bool blocking{false};

    std::string arg0{};
    std::string arg1{};
    int intArg0{0};

    void *ptr0{nullptr};
    const void *ptr1{nullptr};
    void *ptr2{nullptr};
    std::uint64_t u64Arg0{0u};
};

constexpr int MAX_WAIT_SLOTS{32};

struct WaitSlot
{
    int threadId{-1};
    bool waiting{false};
    int returnValue{0};
};

std::mutex g_mutex;                 /*!< Guards the queue and the wait slots. */
std::condition_variable g_wakeCond; /*!< Wakes blocked callers on ack. */
std::deque<GuiCommand> g_queue;     /*!< Pending commands, drained per frame. */
WaitSlot g_waitSlots[MAX_WAIT_SLOTS];
int g_guiThreadId{-1};
std::atomic<int> g_syncInitAndRunning{0};
int g_progressBarIdCounter{0};

int CurrentThreadId()
{
    return static_cast<int>(GetCurrentThreadId());
}

/**
 * @brief Reserves a wait slot for the calling thread. Caller must hold g_mutex.
 * @return Slot index, or -1 if all slots are in use.
 */
int AllocWaitSlot(int par_ThreadId)
{
    for (int i{0}; i < MAX_WAIT_SLOTS; ++i)
    {
        if (0 > g_waitSlots[i].threadId)
        {
            g_waitSlots[i].threadId = par_ThreadId;
            g_waitSlots[i].waiting = true;
            g_waitSlots[i].returnValue = 0;
            return i;
        }
    }
    return -1;
}

/**
 * @brief Wakes the slot owned by par_ThreadId with a return value. Holds g_mutex.
 */
void WakeThreadLocked(int par_ThreadId, int par_ReturnValue)
{
    for (int i{0}; i < MAX_WAIT_SLOTS; ++i)
    {
        if (par_ThreadId == g_waitSlots[i].threadId)
        {
            g_waitSlots[i].returnValue = par_ReturnValue;
            g_waitSlots[i].waiting = false;
        }
    }
    g_wakeCond.notify_all();
}

/**
 * @brief Enqueues a blocking command and blocks until the render thread acks it.
 * @param[in] par_Command Command to enqueue (threadId/blocking are set here).
 * @return The return value supplied by the matching ack, or -1 on error/reentry.
 */
int EnqueueBlocking(GuiCommand par_Command)
{
    const int tid{CurrentThreadId()};
    std::unique_lock<std::mutex> lock(g_mutex);

    // A blocking request issued from the render thread itself would deadlock
    // (it can only be serviced by the render thread's own drain). Return a
    // benign default instead, matching the Qt same-thread short circuit.
    if (tid == g_guiThreadId)
    {
        return 0;
    }

    par_Command.threadId = tid;
    par_Command.blocking = true;
    const int slot{AllocWaitSlot(tid)};
    if (0 > slot)
    {
        return -1;
    }
    g_queue.push_back(std::move(par_Command));

    g_wakeCond.wait(lock, [slot] { return !g_waitSlots[slot].waiting; });

    const int ret{g_waitSlots[slot].returnValue};
    g_waitSlots[slot].threadId = -1;
    return ret;
}

/**
 * @brief Enqueues a fire-and-forget command and returns immediately.
 */
void EnqueueFireAndForget(GuiCommand par_Command)
{
    par_Command.threadId = CurrentThreadId();
    par_Command.blocking = false;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_queue.push_back(std::move(par_Command));
}

/**
 * @brief Executes one drained command on the render thread and returns its
 *        result. M2 handlers are intentionally minimal (no window model yet);
 *        only the RPC command performs real work so remote automation keeps
 *        functioning.
 */
int ExecuteCommand(GuiCommand &par_Command)
{
    switch (par_Command.type)
    {
    case GuiCommandType::OpenWindowByName:
        // Cross-thread (script/RPC) open: create/show the window on the render
        // thread. The menu path opens windows directly and never routes here.
        return XilEnvImGui::WmOpenWindowByName(par_Command.arg0.c_str());

    case GuiCommandType::CloseWindowByName:
        return XilEnvImGui::WmCloseWindowByName(par_Command.arg0.c_str());

    case GuiCommandType::IsWindowOpen:
        return XilEnvImGui::WmIsWindowOpen(par_Command.arg0.c_str());

    case GuiCommandType::SelectSheet:
        return XilEnvImGui::WmSelectSheet(par_Command.arg0.c_str());

    case GuiCommandType::AddSheet:
        return XilEnvImGui::WmAddSheet(par_Command.arg0.c_str());

    case GuiCommandType::DeleteSheet:
        return XilEnvImGui::WmDeleteSheet(par_Command.arg0.c_str());

    case GuiCommandType::RenameSheet:
        return XilEnvImGui::WmRenameSheet(par_Command.arg0.c_str(), par_Command.arg1.c_str());

    case GuiCommandType::IsScriptDialogClosed:
        // No script dialog exists yet: report "closed" so a polling script
        // never blocks waiting for a window that will never open.
        return 1;

    case GuiCommandType::RemoteProcedureCall:
        return RemoteProcedureDecodeMessage(static_cast<RPC_CONNECTION *>(par_Command.ptr0),
                                            static_cast<RPC_API_BASE_MESSAGE *>(const_cast<void *>(par_Command.ptr1)),
                                            static_cast<RPC_API_BASE_MESSAGE_ACK *>(par_Command.ptr2));

    case GuiCommandType::StopOscilloscope:
        // Freeze the identified oscilloscope's acquisition (single-shot trigger /
        // external stop) on the render thread; matches the Qt StopOscilloscope slot.
        return XilEnvImGui::WmStopOscilloscope(par_Command.ptr0, par_Command.u64Arg0);

    default:
        // Every other seam request has no window model to act on yet.
        return 0;
    }
}

} // namespace

extern "C" {

void SetSyncWithGuiThreadIsInitAndRunning(void)
{
    g_guiThreadId = CurrentThreadId();
    g_syncInitAndRunning.store(1);
}

int GetSyncWithGuiThreadIsInitAndRunning(void)
{
    return g_syncInitAndRunning.load();
}

void DrainGuiCommandQueueFromOtherThread(void)
{
    std::deque<GuiCommand> local;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        local.swap(g_queue);
    }

    for (GuiCommand &cmd : local)
    {
        const int ret{ExecuteCommand(cmd)};
        if (cmd.blocking)
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            WakeThreadLocked(cmd.threadId, ret);
        }
    }
}

// -------------------------------------------------------------------------
// Blocking (int-returning) seam functions
// -------------------------------------------------------------------------

int OpenWindowByNameFromOtherThread(char *par_WindowName, int par_PosFlag, int par_XPos, int par_YPos,
                                    int par_SizeFlag, int par_Width, int par_Hight)
{
    (void)par_PosFlag;
    (void)par_XPos;
    (void)par_YPos;
    (void)par_SizeFlag;
    (void)par_Width;
    (void)par_Hight;
    GuiCommand cmd;
    cmd.type = GuiCommandType::OpenWindowByName;
    if (nullptr != par_WindowName)
    {
        cmd.arg0 = par_WindowName;
    }
    return EnqueueBlocking(std::move(cmd));
}

int IsWindowOpenFromOtherThread(char *par_WindowName)
{
    GuiCommand cmd;
    cmd.type = GuiCommandType::IsWindowOpen;
    if (nullptr != par_WindowName)
    {
        cmd.arg0 = par_WindowName;
    }
    return EnqueueBlocking(std::move(cmd));
}

int CloseWindowByNameFromOtherThread(char *par_WindowName)
{
    GuiCommand cmd;
    cmd.type = GuiCommandType::CloseWindowByName;
    if (nullptr != par_WindowName)
    {
        cmd.arg0 = par_WindowName;
    }
    return EnqueueBlocking(std::move(cmd));
}

void SaveAllConfigToIniDataBaseFromOtherThread(void)
{
    GuiCommand cmd;
    cmd.type = GuiCommandType::SaveAllConfigToIniDataBase;
    (void)EnqueueBlocking(std::move(cmd));
}

int SelectSheetFromOtherThread(char *par_SheetName)
{
    GuiCommand cmd;
    cmd.type = GuiCommandType::SelectSheet;
    if (nullptr != par_SheetName)
    {
        cmd.arg0 = par_SheetName;
    }
    return EnqueueBlocking(std::move(cmd));
}

int AddSheetFromOtherThread(char *par_SheetName)
{
    GuiCommand cmd;
    cmd.type = GuiCommandType::AddSheet;
    if (nullptr != par_SheetName)
    {
        cmd.arg0 = par_SheetName;
    }
    return EnqueueBlocking(std::move(cmd));
}

int DeleteSheetFromOtherThread(char *par_SheetName)
{
    GuiCommand cmd;
    cmd.type = GuiCommandType::DeleteSheet;
    if (nullptr != par_SheetName)
    {
        cmd.arg0 = par_SheetName;
    }
    return EnqueueBlocking(std::move(cmd));
}

int RenameSheetFromOtherThread(char *par_OldSheetName, char *par_NewSheetName)
{
    GuiCommand cmd;
    cmd.type = GuiCommandType::RenameSheet;
    if (nullptr != par_OldSheetName)
    {
        cmd.arg0 = par_OldSheetName;
    }
    if (nullptr != par_NewSheetName)
    {
        cmd.arg1 = par_NewSheetName;
    }
    return EnqueueBlocking(std::move(cmd));
}

int ImportHotkeyFromOtherThread(char *par_Filename)
{
    GuiCommand cmd;
    cmd.type = GuiCommandType::ImportHotkey;
    if (nullptr != par_Filename)
    {
        cmd.arg0 = par_Filename;
    }
    return EnqueueBlocking(std::move(cmd));
}

int ExportHotkeyFromOtherThread(char *par_Filename)
{
    GuiCommand cmd;
    cmd.type = GuiCommandType::ExportHotkey;
    if (nullptr != par_Filename)
    {
        cmd.arg0 = par_Filename;
    }
    return EnqueueBlocking(std::move(cmd));
}

void AddNewScriptErrorMessageFromOtherThread(int par_Level, int par_LineNr, const char *par_Filename,
                                             const char *par_Message)
{
    (void)par_LineNr;
    GuiCommand cmd;
    cmd.type = GuiCommandType::AddNewScriptErrorMessage;
    cmd.intArg0 = par_Level;
    if (nullptr != par_Filename)
    {
        cmd.arg0 = par_Filename;
    }
    if (nullptr != par_Message)
    {
        cmd.arg1 = par_Message;
    }
    (void)EnqueueBlocking(std::move(cmd));
}

int ScriptCreateDialogFromOtherThread(char *par_Headline)
{
    GuiCommand cmd;
    cmd.type = GuiCommandType::ScriptCreateDialog;
    if (nullptr != par_Headline)
    {
        cmd.arg0 = par_Headline;
    }
    return EnqueueBlocking(std::move(cmd));
}

int ScriptAddDialogItemFromOtherThread(char *par_Element, char *par_Label)
{
    GuiCommand cmd;
    cmd.type = GuiCommandType::ScriptAddDialogItem;
    if (nullptr != par_Element)
    {
        cmd.arg0 = par_Element;
    }
    if (nullptr != par_Label)
    {
        cmd.arg1 = par_Label;
    }
    return EnqueueBlocking(std::move(cmd));
}

int ScriptShowDialogFromOtherThread(void)
{
    GuiCommand cmd;
    cmd.type = GuiCommandType::ScriptShowDialog;
    return EnqueueBlocking(std::move(cmd));
}

int ScriptDestroyDialogFromOtherThread(void)
{
    GuiCommand cmd;
    cmd.type = GuiCommandType::ScriptDestroyDialog;
    return EnqueueBlocking(std::move(cmd));
}

int IsScriptDialogClosedFromOtherThread(void)
{
    GuiCommand cmd;
    cmd.type = GuiCommandType::IsScriptDialogClosed;
    return EnqueueBlocking(std::move(cmd));
}

int RemoteProcedureCallRequestFromOtherThread(void *par_Connection, const void *par_In, void *ret_Out)
{
    GuiCommand cmd;
    cmd.type = GuiCommandType::RemoteProcedureCall;
    cmd.ptr0 = par_Connection;
    cmd.ptr1 = par_In;
    cmd.ptr2 = ret_Out;
    return EnqueueBlocking(std::move(cmd));
}

// -------------------------------------------------------------------------
// Fire-and-forget (void) seam functions
// -------------------------------------------------------------------------

void SchedulerStateChanged(int par_State)
{
    GuiCommand cmd;
    cmd.type = GuiCommandType::SchedulerStateChanged;
    cmd.intArg0 = par_State;
    EnqueueFireAndForget(std::move(cmd));
}

void SetNotFasterThanRealtimeState(int par_State)
{
    GuiCommand cmd;
    cmd.type = GuiCommandType::SetNotFasterThanRealtime;
    cmd.intArg0 = par_State;
    EnqueueFireAndForget(std::move(cmd));
}

void SetSuppressDisplayNonExistValuesState(int par_State)
{
    GuiCommand cmd;
    cmd.type = GuiCommandType::SetSuppressDisplayNonExist;
    cmd.intArg0 = par_State;
    EnqueueFireAndForget(std::move(cmd));
}

void DeactivateBreakpointFromOtherThread(void)
{
    GuiCommand cmd;
    cmd.type = GuiCommandType::DeactivateBreakpoint;
    EnqueueFireAndForget(std::move(cmd));
}

void StopOscilloscopeFromOtherThread(void *par_OscilloscopeWidget, uint64_t par_Time)
{
    GuiCommand cmd;
    cmd.type = GuiCommandType::StopOscilloscope;
    cmd.ptr0 = par_OscilloscopeWidget;
    cmd.u64Arg0 = par_Time;
    EnqueueFireAndForget(std::move(cmd));
}

// -------------------------------------------------------------------------
// Immediate (non-queued) progress-bar functions - no ack in the ABI
// -------------------------------------------------------------------------

int OpenProgressBarFromOtherThread(const char *par_ProgressName)
{
    (void)par_ProgressName;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (0x7FFF <= ++g_progressBarIdCounter)
    {
        g_progressBarIdCounter = 1;
    }
    return g_progressBarIdCounter;
}

void SetProgressBarFromOtherThread(int par_ProgressBarID, int par_Value)
{
    (void)par_ProgressBarID;
    (void)par_Value;
}

void CloseProgressBarFromOtherThread(int par_ProgressBarID)
{
    (void)par_ProgressBarID;
}

// -------------------------------------------------------------------------
// Ack entry points - the render thread wakes blocked callers via these. The
// blocking handlers above wake through DrainGuiCommandQueueFromOtherThread(),
// so these public acks remain available for symmetry with the Qt ABI and for
// any code path that acks explicitly.
// -------------------------------------------------------------------------

void SchedulerStateChangedAck(int par_ThreadId) { (void)par_ThreadId; }

void OpenWindowByNameFromOtherThreadAck(int par_ThreadId, int par_RetrunValue)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    WakeThreadLocked(par_ThreadId, par_RetrunValue);
}

void IsWindowOpenFromOtherThreadAck(int par_ThreadId, int par_RetrunValue)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    WakeThreadLocked(par_ThreadId, par_RetrunValue);
}

void SaveAllConfigToIniDataBaseFromOtherThreadAck(int par_ThreadId)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    WakeThreadLocked(par_ThreadId, 0);
}

void CloseWindowByNameFromOtherThreadAck(int par_ThreadId, int par_RetrunValue)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    WakeThreadLocked(par_ThreadId, par_RetrunValue);
}

void SelectSheetFromOtherThreadAck(int par_ThreadId, int par_RetrunValue)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    WakeThreadLocked(par_ThreadId, par_RetrunValue);
}

void AddSheetFromOtherThreadAck(int par_ThreadId, int par_RetrunValue)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    WakeThreadLocked(par_ThreadId, par_RetrunValue);
}

void DeleteSheetFromOtherThreadAck(int par_ThreadId, int par_RetrunValue)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    WakeThreadLocked(par_ThreadId, par_RetrunValue);
}

void RenameSheetFromOtherThreadAck(int par_ThreadId, int par_RetrunValue)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    WakeThreadLocked(par_ThreadId, par_RetrunValue);
}

void ImportHotkeyFromOtherThreadAck(int par_ThreadId, int par_ReturnValue)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    WakeThreadLocked(par_ThreadId, par_ReturnValue);
}

void ExportHotkeyFromOtherThreadAck(int par_ThreadId, int par_ReturnValue)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    WakeThreadLocked(par_ThreadId, par_ReturnValue);
}

void AddNewScriptErrorMessageFromOtherThreadAck(int par_ThreadId)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    WakeThreadLocked(par_ThreadId, 0);
}

void ScriptCreateDialogFromOtherThreadAck(int par_ThreadId, int par_ReturnValue)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    WakeThreadLocked(par_ThreadId, par_ReturnValue);
}

void ScriptAddDialogItemFromOtherThreadAck(int par_ThreadId, int par_RetrunValue)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    WakeThreadLocked(par_ThreadId, par_RetrunValue);
}

void ScriptShowDialogFromOtherThreadAck(int par_ThreadId, int par_RetrunValue)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    WakeThreadLocked(par_ThreadId, par_RetrunValue);
}

void ScriptDestroyDialogFromOtherThreadAck(int par_ThreadId, int par_RetrunValue)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    WakeThreadLocked(par_ThreadId, par_RetrunValue);
}

void IsScriptDialogClosedFromOtherThreadAck(int par_ThreadId, int par_RetrunValue)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    WakeThreadLocked(par_ThreadId, par_RetrunValue);
}

void RemoteProcedureCallRequestFromOtherThreaddAck(int par_ThreadId, int par_RetrunValue)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    WakeThreadLocked(par_ThreadId, par_RetrunValue);
}

} // extern "C"
