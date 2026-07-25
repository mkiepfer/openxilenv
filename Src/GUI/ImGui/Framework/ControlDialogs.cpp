#include "Framework/ControlDialogs.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "imgui.h"

#include "MainWinowSyncWithOtherThreads.h"

extern "C" {
#include "Config.h"
#include "MainValues.h"
#include "IniDataBase.h"
#include "Scheduler.h"
#include "SchedBarrier.h"
#include "A2LLink.h"
#include "tcb.h"
}

namespace XilEnvImGui
{

namespace
{

constexpr int PROCESS_LIST_ENUM_FLAGS{2 | 0x100 | 0x200}; /*!< running + login + non-sleep, mirrors Qt. */
constexpr int RANGE_CONTROL_TEXT_MAXC{512};

/**
 * @brief The nine hotkey function labels indexed by function number 0..8.
 */
const std::array<const char *, 9> HOTKEY_FUNCTION_LABELS{{
    "none", "single shot equation", "start script", "stop script", "change sheet",
    "run control continue", "run control stop", "run control next one", "run control next xxx"}};

/**
 * @brief Hotkeys reserved by the system and rejected by the editor (mirrors NewHotkeyDialog).
 */
const std::array<const char *, 20> RESERVED_HOTKEYS{{
    "f1", "alt+f1", "shift+f1", "ctrl+f1", "alt+f4", "ctrl+f4", "ctrl+f6", "f8", "ctrl+f10",
    "shift+f10", "f12", "shift+f12", "alt+f", "alt+d", "alt+s", "alt+w", "alt+a", "alt+c",
    "alt+b", "ctrl+t"}};

/**
 * @brief Thread-priority combo entries (label + THREAD_PRIORITY_* value), matching the Qt combo.
 */
struct ThreadPriorityOption
{
    const char *label;
    int value;
};
const std::array<ThreadPriorityOption, 7> THREAD_PRIORITY_OPTIONS{{
    {"THREAD_PRIORITY_IDLE", THREAD_PRIORITY_IDLE},
    {"THREAD_PRIORITY_LOWEST", THREAD_PRIORITY_LOWEST},
    {"THREAD_PRIORITY_BELOW_NORMAL", THREAD_PRIORITY_BELOW_NORMAL},
    {"THREAD_PRIORITY_NORMAL", THREAD_PRIORITY_NORMAL},
    {"THREAD_PRIORITY_ABOVE_NORMAL", THREAD_PRIORITY_ABOVE_NORMAL},
    {"THREAD_PRIORITY_HIGHEST", THREAD_PRIORITY_HIGHEST},
    {"THREAD_PRIORITY_TIME_CRITICAL", THREAD_PRIORITY_TIME_CRITICAL}}};

/**
 * @brief Returns a human-readable label for a process state value.
 */
const char *ProcessStateLabel(int par_State)
{
    switch (par_State)
    {
    case PROCESS_AKTIV:
        return "active";
    case PROCESS_SLEEP:
        return "sleep";
    case PROCESS_NONSLEEP:
        return "no sleep able";
    case EX_PROCESS_REFERENCE:
        return "reference";
    case EX_PROCESS_INIT:
        return "init";
    case EX_PROCESS_TERMINATE:
        return "terminate";
    case EX_PROCSS_LOGIN:
        return "login";
    case EX_PROCESS_WAIT_FOR_END_OF_CYCLE_TO_GO_TO_TERM:
        return "go to terminate";
    default:
        return "unknown";
    }
}

/**
 * @brief Returns a human-readable label for a process type value.
 */
const char *ProcessTypeLabel(int par_Type)
{
    switch (par_Type)
    {
    case 0:
        return "internal";
    case 2:
        return "external";
    default:
        return "realtime";
    }
}

/**
 * @brief Splits a ';'-separated list into trimmed non-empty tokens.
 */
std::vector<std::string> SplitSemicolon(const std::string &par_List)
{
    std::vector<std::string> result;
    std::size_t start{0};
    while (start <= par_List.size())
    {
        const std::size_t pos{par_List.find(';', start)};
        const std::size_t end{(std::string::npos == pos) ? par_List.size() : pos};
        std::string token{par_List.substr(start, end - start)};
        while (!token.empty() && (' ' == token.front()))
        {
            token.erase(token.begin());
        }
        while (!token.empty() && (' ' == token.back()))
        {
            token.pop_back();
        }
        if (!token.empty())
        {
            result.push_back(token);
        }
        if (std::string::npos == pos)
        {
            break;
        }
        start = pos + 1u;
    }
    return result;
}

/**
 * @brief Joins tokens into a ';'-separated list.
 */
std::string JoinSemicolon(const std::vector<std::string> &par_Tokens)
{
    std::string result;
    for (const std::string &token : par_Tokens)
    {
        if (!result.empty())
        {
            result += ';';
        }
        result += token;
    }
    return result;
}

/**
 * @brief Reads a ';'-separated string ini entry into a std::string.
 */
std::string ReadIniString(const char *par_Section, const std::string &par_Entry, int par_Fd)
{
    char buffer[INI_MAX_LINE_LENGTH];
    buffer[0] = '\0';
    IniFileDataBaseReadString(par_Section, par_Entry.c_str(), "", buffer, sizeof(buffer), par_Fd);
    return std::string{buffer};
}

} // namespace

ControlDialogManager &ControlDialogManager::GetInstance()
{
    static ControlDialogManager s_instance;
    return s_instance;
}

// ---------------------------------------------------------------------------
// Shared lookups
// ---------------------------------------------------------------------------

void ControlDialogManager::LoadAvailableBarriers()
{
    m_availableBarriers.clear();
    const int fd{GetMainFileDescriptor()};
    for (int b{0}; ; ++b)
    {
        char entry[64];
        std::snprintf(entry, sizeof(entry), "Barrier_%i", b);
        const std::string line{ReadIniString("SchedulerBarriersConfiguration", entry, fd)};
        if (line.empty())
        {
            break;
        }
        const std::string name{line.substr(0, line.find(','))};
        m_availableBarriers.push_back(name);
    }
}

void ControlDialogManager::LoadSchedulerNames()
{
    m_schedulerNames.clear();
    m_schedulerNames.emplace_back("Scheduler");
    const int fd{GetMainFileDescriptor()};
    for (int s{1}; ; ++s)
    {
        char entry[64];
        std::snprintf(entry, sizeof(entry), "Scheduler_%i", s);
        const std::string name{ReadIniString(SCHED_INI_SECTION, entry, fd)};
        if (name.empty())
        {
            break;
        }
        m_schedulerNames.push_back(name);
    }
}

// ---------------------------------------------------------------------------
// Configure process
// ---------------------------------------------------------------------------

ControlDialogManager::ProcessConfigEntry
ControlDialogManager::ReadProcessInfos(const std::string &par_Name) const
{
    ProcessConfigEntry entry;
    entry.name = par_Name;

    const int fd{GetMainFileDescriptor()};
    const int pid{get_pid_by_name(par_Name.c_str())};
    entry.pid = pid;

    bool tcbValid{false};
    int prio{0};
    int cycles{0};
    int delay{0};
    if (0 < pid)
    {
        int type{0};
        int messageSize{0};
        char assignedScheduler[MAX_PATH];
        std::uint64_t bbMask{0};
        int state{0};
        std::uint64_t cyclesCounter{0};
        std::uint64_t cyclesTime{0};
        std::uint64_t cyclesTimeMax{0};
        std::uint64_t cyclesTimeMin{0};
        if (0 == get_process_info_ex(pid, nullptr, 0, &type, &prio, &cycles, &delay, &messageSize,
                                     assignedScheduler, sizeof(assignedScheduler), &bbMask, &state,
                                     &cyclesCounter, &cyclesTime, &cyclesTimeMax, &cyclesTimeMin))
        {
            entry.running = true;
            entry.state = state;
            entry.type = type;
            entry.mask = bbMask;
            entry.cycleCount = cyclesCounter;
            entry.cycleTime = cyclesTime;
            entry.cycleTimeMax = cyclesTimeMax;
            entry.cycleTimeMin = cyclesTimeMin;
            tcbValid = true;
        }
    }

    int iniPriority{0};
    int iniTimeSteps{0};
    int iniDelay{0};
    int iniTimeout{0};
    char rangeErrorCounter[RANGE_CONTROL_TEXT_MAXC];
    char rangeControl[RANGE_CONTROL_TEXT_MAXC];
    unsigned int rangeControlFlags{0};
    char bbPrefix[RANGE_CONTROL_TEXT_MAXC];
    char schedulerName[MAX_SCHEDULER_NAME_LENGTH];
    char barBeforeOnly[INI_MAX_LINE_LENGTH];
    char barBeforeWait[INI_MAX_LINE_LENGTH];
    char barBehindOnly[INI_MAX_LINE_LENGTH];
    char barBehindWait[INI_MAX_LINE_LENGTH];
    char barLoopBeforeOnly[INI_MAX_LINE_LENGTH];
    char barLoopBeforeWait[INI_MAX_LINE_LENGTH];
    char barLoopBehindOnly[INI_MAX_LINE_LENGTH];
    char barLoopBehindWait[INI_MAX_LINE_LENGTH];
    rangeErrorCounter[0] = '\0';
    rangeControl[0] = '\0';
    bbPrefix[0] = '\0';
    schedulerName[0] = '\0';
    barBeforeOnly[0] = barBeforeWait[0] = barBehindOnly[0] = barBehindWait[0] = '\0';
    barLoopBeforeOnly[0] = barLoopBeforeWait[0] = barLoopBehindOnly[0] = barLoopBehindWait[0] = '\0';

    read_extprocinfos_from_ini(fd, par_Name.c_str(), &iniPriority, &iniTimeSteps, &iniDelay, &iniTimeout,
                               rangeErrorCounter, sizeof(rangeErrorCounter), rangeControl,
                               sizeof(rangeControl), &rangeControlFlags, bbPrefix, sizeof(bbPrefix),
                               schedulerName, sizeof(schedulerName), barBeforeOnly, sizeof(barBeforeOnly),
                               barBeforeWait, sizeof(barBeforeWait), barBehindOnly, sizeof(barBehindOnly),
                               barBehindWait, sizeof(barBehindWait), barLoopBeforeOnly,
                               sizeof(barLoopBeforeOnly), barLoopBeforeWait, sizeof(barLoopBeforeWait),
                               barLoopBehindOnly, sizeof(barLoopBehindOnly), barLoopBehindWait,
                               sizeof(barLoopBehindWait));

    entry.priority = iniPriority;
    entry.timeSteps = iniTimeSteps;
    entry.delay = iniDelay;
    entry.timeout = iniTimeout;
    if (tcbValid)
    {
        // A running process wins: use the live TCB scheduling values.
        entry.priority = prio;
        entry.timeSteps = cycles;
        entry.delay = delay;
    }

    entry.rcBeforeActive = (0u != (rangeControlFlags & RANGE_CONTROL_BEFORE_ACTIVE_FLAG));
    entry.rcBehindActive = (0u != (rangeControlFlags & RANGE_CONTROL_BEHIND_ACTIVE_FLAG));
    entry.rcBeforeDeactive = (0u != (rangeControlFlags & RANGE_CONTROL_BEFORE_INIT_AS_DEACTIVE));
    entry.rcBehindDeactive = (0u != (rangeControlFlags & RANGE_CONTROL_BEHIND_INIT_AS_DEACTIVE));
    entry.rcStopSchedFlag = (0u != (rangeControlFlags & RANGE_CONTROL_STOP_SCHEDULER_FLAG));
    entry.rcOutput = static_cast<int>(rangeControlFlags & RANGE_CONTROL_OUTPUT_MASK);
    entry.rcErrorCounterFlag = (0u != (rangeControlFlags & RANGE_CONTROL_COUNTER_VARIABLE_FLAG));
    entry.rcVarFlag = (0u != (rangeControlFlags & RANGE_CONTROL_CONTROL_VARIABLE_FLAG));
    entry.rcPhysFlag = (0u != (rangeControlFlags & RANGE_CONTROL_PHYSICAL_FLAG));
    entry.rcLimitValuesFlag = (0u != (rangeControlFlags & RANGE_CONTROL_LIMIT_VALUES_FLAG));
    entry.rcErrorCounter = rangeErrorCounter;
    entry.rcControl = rangeControl;
    entry.bbPrefix = bbPrefix;
    entry.schedulerName = schedulerName;
    entry.barBeforeOnly = barBeforeOnly;
    entry.barBeforeWait = barBeforeWait;
    entry.barBehindOnly = barBehindOnly;
    entry.barBehindWait = barBehindWait;
    entry.barLoopBeforeOnly = barLoopBeforeOnly;
    entry.barLoopBeforeWait = barLoopBeforeWait;
    entry.barLoopBehindOnly = barLoopBehindOnly;
    entry.barLoopBehindWait = barLoopBehindWait;

    char equBefore[512];
    char equBehind[512];
    char svlFile[512];
    char a2lFile[512];
    equBefore[0] = equBehind[0] = svlFile[0] = a2lFile[0] = '\0';
    GetBeforeProcessEquationFileName(par_Name.c_str(), equBefore);
    GetBehindProcessEquationFileName(par_Name.c_str(), equBehind);
    GetSVLFileLoadedBeforeInitProcessFileName(par_Name.c_str(), svlFile, sizeof(svlFile));
    int a2lFlags{0};
    GetA2LFileAssociatedProcessFileName(par_Name.c_str(), a2lFile, sizeof(a2lFile), &a2lFlags);
    entry.equationBefore = equBefore;
    entry.equationBehind = equBehind;
    entry.svlFile = svlFile;
    entry.a2lFile = a2lFile;
    entry.a2lFlags = a2lFlags;

    char longName[MAX_PATH];
    longName[0] = '\0';
    GetProcessLongName(pid, longName, sizeof(longName));
    entry.longName = longName;

    return entry;
}

void ControlDialogManager::SnapshotProcesses()
{
    LoadAvailableBarriers();
    LoadSchedulerNames();

    m_processes.clear();
    m_processesSave.clear();

    // Running processes first (mirrors ReadAllProcessesInfos).
    READ_NEXT_PROCESS_NAME *buffer{init_read_next_process_name(PROCESS_LIST_ENUM_FLAGS)};
    if (nullptr != buffer)
    {
        char *name{nullptr};
        while (nullptr != (name = read_next_process_name(buffer)))
        {
            m_processes.push_back(ReadProcessInfos(std::string{name}));
        }
        close_read_next_process_name(buffer);
    }

    // Optionally the configured-but-not-running processes from the ini.
    if (m_processShowNonRunning)
    {
        const int fd{GetMainFileDescriptor()};
        char entry[INI_MAX_ENTRYNAME_LENGTH];
        int entryIdx{0};
        static const std::array<const char *, 16> skipPrefixes{{
            "Equation before", "Equation behind", "BBPrefixForProcess", "SchedulerForProcess",
            "BarriersBefore", "BarriersBehind", "BarriersLoopOut", "SVL before", "A2L associated",
            "Scheduler_", "PriorityForScheduler", "Period", "LoginTimeout", "Login Timeout",
            "SyncWithFlexray", "BarriersBeforeOnlySignalForScheduler"}};
        while (0 <= (entryIdx = IniFileDataBaseGetNextEntryName(entryIdx, SCHED_INI_SECTION, entry,
                                                                sizeof(entry), fd)))
        {
            bool skip{false};
            for (const char *prefix : skipPrefixes)
            {
                if (0 == std::strncmp(entry, prefix, std::strlen(prefix)))
                {
                    skip = true;
                    break;
                }
            }
            if (skip)
            {
                continue;
            }
            const std::string name{entry};
            const bool present{std::any_of(m_processes.begin(), m_processes.end(),
                                           [&name](const ProcessConfigEntry &e) { return e.name == name; })};
            if (!present)
            {
                m_processes.push_back(ReadProcessInfos(name));
            }
        }
    }

    for (const ProcessConfigEntry &e : m_processes)
    {
        m_processesSave.push_back(e.name);
    }
    m_processSelected = m_processes.empty() ? -1 : 0;
}

void ControlDialogManager::StoreProcessInfos(const ProcessConfigEntry &par_Entry) const
{
    const int fd{GetMainFileDescriptor()};
    const int rcBefore{(par_Entry.rcBeforeActive ? 1 : 0) | (par_Entry.rcBeforeDeactive ? 2 : 0)};
    const int rcBehind{(par_Entry.rcBehindActive ? 1 : 0) | (par_Entry.rcBehindDeactive ? 2 : 0)};

    write_extprocinfos_to_ini(fd, par_Entry.name.c_str(), par_Entry.priority, par_Entry.timeSteps,
                              par_Entry.delay, par_Entry.timeout, rcBefore, rcBehind,
                              par_Entry.rcStopSchedFlag ? 1 : 0, par_Entry.rcOutput,
                              par_Entry.rcErrorCounterFlag ? 1 : 0, par_Entry.rcErrorCounter.c_str(),
                              par_Entry.rcVarFlag ? 1 : 0, par_Entry.rcControl.c_str(),
                              par_Entry.rcPhysFlag ? 1 : 0, par_Entry.rcLimitValuesFlag ? 1 : 0,
                              par_Entry.bbPrefix.c_str(), par_Entry.schedulerName.c_str(),
                              par_Entry.barBeforeOnly.c_str(), par_Entry.barBeforeWait.c_str(),
                              par_Entry.barBehindOnly.c_str(), par_Entry.barBehindWait.c_str(),
                              par_Entry.barLoopBeforeOnly.c_str(), par_Entry.barLoopBeforeWait.c_str(),
                              par_Entry.barLoopBehindOnly.c_str(), par_Entry.barLoopBehindWait.c_str());

    SetBeforeProcessEquationFileName(par_Entry.name.c_str(), par_Entry.equationBefore.c_str());
    SetBehindProcessEquationFileName(par_Entry.name.c_str(), par_Entry.equationBehind.c_str());
    SetSVLFileLoadedBeforeInitProcessFileName(par_Entry.name.c_str(), par_Entry.svlFile.c_str(), 1);
    // Persist the A2L association (ini). The live load/reload of the A2L link that the Qt dialog
    // performs for a running process is a runtime action and is deferred (see report).
    SetA2LFileAssociatedToProcessFileName(par_Entry.name.c_str(), par_Entry.a2lFile.c_str(),
                                          par_Entry.a2lFlags, 1);
}

void ControlDialogManager::SaveAllProcesses()
{
    const int fd{GetMainFileDescriptor()};

    // Delete processes that were removed from the list.
    for (const std::string &saved : m_processesSave)
    {
        const bool present{std::any_of(m_processes.begin(), m_processes.end(),
                                       [&saved](const ProcessConfigEntry &e) { return e.name == saved; })};
        if (!present)
        {
            SetBeforeProcessEquationFileName(saved.c_str(), nullptr);
            SetBehindProcessEquationFileName(saved.c_str(), nullptr);
            SetSVLFileLoadedBeforeInitProcessFileName(saved.c_str(), nullptr, 1);
            IniFileDataBaseWriteString(SCHED_INI_SECTION, saved.c_str(), nullptr, fd);
            RemoveProcessOrSchedulerFromAllBarrierIniConfig(saved.c_str(), 1);
        }
    }

    for (const ProcessConfigEntry &e : m_processes)
    {
        if (e.changed)
        {
            StoreProcessInfos(e);
        }
    }
}

// ---------------------------------------------------------------------------
// Configure schedulers
// ---------------------------------------------------------------------------

ControlDialogManager::SchedulerConfigEntry
ControlDialogManager::ReadSchedulerInfos(const std::string &par_Name) const
{
    SchedulerConfigEntry entry;
    entry.name = par_Name;
    const int fd{GetMainFileDescriptor()};

    entry.barBeforeOnly = ReadIniString(SCHED_INI_SECTION,
                                        "BarriersBeforeOnlySignalForScheduler " + par_Name, fd);
    entry.barBeforeWait = ReadIniString(SCHED_INI_SECTION,
                                        "BarriersBeforeSignalAndWaitForScheduler " + par_Name, fd);
    entry.barBehindOnly = ReadIniString(SCHED_INI_SECTION,
                                        "BarriersBehindOnlySignalForScheduler " + par_Name, fd);
    entry.barBehindWait = ReadIniString(SCHED_INI_SECTION,
                                        "BarriersBehindSignalAndWaitForScheduler " + par_Name, fd);
    entry.priority = IniFileDataBaseReadInt(SCHED_INI_SECTION,
                                            ("PriorityForScheduler " + par_Name).c_str(),
                                            THREAD_PRIORITY_IDLE, fd);
    if (THREAD_PRIORITY_IDLE > entry.priority)
    {
        entry.priority = THREAD_PRIORITY_IDLE;
    }
    if (THREAD_PRIORITY_TIME_CRITICAL < entry.priority)
    {
        entry.priority = THREAD_PRIORITY_TIME_CRITICAL;
    }
    return entry;
}

void ControlDialogManager::SnapshotSchedulers()
{
    LoadAvailableBarriers();
    m_schedulers.clear();
    m_schedulersSave.clear();

    m_schedulers.push_back(ReadSchedulerInfos("Scheduler"));
    const int fd{GetMainFileDescriptor()};
    for (int x{1}; ; ++x)
    {
        char entry[64];
        std::snprintf(entry, sizeof(entry), "Scheduler_%i", x);
        const std::string name{ReadIniString(SCHED_INI_SECTION, entry, fd)};
        if (name.empty())
        {
            break;
        }
        m_schedulers.push_back(ReadSchedulerInfos(name));
    }

    for (const SchedulerConfigEntry &e : m_schedulers)
    {
        m_schedulersSave.push_back(e.name);
    }
    m_schedulerSelected = m_schedulers.empty() ? -1 : 0;
}

void ControlDialogManager::StoreSchedulerInfos(const SchedulerConfigEntry &par_Entry) const
{
    const int fd{GetMainFileDescriptor()};
    IniFileDataBaseWriteString(SCHED_INI_SECTION,
                               ("BarriersBeforeOnlySignalForScheduler " + par_Entry.name).c_str(),
                               par_Entry.barBeforeOnly.c_str(), fd);
    IniFileDataBaseWriteString(SCHED_INI_SECTION,
                               ("BarriersBeforeSignalAndWaitForScheduler " + par_Entry.name).c_str(),
                               par_Entry.barBeforeWait.c_str(), fd);
    IniFileDataBaseWriteString(SCHED_INI_SECTION,
                               ("BarriersBehindOnlySignalForScheduler " + par_Entry.name).c_str(),
                               par_Entry.barBehindOnly.c_str(), fd);
    IniFileDataBaseWriteString(SCHED_INI_SECTION,
                               ("BarriersBehindSignalAndWaitForScheduler " + par_Entry.name).c_str(),
                               par_Entry.barBehindWait.c_str(), fd);
    IniFileDataBaseWriteInt(SCHED_INI_SECTION, ("PriorityForScheduler " + par_Entry.name).c_str(),
                            par_Entry.priority, fd);
}

void ControlDialogManager::SaveAllSchedulers()
{
    const int fd{GetMainFileDescriptor()};

    // Delete schedulers that were removed from the list.
    for (const std::string &saved : m_schedulersSave)
    {
        const bool present{std::any_of(m_schedulers.begin(), m_schedulers.end(),
                                       [&saved](const SchedulerConfigEntry &e) { return e.name == saved; })};
        if (!present)
        {
            IniFileDataBaseWriteString(SCHED_INI_SECTION,
                                       ("BarriersBeforeOnlySignalForScheduler " + saved).c_str(),
                                       nullptr, fd);
            IniFileDataBaseWriteString(SCHED_INI_SECTION,
                                       ("BarriersBeforeSignalAndWaitForScheduler " + saved).c_str(),
                                       nullptr, fd);
            IniFileDataBaseWriteString(SCHED_INI_SECTION,
                                       ("BarriersBehindOnlySignalForScheduler " + saved).c_str(),
                                       nullptr, fd);
            IniFileDataBaseWriteString(SCHED_INI_SECTION,
                                       ("BarriersBehindSignalAndWaitForScheduler " + saved).c_str(),
                                       nullptr, fd);
            IniFileDataBaseWriteString(SCHED_INI_SECTION,
                                       ("PriorityForScheduler " + saved).c_str(), nullptr, fd);
            RemoveProcessOrSchedulerFromAllBarrierIniConfig(saved.c_str(), 0);
        }
    }

    // Rewrite the Scheduler_%i name list from the current non-main schedulers.
    for (int x{1}; ; ++x)
    {
        char entry[64];
        std::snprintf(entry, sizeof(entry), "Scheduler_%i", x);
        if (ReadIniString(SCHED_INI_SECTION, entry, fd).empty())
        {
            break;
        }
        IniFileDataBaseWriteString(SCHED_INI_SECTION, entry, nullptr, fd);
    }
    int idx{1};
    for (const SchedulerConfigEntry &e : m_schedulers)
    {
        if ("Scheduler" != e.name)
        {
            char entry[64];
            std::snprintf(entry, sizeof(entry), "Scheduler_%i", idx);
            IniFileDataBaseWriteString(SCHED_INI_SECTION, entry, e.name.c_str(), fd);
            ++idx;
        }
    }

    for (const SchedulerConfigEntry &e : m_schedulers)
    {
        StoreSchedulerInfos(e);
    }
}

// ---------------------------------------------------------------------------
// Configure barriers
// ---------------------------------------------------------------------------

void ControlDialogManager::SnapshotBarriers()
{
    m_barriers.clear();
    m_barriersSave.clear();
    const int fd{GetMainFileDescriptor()};
    for (int b{0}; ; ++b)
    {
        char entry[64];
        std::snprintf(entry, sizeof(entry), "Barrier_%i", b);
        const std::string line{ReadIniString("SchedulerBarriersConfiguration", entry, fd)};
        if (line.empty())
        {
            break;
        }
        BarrierConfigEntry barrier;
        const std::size_t comma{line.find(',')};
        barrier.name = line.substr(0, comma);
        if (std::string::npos != comma)
        {
            const std::string rest{line.substr(comma + 1u)};
            barrier.waitIfAlone = (std::string::npos != rest.find("WaitIfAlone"));
        }
        m_barriers.push_back(barrier);
        m_barriersSave.push_back(barrier.name);
    }
    m_barrierNewName[0] = '\0';
    m_barrierNewWaitIfAlone = false;
}

void ControlDialogManager::SaveAllBarriers()
{
    // Delete barriers no longer present.
    for (const std::string &saved : m_barriersSave)
    {
        const bool present{std::any_of(m_barriers.begin(), m_barriers.end(),
                                       [&saved](const BarrierConfigEntry &e) { return e.name == saved; })};
        if (!present)
        {
            DeleteBarrierFromIni(saved.c_str());
        }
    }
    // Add newly created barriers.
    for (const BarrierConfigEntry &e : m_barriers)
    {
        const bool existed{std::any_of(m_barriersSave.begin(), m_barriersSave.end(),
                                       [&e](const std::string &s) { return s == e.name; })};
        if (!existed)
        {
            std::string toAdd{e.name};
            if (e.waitIfAlone)
            {
                toAdd += ",WaitIfAlone";
            }
            AddNewBarrierToIni(toAdd.c_str());
        }
    }
}

// ---------------------------------------------------------------------------
// Hotkeys
// ---------------------------------------------------------------------------

void ControlDialogManager::SnapshotHotkeys()
{
    m_hotkeys.clear();
    const int fd{GetMainFileDescriptor()};

    // Function keys F2..F12 with Pure/Alt/Control/Shift modifiers.
    static const std::array<std::pair<const char *, const char *>, 4> fKeyMods{{
        {"Pure", ""}, {"Alt", "Alt+"}, {"Control", "Ctrl+"}, {"Shift", "Shift+"}}};
    for (int y{2}; y < 13; ++y)
    {
        for (const auto &mod : fKeyMods)
        {
            char entry[32];
            std::snprintf(entry, sizeof(entry), "%s F%i", mod.first, y);
            char value[INI_MAX_LINE_LENGTH];
            value[0] = '\0';
            IniFileDataBaseReadString("GUI/FunctionHotkeys", entry, "", value, sizeof(value), fd);
            char *endptr{nullptr};
            const int func{static_cast<int>(std::strtol(value, &endptr, 0))};
            if ((nullptr != endptr) && (',' == *endptr) && (0 != func))
            {
                HotkeyEntry hotkey;
                char display[64];
                std::snprintf(display, sizeof(display), "%sF%i", mod.second, y);
                hotkey.hotkey = display;
                hotkey.function = func;
                hotkey.formula = (endptr + 1);
                m_hotkeys.push_back(hotkey);
            }
        }
    }

    // Alt/Control + letters a..z and digits 0..9.
    static const std::array<std::pair<const char *, const char *>, 2> charMods{{
        {"Alt", "Alt+"}, {"Control", "Ctrl+"}}};
    for (int range{0}; range < 2; ++range)
    {
        const int start{(0 == range) ? 'a' : '0'};
        const int stop{(0 == range) ? 'z' : '9'};
        for (int y{start}; y <= stop; ++y)
        {
            for (const auto &mod : charMods)
            {
                char entry[32];
                std::snprintf(entry, sizeof(entry), "%s %c", mod.first, static_cast<char>(y));
                char value[INI_MAX_LINE_LENGTH];
                value[0] = '\0';
                IniFileDataBaseReadString("GUI/FunctionHotkeys", entry, "", value, sizeof(value), fd);
                char *endptr{nullptr};
                const int func{static_cast<int>(std::strtol(value, &endptr, 0))};
                if (nullptr != endptr)
                {
                    if (',' == *endptr)
                    {
                        ++endptr;
                    }
                    while ((0 != *endptr) && (0 != std::isspace(static_cast<unsigned char>(*endptr))))
                    {
                        ++endptr;
                    }
                }
                if (0 != func)
                {
                    HotkeyEntry hotkey;
                    char display[64];
                    const int upper{(0 == range) ? (y - 32) : y};
                    std::snprintf(display, sizeof(display), "%s%c", mod.second, static_cast<char>(upper));
                    hotkey.hotkey = display;
                    hotkey.function = func;
                    hotkey.formula = (nullptr != endptr) ? endptr : "";
                    m_hotkeys.push_back(hotkey);
                }
            }
        }
    }

    m_hotkeySelected = m_hotkeys.empty() ? -1 : 0;
}

void ControlDialogManager::SaveAllHotkeys()
{
    const int fd{GetMainFileDescriptor()};

    const auto findHotkey{[this](const std::string &par_Display) -> const HotkeyEntry * {
        for (const HotkeyEntry &e : m_hotkeys)
        {
            std::string a{e.hotkey};
            std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            std::string b{par_Display};
            std::transform(b.begin(), b.end(), b.begin(), ::tolower);
            if (a == b)
            {
                return &e;
            }
        }
        return nullptr;
    }};

    static const std::array<std::pair<const char *, const char *>, 4> fKeyMods{{
        {"Pure", ""}, {"Alt", "Alt+"}, {"Control", "Ctrl+"}, {"Shift", "Shift+"}}};
    for (int y{2}; y < 13; ++y)
    {
        for (const auto &mod : fKeyMods)
        {
            char entry[32];
            std::snprintf(entry, sizeof(entry), "%s F%i", mod.first, y);
            char display[64];
            std::snprintf(display, sizeof(display), "%sF%i", mod.second, y);
            char value[INI_MAX_LINE_LENGTH + 16];
            const HotkeyEntry *hotkey{findHotkey(display)};
            if (nullptr != hotkey)
            {
                std::snprintf(value, sizeof(value), "%i,%s", hotkey->function, hotkey->formula.c_str());
            }
            else
            {
                std::snprintf(value, sizeof(value), "0,");
            }
            IniFileDataBaseWriteString("GUI/FunctionHotkeys", entry, value, fd);
        }
    }

    static const std::array<std::pair<const char *, const char *>, 2> charMods{{
        {"Alt", "Alt+"}, {"Control", "Ctrl+"}}};
    for (int range{0}; range < 2; ++range)
    {
        const int start{(0 == range) ? 'a' : '0'};
        const int stop{(0 == range) ? 'z' : '9'};
        for (int y{start}; y <= stop; ++y)
        {
            for (const auto &mod : charMods)
            {
                char entry[32];
                std::snprintf(entry, sizeof(entry), "%s %c", mod.first, static_cast<char>(y));
                char display[64];
                const int upper{(0 == range) ? (y - 32) : y};
                std::snprintf(display, sizeof(display), "%s%c", mod.second, static_cast<char>(upper));
                char value[INI_MAX_LINE_LENGTH + 16];
                const HotkeyEntry *hotkey{findHotkey(display)};
                if (nullptr != hotkey)
                {
                    std::snprintf(value, sizeof(value), "%i,%s", hotkey->function,
                                  hotkey->formula.c_str());
                }
                else
                {
                    std::snprintf(value, sizeof(value), "0,");
                }
                IniFileDataBaseWriteString("GUI/FunctionHotkeys", entry, value, fd);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Public open helpers
// ---------------------------------------------------------------------------

void ControlDialogManager::OpenConfigureProcess()
{
    m_processShowNonRunning = false;
    SnapshotProcesses();
    m_processOpen = true;
    m_processOpenRequested = true;
}

void ControlDialogManager::OpenConfigureSchedulers()
{
    SnapshotSchedulers();
    m_schedulerOpen = true;
    m_schedulerOpenRequested = true;
}

void ControlDialogManager::OpenConfigureBarriers()
{
    SnapshotBarriers();
    m_barrierOpen = true;
    m_barrierOpenRequested = true;
}

void ControlDialogManager::OpenHotkeys()
{
    SnapshotHotkeys();
    m_hotkeyOpen = true;
    m_hotkeyOpenRequested = true;
}

void ControlDialogManager::OpenAbout()
{
    char help[512];
    const int exeAddrSize{8 * static_cast<int>(sizeof(void *))};
    const char *systemName{
#ifdef _WIN32
        "Win"
#elif defined(__linux__)
        "Linux"
#else
        "Unknown"
#endif
    };

    m_aboutText.clear();
#if (XILENV_MINOR_VERSION == 0)
    std::snprintf(help, sizeof(help), "Version %.3lf.final %s%i (build %s)\n",
                  XILENV_VERSION / 1000.0, systemName, exeAddrSize, __DATE__);
#elif (XILENV_MINOR_VERSION < 0)
    std::snprintf(help, sizeof(help), "Version %.3lf.pre-%d %s%i (build %s)\n",
                  XILENV_VERSION / 1000.0, -XILENV_MINOR_VERSION, systemName, exeAddrSize, __DATE__);
#else
    std::snprintf(help, sizeof(help), "Version %d.%d.%d %s%i (build %s)\n", XILENV_VERSION,
                  XILENV_MINOR_VERSION, XILENV_PATCH_VERSION, systemName, exeAddrSize, __DATE__);
#endif
    m_aboutText += help;

    const char *year{__DATE__};
    year += std::strlen(__DATE__) - 4;
    std::snprintf(help, sizeof(help), "Copyright  1997-%s ZF Friedrichshafen AG\n\n", year);
    m_aboutText += help;
    m_aboutText +=
        "Licensed under the Apache License, Version 2.0 (the \"License\");\n"
        "you may not use this file except in compliance with the License.\n"
        "You may obtain a copy of the License at\n\n"
        "    http://www.apache.org/licenses/LICENSE-2.0\n\n"
        "Unless required by applicable law or agreed to in writing, software\n"
        "distributed under the License is distributed on an \"AS IS\" BASIS\n"
        "WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.\n"
        "See the License for the specific language governing permissions and\n"
        "limitations under the License.\n";
#ifdef _MSC_FULL_VER
    std::snprintf(help, sizeof(help), "\nCompiled with Visual C compiler version %i\n", _MSC_FULL_VER);
#else
    std::snprintf(help, sizeof(help), "\nCompiled with GCC compiler version %i.%i.%i\n", __GNUC__,
                  __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#endif
    m_aboutText += help;
    m_aboutText += "\nGUI front-end: Dear ImGui (GLFW + OpenGL3)\n";
    m_aboutText += "\nSupported target compiler:\n GCC";

    m_aboutOpen = true;
    m_aboutOpenRequested = true;
}

// ---------------------------------------------------------------------------
// Shared widgets
// ---------------------------------------------------------------------------

void ControlDialogManager::RenderBarrierListEditor(const char *par_Id, std::string &par_List)
{
    std::vector<std::string> tokens{SplitSemicolon(par_List)};
    bool changed{false};

    ImGui::PushID(par_Id);
    if (ImGui::BeginChild("##list", ImVec2(0.0f, 68.0f), true))
    {
        for (std::size_t i{0}; i < tokens.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::SmallButton("x"))
            {
                tokens.erase(tokens.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                ImGui::PopID();
                break;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(tokens[i].c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    if (ImGui::BeginCombo("##add", "add barrier..."))
    {
        for (const std::string &available : m_availableBarriers)
        {
            const bool alreadyIn{std::any_of(tokens.begin(), tokens.end(),
                                             [&available](const std::string &t) { return t == available; })};
            if (!alreadyIn && ImGui::Selectable(available.c_str()))
            {
                tokens.push_back(available);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();

    if (changed)
    {
        par_List = JoinSemicolon(tokens);
    }
}

void ControlDialogManager::OpenFilePicker(std::string *par_pTarget, const char *par_Extension)
{
    m_pFilePickerTarget = par_pTarget;
    m_filePickerExt = (nullptr != par_Extension) ? par_Extension : "";
    std::error_code ec;
    std::filesystem::path start{*par_pTarget};
    if (!par_pTarget->empty() && std::filesystem::exists(start.parent_path(), ec))
    {
        m_filePickerPath = start.parent_path().string();
    }
    else
    {
        m_filePickerPath = std::filesystem::current_path(ec).string();
    }
    m_filePickerName[0] = '\0';
    m_filePickerOpenRequested = true;
}

void ControlDialogManager::RenderFilePicker()
{
    namespace fs = std::filesystem;
    if (m_filePickerOpenRequested)
    {
        ImGui::OpenPopup("Select file###ControlFilePicker");
        m_filePickerOpenRequested = false;
    }
    ImGui::SetNextWindowSize(ImVec2(560.0f, 460.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Select file###ControlFilePicker", nullptr,
                                ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }

    std::error_code ec;
    ImGui::TextUnformatted(m_filePickerPath.c_str());
    if (ImGui::Button(".. (up)"))
    {
        const fs::path parent{fs::path{m_filePickerPath}.parent_path()};
        if (!parent.empty())
        {
            m_filePickerPath = parent.string();
        }
    }

    ImGui::BeginChild("##filelist", ImVec2(0.0f, 300.0f), true);
    std::vector<std::string> dirs;
    std::vector<std::string> files;
    for (fs::directory_iterator it{m_filePickerPath, fs::directory_options::skip_permission_denied, ec};
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
            m_filePickerPath = (fs::path{m_filePickerPath} / name).string();
        }
    }
    for (const std::string &name : files)
    {
        if (ImGui::Selectable(name.c_str()))
        {
            std::snprintf(m_filePickerName.data(), m_filePickerName.size(), "%s", name.c_str());
        }
    }
    ImGui::EndChild();

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##filepickername", m_filePickerName.data(), m_filePickerName.size());

    if (ImGui::Button("Select", ImVec2(100.0f, 0.0f)) && ('\0' != m_filePickerName[0]))
    {
        if (nullptr != m_pFilePickerTarget)
        {
            *m_pFilePickerTarget = (fs::path{m_filePickerPath} / m_filePickerName.data()).string();
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void ControlDialogManager::Render()
{
    RenderConfigureProcess();
    RenderConfigureSchedulers();
    RenderConfigureBarriers();
    RenderHotkeys();
    RenderAbout();
    RenderFilePicker();
}

void ControlDialogManager::RenderConfigureProcess()
{
    if (m_processOpenRequested)
    {
        ImGui::OpenPopup("Configure process###ConfigureProcess");
        m_processOpenRequested = false;
    }
    if (!m_processOpen)
    {
        return;
    }

    const ImGuiViewport *viewport{ImGui::GetMainViewport()};
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(760.0f, 640.0f), ImGuiCond_Appearing);

    bool open{true};
    if (ImGui::BeginPopupModal("Configure process###ConfigureProcess", &open,
                               ImGuiWindowFlags_NoSavedSettings))
    {
        // Left: process list + add/delete.
        ImGui::BeginChild("##proclist", ImVec2(220.0f, -ImGui::GetFrameHeightWithSpacing() - 8.0f), true);
        for (int i{0}; i < static_cast<int>(m_processes.size()); ++i)
        {
            const ProcessConfigEntry &e{m_processes[static_cast<std::size_t>(i)]};
            std::string label{e.name};
            if (!e.running)
            {
                label += "  (not running)";
            }
            if (ImGui::Selectable(label.c_str(), m_processSelected == i))
            {
                m_processSelected = i;
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginGroup();

        if (ImGui::Checkbox("Show config of not running processes", &m_processShowNonRunning))
        {
            SnapshotProcesses();
        }

        if ((0 <= m_processSelected) && (m_processSelected < static_cast<int>(m_processes.size())))
        {
            ProcessConfigEntry &e{m_processes[static_cast<std::size_t>(m_processSelected)]};
            bool dirty{false};

            if (ImGui::BeginTabBar("##proctabs"))
            {
                if (ImGui::BeginTabItem("General"))
                {
                    ImGui::Text("Process id: %s", e.running ? std::to_string(e.pid).c_str() : "unknown");
                    ImGui::Text("State: %s", ProcessStateLabel(e.state));
                    ImGui::Text("Type: %s", ProcessTypeLabel(e.type));
                    ImGui::Text("Long name: %s", e.longName.c_str());
                    ImGui::Separator();
                    dirty |= ImGui::InputInt("Order (priority)", &e.priority);
                    dirty |= ImGui::InputInt("Repetition cycle", &e.timeSteps);
                    dirty |= ImGui::InputInt("Delay", &e.delay);

                    int schedIdx{0};
                    for (int s{0}; s < static_cast<int>(m_schedulerNames.size()); ++s)
                    {
                        if (m_schedulerNames[static_cast<std::size_t>(s)] == e.schedulerName)
                        {
                            schedIdx = s;
                        }
                    }
                    if (ImGui::BeginCombo("Scheduler",
                                          m_schedulerNames.empty()
                                              ? "Scheduler"
                                              : m_schedulerNames[static_cast<std::size_t>(schedIdx)].c_str()))
                    {
                        for (int s{0}; s < static_cast<int>(m_schedulerNames.size()); ++s)
                        {
                            const std::string &name{m_schedulerNames[static_cast<std::size_t>(s)]};
                            if (ImGui::Selectable(name.c_str(), s == schedIdx))
                            {
                                e.schedulerName = name;
                                dirty = true;
                            }
                        }
                        ImGui::EndCombo();
                    }

                    bool hasPrefix{!e.bbPrefix.empty()};
                    if (ImGui::Checkbox("Blackboard prefix", &hasPrefix))
                    {
                        if (!hasPrefix)
                        {
                            e.bbPrefix.clear();
                        }
                        dirty = true;
                    }
                    if (hasPrefix)
                    {
                        char prefix[512];
                        std::snprintf(prefix, sizeof(prefix), "%s", e.bbPrefix.c_str());
                        if (ImGui::InputText("Prefix", prefix, sizeof(prefix)))
                        {
                            e.bbPrefix = prefix;
                            dirty = true;
                        }
                    }
                    ImGui::Separator();
                    ImGui::Text("Cycle counter: %llu", static_cast<unsigned long long>(e.cycleCount));
                    ImGui::Text("Cycle time (cur/max/min): %llu / %llu / %llu",
                                static_cast<unsigned long long>(e.cycleTime),
                                static_cast<unsigned long long>(e.cycleTimeMax),
                                static_cast<unsigned long long>(e.cycleTimeMin));
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Range control"))
                {
                    dirty |= ImGui::Checkbox("Active before process", &e.rcBeforeActive);
                    if (e.rcBeforeActive)
                    {
                        ImGui::SameLine();
                        dirty |= ImGui::Checkbox("deactivate at startup##before", &e.rcBeforeDeactive);
                    }
                    dirty |= ImGui::Checkbox("Active behind process", &e.rcBehindActive);
                    if (e.rcBehindActive)
                    {
                        ImGui::SameLine();
                        dirty |= ImGui::Checkbox("deactivate at startup##behind", &e.rcBehindDeactive);
                    }
                    dirty |= ImGui::Checkbox("Physical range check", &e.rcPhysFlag);
                    dirty |= ImGui::Checkbox("Limit values", &e.rcLimitValuesFlag);
                    dirty |= ImGui::Checkbox("Stop scheduler on error", &e.rcStopSchedFlag);

                    dirty |= ImGui::Checkbox("Use error-counter variable", &e.rcErrorCounterFlag);
                    if (e.rcErrorCounterFlag)
                    {
                        char counter[512];
                        std::snprintf(counter, sizeof(counter), "%s", e.rcErrorCounter.c_str());
                        if (ImGui::InputText("Error counter", counter, sizeof(counter)))
                        {
                            e.rcErrorCounter = counter;
                            dirty = true;
                        }
                    }
                    dirty |= ImGui::Checkbox("Use control variable", &e.rcVarFlag);
                    if (e.rcVarFlag)
                    {
                        char control[512];
                        std::snprintf(control, sizeof(control), "%s", e.rcControl.c_str());
                        if (ImGui::InputText("Control variable", control, sizeof(control)))
                        {
                            e.rcControl = control;
                            dirty = true;
                        }
                    }
                    ImGui::Separator();
                    ImGui::TextUnformatted("Output:");
                    dirty |= ImGui::RadioButton("none", &e.rcOutput, 0);
                    ImGui::SameLine();
                    dirty |= ImGui::RadioButton("message box", &e.rcOutput, 1);
                    ImGui::SameLine();
                    dirty |= ImGui::RadioButton("write to file", &e.rcOutput, 2);
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Equations / files"))
                {
                    const auto fileRow{[this, &dirty](const char *par_Label, std::string &par_Value,
                                                      const char *par_Ext) {
                        char buffer[512];
                        std::snprintf(buffer, sizeof(buffer), "%s", par_Value.c_str());
                        ImGui::PushID(par_Label);
                        ImGui::SetNextItemWidth(-90.0f);
                        if (ImGui::InputText(par_Label, buffer, sizeof(buffer)))
                        {
                            par_Value = buffer;
                            dirty = true;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("..."))
                        {
                            OpenFilePicker(&par_Value, par_Ext);
                            dirty = true;
                        }
                        ImGui::PopID();
                    }};
                    fileRow("Equation before", e.equationBefore, ".tri");
                    fileRow("Equation behind", e.equationBehind, ".tri");
                    fileRow("SVL file", e.svlFile, ".svl");
                    fileRow("A2L file", e.a2lFile, ".a2l");

                    ImGui::Separator();
                    ImGui::TextUnformatted("A2L update flags:");
                    const auto flagBox{[&e, &dirty](const char *par_Label, int par_Bit) {
                        bool on{0 != (e.a2lFlags & par_Bit)};
                        if (ImGui::Checkbox(par_Label, &on))
                        {
                            if (on)
                            {
                                e.a2lFlags |= par_Bit;
                            }
                            else
                            {
                                e.a2lFlags &= ~par_Bit;
                            }
                            dirty = true;
                        }
                    }};
                    flagBox("Update addresses", A2L_LINK_UPDATE_FLAG);
                    flagBox("Address translation (DLL)", A2L_LINK_ADDRESS_TRANSLATION_DLL_FLAG);
                    flagBox("Address translation (multi DLL)", A2L_LINK_ADDRESS_TRANSLATION_MULTI_DLL_FLAG);
                    flagBox("Ignore MOD_COMMON alignments", A2L_LINK_IGNORE_MOD_COMMON_ALIGNMENTS_FLAG);
                    flagBox("Ignore record layout alignments", A2L_LINK_IGNORE_RECORD_LAYOUT_ALIGNMENTS_FLAG);
                    flagBox("Remember referenced labels", A2L_LINK_REMEMBER_REFERENCED_LABELS_FLAG);
                    flagBox("Ignore read only", A2L_LINK_IGNORE_READ_ONLY_FLAG);
                    flagBox("Default alignment", A2L_LINK_DEFAUT_ALIGNMENT_FLAG);
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Barriers"))
                {
                    ImGui::TextUnformatted("Before process (signal only):");
                    RenderBarrierListEditor("beforeOnly", e.barBeforeOnly);
                    ImGui::TextUnformatted("Before process (signal and wait):");
                    RenderBarrierListEditor("beforeWait", e.barBeforeWait);
                    ImGui::TextUnformatted("Behind process (signal only):");
                    RenderBarrierListEditor("behindOnly", e.barBehindOnly);
                    ImGui::TextUnformatted("Behind process (signal and wait):");
                    RenderBarrierListEditor("behindWait", e.barBehindWait);
                    ImGui::Separator();
                    ImGui::TextUnformatted("Loop-out before (signal only):");
                    RenderBarrierListEditor("loopBeforeOnly", e.barLoopBeforeOnly);
                    ImGui::TextUnformatted("Loop-out before (signal and wait):");
                    RenderBarrierListEditor("loopBeforeWait", e.barLoopBeforeWait);
                    ImGui::TextUnformatted("Loop-out behind (signal only):");
                    RenderBarrierListEditor("loopBehindOnly", e.barLoopBehindOnly);
                    ImGui::TextUnformatted("Loop-out behind (signal and wait):");
                    RenderBarrierListEditor("loopBehindWait", e.barLoopBehindWait);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }

            if (dirty)
            {
                e.changed = true;
            }
        }

        ImGui::EndGroup();

        ImGui::Separator();
        if (ImGui::Button("Add process...", ImVec2(120.0f, 0.0f)))
        {
            ImGui::OpenPopup("Add process###AddProcess");
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete process", ImVec2(120.0f, 0.0f)) &&
            (0 <= m_processSelected) && (m_processSelected < static_cast<int>(m_processes.size())))
        {
            m_processes.erase(m_processes.begin() + m_processSelected);
            m_processSelected = m_processes.empty() ? -1 : 0;
        }
        ImGui::SameLine();
        if (ImGui::Button("OK", ImVec2(100.0f, 0.0f)))
        {
            SaveAllProcesses();
            m_processOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
        {
            m_processOpen = false;
            ImGui::CloseCurrentPopup();
        }

        // Add-process sub modal.
        ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Add process###AddProcess", nullptr,
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
        {
            static std::array<char, 256> s_newName{};
            ImGui::InputText("Name", s_newName.data(), s_newName.size());
            if (ImGui::Button("Add", ImVec2(100.0f, 0.0f)) && ('\0' != s_newName[0]))
            {
                const std::string name{s_newName.data()};
                const bool exists{std::any_of(m_processes.begin(), m_processes.end(),
                                              [&name](const ProcessConfigEntry &e) { return e.name == name; })};
                if (!exists)
                {
                    ProcessConfigEntry created;
                    created.name = name;
                    created.changed = true;
                    m_processes.push_back(created);
                    m_processSelected = static_cast<int>(m_processes.size()) - 1;
                }
                s_newName[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
            {
                s_newName[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::EndPopup();
    }

    if (!open)
    {
        m_processOpen = false;
    }
}

void ControlDialogManager::RenderConfigureSchedulers()
{
    if (m_schedulerOpenRequested)
    {
        ImGui::OpenPopup("Configure schedulers###ConfigureSchedulers");
        m_schedulerOpenRequested = false;
    }
    if (!m_schedulerOpen)
    {
        return;
    }

    const ImGuiViewport *viewport{ImGui::GetMainViewport()};
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(640.0f, 520.0f), ImGuiCond_Appearing);

    bool open{true};
    if (ImGui::BeginPopupModal("Configure schedulers###ConfigureSchedulers", &open,
                               ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::BeginChild("##schedlist", ImVec2(200.0f, -ImGui::GetFrameHeightWithSpacing() - 8.0f), true);
        for (int i{0}; i < static_cast<int>(m_schedulers.size()); ++i)
        {
            if (ImGui::Selectable(m_schedulers[static_cast<std::size_t>(i)].name.c_str(),
                                  m_schedulerSelected == i))
            {
                m_schedulerSelected = i;
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginGroup();
        if ((0 <= m_schedulerSelected) && (m_schedulerSelected < static_cast<int>(m_schedulers.size())))
        {
            SchedulerConfigEntry &e{m_schedulers[static_cast<std::size_t>(m_schedulerSelected)]};
            bool dirty{false};

            int prioIdx{0};
            for (int p{0}; p < static_cast<int>(THREAD_PRIORITY_OPTIONS.size()); ++p)
            {
                if (e.priority >= THREAD_PRIORITY_OPTIONS[static_cast<std::size_t>(p)].value)
                {
                    prioIdx = p;
                }
            }
            if (ImGui::BeginCombo("Thread priority",
                                  THREAD_PRIORITY_OPTIONS[static_cast<std::size_t>(prioIdx)].label))
            {
                for (int p{0}; p < static_cast<int>(THREAD_PRIORITY_OPTIONS.size()); ++p)
                {
                    if (ImGui::Selectable(THREAD_PRIORITY_OPTIONS[static_cast<std::size_t>(p)].label,
                                          p == prioIdx))
                    {
                        e.priority = THREAD_PRIORITY_OPTIONS[static_cast<std::size_t>(p)].value;
                        dirty = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Before cycle (signal only):");
            RenderBarrierListEditor("sbeforeOnly", e.barBeforeOnly);
            ImGui::TextUnformatted("Before cycle (signal and wait):");
            RenderBarrierListEditor("sbeforeWait", e.barBeforeWait);
            ImGui::TextUnformatted("Behind cycle (signal only):");
            RenderBarrierListEditor("sbehindOnly", e.barBehindOnly);
            ImGui::TextUnformatted("Behind cycle (signal and wait):");
            RenderBarrierListEditor("sbehindWait", e.barBehindWait);

            if (dirty)
            {
                e.changed = true;
            }
        }
        ImGui::EndGroup();

        ImGui::Separator();
        if (ImGui::Button("Add scheduler...", ImVec2(130.0f, 0.0f)))
        {
            ImGui::OpenPopup("Add scheduler###AddScheduler");
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete scheduler", ImVec2(130.0f, 0.0f)) &&
            (0 <= m_schedulerSelected) && (m_schedulerSelected < static_cast<int>(m_schedulers.size())))
        {
            if ("Scheduler" != m_schedulers[static_cast<std::size_t>(m_schedulerSelected)].name)
            {
                m_schedulers.erase(m_schedulers.begin() + m_schedulerSelected);
                m_schedulerSelected = m_schedulers.empty() ? -1 : 0;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("OK", ImVec2(100.0f, 0.0f)))
        {
            SaveAllSchedulers();
            m_schedulerOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
        {
            m_schedulerOpen = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Add scheduler###AddScheduler", nullptr,
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
        {
            static std::array<char, 128> s_newSched{};
            ImGui::InputText("Name", s_newSched.data(), s_newSched.size());
            if (ImGui::Button("Add", ImVec2(100.0f, 0.0f)) && ('\0' != s_newSched[0]))
            {
                const std::string name{s_newSched.data()};
                const bool exists{std::any_of(m_schedulers.begin(), m_schedulers.end(),
                                              [&name](const SchedulerConfigEntry &e) { return e.name == name; })};
                if (!exists)
                {
                    SchedulerConfigEntry created;
                    created.name = name;
                    created.priority = THREAD_PRIORITY_IDLE;
                    created.changed = true;
                    m_schedulers.push_back(created);
                    m_schedulerSelected = static_cast<int>(m_schedulers.size()) - 1;
                }
                s_newSched[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
            {
                s_newSched[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::EndPopup();
    }

    if (!open)
    {
        m_schedulerOpen = false;
    }
}

void ControlDialogManager::RenderConfigureBarriers()
{
    if (m_barrierOpenRequested)
    {
        ImGui::OpenPopup("Configure barriers###ConfigureBarriers");
        m_barrierOpenRequested = false;
    }
    if (!m_barrierOpen)
    {
        return;
    }

    const ImGuiViewport *viewport{ImGui::GetMainViewport()};
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480.0f, 420.0f), ImGuiCond_Appearing);

    bool open{true};
    if (ImGui::BeginPopupModal("Configure barriers###ConfigureBarriers", &open,
                               ImGuiWindowFlags_NoSavedSettings))
    {
        if (ImGui::BeginTable("##barriertable", 3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg,
                              ImVec2(0.0f, 260.0f)))
        {
            ImGui::TableSetupColumn("Barrier");
            ImGui::TableSetupColumn("WaitIfAlone");
            ImGui::TableSetupColumn("");
            ImGui::TableHeadersRow();
            for (int i{0}; i < static_cast<int>(m_barriers.size()); ++i)
            {
                BarrierConfigEntry &b{m_barriers[static_cast<std::size_t>(i)]};
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(b.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(b.waitIfAlone ? "yes" : "no");
                ImGui::TableSetColumnIndex(2);
                ImGui::PushID(i);
                if (ImGui::SmallButton("delete"))
                {
                    m_barriers.erase(m_barriers.begin() + i);
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText("New barrier", m_barrierNewName.data(), m_barrierNewName.size());
        ImGui::SameLine();
        ImGui::Checkbox("WaitIfAlone", &m_barrierNewWaitIfAlone);
        ImGui::SameLine();
        if (ImGui::Button("Add") && ('\0' != m_barrierNewName[0]))
        {
            const std::string name{m_barrierNewName.data()};
            const bool exists{std::any_of(m_barriers.begin(), m_barriers.end(),
                                          [&name](const BarrierConfigEntry &e) { return e.name == name; })};
            if (!exists)
            {
                BarrierConfigEntry created;
                created.name = name;
                created.waitIfAlone = m_barrierNewWaitIfAlone;
                m_barriers.push_back(created);
            }
            m_barrierNewName[0] = '\0';
            m_barrierNewWaitIfAlone = false;
        }

        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(100.0f, 0.0f)))
        {
            SaveAllBarriers();
            m_barrierOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
        {
            m_barrierOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (!open)
    {
        m_barrierOpen = false;
    }
}

void ControlDialogManager::RenderHotkeys()
{
    if (m_hotkeyOpenRequested)
    {
        ImGui::OpenPopup("Function hotkeys###FunctionHotkeys");
        m_hotkeyOpenRequested = false;
    }
    if (!m_hotkeyOpen)
    {
        return;
    }

    const ImGuiViewport *viewport{ImGui::GetMainViewport()};
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560.0f, 440.0f), ImGuiCond_Appearing);

    bool open{true};
    if (ImGui::BeginPopupModal("Function hotkeys###FunctionHotkeys", &open,
                               ImGuiWindowFlags_NoSavedSettings))
    {
        if (ImGui::BeginTable("##hotkeytable", 3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg,
                              ImVec2(0.0f, 280.0f)))
        {
            ImGui::TableSetupColumn("Hotkey");
            ImGui::TableSetupColumn("Function");
            ImGui::TableSetupColumn("Formula");
            ImGui::TableHeadersRow();
            for (int i{0}; i < static_cast<int>(m_hotkeys.size()); ++i)
            {
                const HotkeyEntry &h{m_hotkeys[static_cast<std::size_t>(i)]};
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable(h.hotkey.c_str(), m_hotkeySelected == i,
                                      ImGuiSelectableFlags_SpanAllColumns))
                {
                    m_hotkeySelected = i;
                }
                ImGui::TableSetColumnIndex(1);
                const char *funcLabel{((0 <= h.function) &&
                                       (h.function < static_cast<int>(HOTKEY_FUNCTION_LABELS.size())))
                                          ? HOTKEY_FUNCTION_LABELS[static_cast<std::size_t>(h.function)]
                                          : "unknown"};
                ImGui::TextUnformatted(funcLabel);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(h.formula.c_str());
            }
            ImGui::EndTable();
        }

        ImGui::Separator();
        if (ImGui::Button("New...", ImVec2(80.0f, 0.0f)))
        {
            m_hotkeyEditIsNew = true;
            m_hotkeyEditKey[0] = '\0';
            m_hotkeyEditFunction = 0;
            m_hotkeyEditFormula[0] = '\0';
            m_hotkeyError.clear();
            m_hotkeyEditorOpen = true;
            m_hotkeyEditorOpenRequested = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Edit...", ImVec2(80.0f, 0.0f)) &&
            (0 <= m_hotkeySelected) && (m_hotkeySelected < static_cast<int>(m_hotkeys.size())))
        {
            const HotkeyEntry &h{m_hotkeys[static_cast<std::size_t>(m_hotkeySelected)]};
            m_hotkeyEditIsNew = false;
            std::snprintf(m_hotkeyEditKey.data(), m_hotkeyEditKey.size(), "%s", h.hotkey.c_str());
            m_hotkeyEditFunction = h.function;
            std::snprintf(m_hotkeyEditFormula.data(), m_hotkeyEditFormula.size(), "%s",
                          h.formula.c_str());
            m_hotkeyError.clear();
            m_hotkeyEditorOpen = true;
            m_hotkeyEditorOpenRequested = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete", ImVec2(80.0f, 0.0f)) &&
            (0 <= m_hotkeySelected) && (m_hotkeySelected < static_cast<int>(m_hotkeys.size())))
        {
            m_hotkeys.erase(m_hotkeys.begin() + m_hotkeySelected);
            m_hotkeySelected = m_hotkeys.empty() ? -1 : 0;
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(80.0f, 0.0f)))
        {
            SaveAllHotkeys();
            m_hotkeyOpen = false;
            ImGui::CloseCurrentPopup();
        }

        // New/Edit sub modal.
        if (m_hotkeyEditorOpenRequested)
        {
            ImGui::OpenPopup("Hotkey###HotkeyEditor");
            m_hotkeyEditorOpenRequested = false;
        }
        ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Hotkey###HotkeyEditor", nullptr,
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Shortcut (e.g. Ctrl+F6)", m_hotkeyEditKey.data(), m_hotkeyEditKey.size());
            if (ImGui::BeginCombo("Function",
                                  HOTKEY_FUNCTION_LABELS[static_cast<std::size_t>(m_hotkeyEditFunction)]))
            {
                for (int f{0}; f < static_cast<int>(HOTKEY_FUNCTION_LABELS.size()); ++f)
                {
                    if (ImGui::Selectable(HOTKEY_FUNCTION_LABELS[static_cast<std::size_t>(f)],
                                          f == m_hotkeyEditFunction))
                    {
                        m_hotkeyEditFunction = f;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::InputText("Formula / file", m_hotkeyEditFormula.data(), m_hotkeyEditFormula.size());

            if (!m_hotkeyError.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", m_hotkeyError.c_str());
            }

            if (ImGui::Button("OK", ImVec2(100.0f, 0.0f)))
            {
                const std::string key{m_hotkeyEditKey.data()};
                std::string keyLower{key};
                std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(), ::tolower);
                const bool reserved{std::any_of(RESERVED_HOTKEYS.begin(), RESERVED_HOTKEYS.end(),
                                                [&keyLower](const char *r) { return keyLower == r; })};
                if (key.empty())
                {
                    m_hotkeyError = "Shortcut must not be empty.";
                }
                else if (reserved)
                {
                    m_hotkeyError = "The expression is reserved by the system.";
                }
                else
                {
                    // Overwrite an existing entry with the same key.
                    for (std::size_t i{0}; i < m_hotkeys.size(); ++i)
                    {
                        std::string existing{m_hotkeys[i].hotkey};
                        std::transform(existing.begin(), existing.end(), existing.begin(), ::tolower);
                        if (existing == keyLower)
                        {
                            m_hotkeys.erase(m_hotkeys.begin() + static_cast<std::ptrdiff_t>(i));
                            break;
                        }
                    }
                    HotkeyEntry entry;
                    entry.hotkey = key;
                    entry.function = m_hotkeyEditFunction;
                    entry.formula = m_hotkeyEditFormula.data();
                    m_hotkeys.push_back(entry);
                    m_hotkeyEditorOpen = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
            {
                m_hotkeyEditorOpen = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::EndPopup();
    }

    if (!open)
    {
        SaveAllHotkeys();
        m_hotkeyOpen = false;
    }
}

void ControlDialogManager::RenderAbout()
{
    if (m_aboutOpenRequested)
    {
        ImGui::OpenPopup("About OpenXilEnv###AboutDialog");
        m_aboutOpenRequested = false;
    }

    const ImGuiViewport *viewport{ImGui::GetMainViewport()};
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560.0f, 420.0f), ImGuiCond_Appearing);

    bool open{true};
    if (ImGui::BeginPopupModal("About OpenXilEnv###AboutDialog", &open,
                               ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::BeginChild("##abouttext", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() - 8.0f));
        ImGui::TextUnformatted(m_aboutText.c_str());
        ImGui::EndChild();
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120.0f, 0.0f)))
        {
            m_aboutOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (!open)
    {
        m_aboutOpen = false;
    }
}

} // namespace XilEnvImGui
