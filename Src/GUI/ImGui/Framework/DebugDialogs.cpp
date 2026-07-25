#include "Framework/DebugDialogs.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"

extern "C" {
#include "Platform.h"
#include "Blackboard.h"
#include "EquationList.h"
#include "Files.h"
#include "IniDataBase.h"
#include "MyMemory.h"
#include "RemoteMasterNet.h"
#include "ScBbCopyLists.h"
#include "Scheduler.h"
#include "SchedBarrier.h"
#include "ThrowError.h"
#include "UniqueNumber.h"
#include "Wildcards.h"
#include "DebugInfos.h"
#include "DebugInfoDB.h"
#include "DebugInfoAccessExpression.h"
#include "ExtProcessReferences.h"
#include "BaseMessages.h"
}

namespace XilEnvImGui
{

namespace
{

// Linux performance-counter to microseconds conversion (mirrors the Qt dialog).
constexpr double RM_TIME_CONVERSION{1000.0};

/**
 * @brief Maps a barrier-logging action code to its human readable label.
 */
const char *BarrierActionName(BARRIER_LOGGING_ACTION par_Action)
{
    switch (par_Action)
    {
    case BARRIER_LOGGING_UNDEF_ACTION: return "undef";
    case BARRIER_LOGGING_EMERGENCY_CLEANUP_ACTION: return "emergency cleanup killed process";
    case BARRIER_LOGGING_ADD_BEFORE_PROCESS_ACTION: return "add before process";
    case BARRIER_LOGGING_REMOVE_BEFORE_PROCESS_ACTION: return "remove before process";
    case BARRIER_LOGGING_GOTO_SLEEP_BEFORE_PROCESS_ACTION: return "goto sleep before process";
    case BARRIER_LOGGING_WAKE_ALL_BEFORE_PROCESS_ACTION: return "wake all before process";
    case BARRIER_LOGGING_WALK_THROUGH_BEFORE_PROCESS_ACTION: return "walk through before process";
    case BARRIER_LOGGING_ADD_BEHIND_PROCESS_ACTION: return "add behind process";
    case BARRIER_LOGGING_REMOVE_BEHIND_PROCESS_ACTION: return "remove behind process";
    case BARRIER_LOGGING_GOTO_SLEEP_BEHIND_PROCESS_ACTION: return "goto sleep behind process";
    case BARRIER_LOGGING_WAKE_ALL_BEHIND_PROCESS_ACTION: return "wake all behind process";
    case BARRIER_LOGGING_WALK_THROUGH_BEHIND_PROCESS_ACTION: return "walk through behind process";
    case BARRIER_LOGGING_ADD_BEFORE_SCHEDULER_ACTION: return "add before scheduler";
    case BARRIER_LOGGING_REMOVE_BEFORE_SCHEDULER_ACTION: return "remove before scheduler";
    case BARRIER_LOGGING_GOTO_SLEEP_BEFORE_SCHEDULER_ACTION: return "goto sleep before scheduler";
    case BARRIER_LOGGING_WAKE_ALL_BEFORE_SCHEDULER_ACTION: return "wake all before scheduler";
    case BARRIER_LOGGING_WALK_THROUGH_BEFORE_SCHEDULER_ACTION: return "walk through before scheduler";
    case BARRIER_LOGGING_ADD_BEHIND_SCHEDULER_ACTION: return "add behind scheduler";
    case BARRIER_LOGGING_REMOVE_BEHIND_SCHEDULER_ACTION: return "remove behind scheduler";
    case BARRIER_LOGGING_GOTO_SLEEP_BEHIND_SCHEDULER_ACTION: return "goto sleep behind scheduler";
    case BARRIER_LOGGING_WAKE_ALL_BEHIND_SCHEDULER_ACTION: return "wake all behind scheduler";
    case BARRIER_LOGGING_WALK_THROUGH_BEHIND_SCHEDULER_ACTION: return "walk through behind scheduler";
    case BARRIER_LOGGING_ADD_BEFORE_LOOPOUT_ACTION: return "add before loopout";
    case BARRIER_LOGGING_REMOVE_BEFORE_LOOPOUT_ACTION: return "remove before loopout";
    case BARRIER_LOGGING_GOTO_SLEEP_BEFORE_LOOPOUT_ACTION: return "goto sleep before loopout";
    case BARRIER_LOGGING_WAKE_ALL_BEFORE_LOOPOUT_ACTION: return "wake all before loopout";
    case BARRIER_LOGGING_WALK_THROUGH_BEFORE_LOOPOUT_ACTION: return "walk through before loopout";
    case BARRIER_LOGGING_ADD_BEHIND_LOOPOUT_ACTION: return "add behind loopout";
    case BARRIER_LOGGING_REMOVE_BEHIND_LOOPOUT_ACTION: return "remove behind loopout";
    case BARRIER_LOGGING_GOTO_SLEEP_BEHIND_LOOPOUT_ACTION: return "goto sleep behind loopout";
    case BARRIER_LOGGING_WAKE_ALL_BEHIND_LOOPOUT_ACTION: return "wake all behind loopout";
    case BARRIER_LOGGING_WALK_THROUGH_BEHIND_LOOPOUT_ACTION: return "walk through behind loopout";
    default: return "undef";
    }
}

/**
 * @brief Maps a debug-info type code to its compiler label.
 */
const char *DebugInfoTypeName(int32_t par_Type)
{
    switch (par_Type)
    {
    case DEBUGINFOTYPE_VISUAL6: return "VisualC 6.0";
    case DEBUGINFOTYPE_VISUALNET_2003: return "VisualC 2003";
    case DEBUGINFOTYPE_VISUALNET_2008: return "VisualC 2008";
    case DEBUGINFOTYPE_VISUALNET_2010: return "VisualC 2010";
    case DEBUGINFOTYPE_VISUALNET_2012: return "VisualC 2012";
    case DEBUGINFOTYPE_VISUALNET_2015: return "VisualC 2015";
    case DEBUGINFOTYPE_VISUALNET_2017: return "VisualC 2017";
    case DEBUGINFOTYPE_VISUALNET_2019: return "VisualC 2019";
    case DEBUGINFOTYPE_DWARF: return "Dwarf (gcc)";
    default: return "unknown";
    }
}

} // namespace

DebugDialogManager &DebugDialogManager::GetInstance()
{
    static DebugDialogManager instance;
    return instance;
}

void DebugDialogManager::OpenRemoteMasterCallStatistics()
{
    m_rmStatsOpen = true;
}

void DebugDialogManager::OpenInternals()
{
    m_internalsOpen = true;
}

void DebugDialogManager::OpenLoadedDebugInfos()
{
    m_debugInfosOpen = true;
}

void DebugDialogManager::OpenActiveEquations()
{
    if (0 == m_activeEqFilter[0])
    {
        m_activeEqFilter[0] = '*';
    }
    m_activeEqTypeMask = static_cast<int>(
        IniFileDataBaseReadUInt("BasicSettings", "ActiveEquationsWindowTypeFilterMask", 0xFFFFFFFFu,
                                GetMainFileDescriptor()));
    m_activeEqOpen = true;
}

void DebugDialogManager::OpenExtError()
{
    m_extErrorOpen = true;
}

void DebugDialogManager::OpenBarrierHistory()
{
    m_barrierHistoryOpen = true;
}

void DebugDialogManager::OpenExternProcessCopyList()
{
    RefreshExternalProcessNames();
    if ((0 > m_copyListProcessSel) && (!m_externalProcesses.empty()))
    {
        m_copyListProcessSel = 0;
    }
    if (0 == m_copyListFilter[0])
    {
        m_copyListFilter[0] = '*';
    }
    if (0 == m_copyListAddrFrom[0])
    {
        std::snprintf(m_copyListAddrFrom.data(), m_copyListAddrFrom.size(), "%s", "0");
    }
    if (0 == m_copyListAddrTo[0])
    {
        std::snprintf(m_copyListAddrTo.data(), m_copyListAddrTo.size(), "%s", "ffffffffffffffff");
    }
    m_copyListOpen = true;
}

void DebugDialogManager::OpenReferencedLabels()
{
    RefreshExternalProcessNames();
    if ((0 > m_refLabelsProcessSel) && (!m_externalProcesses.empty()))
    {
        m_refLabelsProcessSel = 0;
    }
    if ((0 <= m_refLabelsProcessSel) &&
        (m_refLabelsProcessSel < static_cast<int>(m_externalProcesses.size())))
    {
        FillReferencedLabels(m_externalProcesses[static_cast<std::size_t>(m_refLabelsProcessSel)]);
    }
    m_refLabelsOpen = true;
}

void DebugDialogManager::AddExtErrorMessage(int par_Level, std::uint64_t par_Cycle, int par_Pid,
                                            const char *par_Message)
{
    ExtErrorEntry entry{};
    entry.level = par_Level;
    entry.cycle = par_Cycle;
    entry.pid = par_Pid;
    // Normalise all whitespace to plain spaces (mirrors the Qt model).
    if (nullptr != par_Message)
    {
        entry.message.reserve(std::strlen(par_Message));
        for (const char *s = par_Message; 0 != *s; ++s)
        {
            const unsigned char c{static_cast<unsigned char>(*s)};
            entry.message.push_back((0 != std::isspace(c)) ? ' ' : *s);
        }
    }
    std::lock_guard<std::mutex> lock(m_extErrorMutex);
    m_extErrorEntries.push_back(std::move(entry));
    m_extErrorOpen = true;
}

void DebugDialogManager::RefreshExternalProcessNames()
{
    m_externalProcesses.clear();
    READ_NEXT_PROCESS_NAME *buffer{init_read_next_process_name(2 | 0x100)};
    char *name{nullptr};
    while (nullptr != (name = read_next_process_name(buffer)))
    {
        if (0 == IsInternalProcess(name))
        {
            m_externalProcesses.emplace_back(name);
        }
    }
    close_read_next_process_name(buffer);
}

void DebugDialogManager::Render()
{
    RenderRemoteMasterCallStatistics();
    RenderInternals();
    RenderLoadedDebugInfos();
    RenderActiveEquations();
    RenderExtError();
    RenderBarrierHistory();
    RenderExternProcessCopyList();
    RenderReferencedLabels();
}

void DebugDialogManager::RenderRemoteMasterCallStatistics()
{
    if (!m_rmStatsOpen)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(760.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Remote master call statistics###RemoteMasterCallStatistics", &m_rmStatsOpen))
    {
        if (ImGui::Button("Reset"))
        {
            ResetRemoteMasterCallStatistics();
        }
        ImGui::Separator();

        const int rows{RemoteMasterGetRowCount()};
        const ImGuiTableFlags flags{ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                    ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX};
        if (ImGui::BeginTable("###RmStatsTable", 7, flags))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Index");
            ImGui::TableSetupColumn("CallCount");
            ImGui::TableSetupColumn("ActualTime (us)");
            ImGui::TableSetupColumn("AccumulatedTime (us)");
            ImGui::TableSetupColumn("AverageTime (us)");
            ImGui::TableSetupColumn("MinTime (us)");
            ImGui::TableSetupColumn("MaxTime (us)");
            ImGui::TableHeadersRow();

            for (int row = 0; row < rows; ++row)
            {
                std::uint64_t callCounter{0};
                std::uint64_t lastTime{0};
                std::uint64_t accumulatedTime{0};
                std::uint64_t maxTime{0};
                std::uint64_t minTime{0};
                if ((0 <= RemoteMasterGetNextCallToStatistic(row, &callCounter, &lastTime,
                                                             &accumulatedTime, &maxTime, &minTime)) &&
                    (0 < callCounter))
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%d", row);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%llu", static_cast<unsigned long long>(callCounter));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%.3f", static_cast<double>(lastTime) * RM_TIME_CONVERSION);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%.3f", static_cast<double>(accumulatedTime) * RM_TIME_CONVERSION);
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%.3f", static_cast<double>(accumulatedTime) * RM_TIME_CONVERSION /
                                            static_cast<double>(callCounter));
                    ImGui::TableSetColumnIndex(5);
                    ImGui::Text("%.3f", static_cast<double>(minTime) * RM_TIME_CONVERSION);
                    ImGui::TableSetColumnIndex(6);
                    ImGui::Text("%.3f", static_cast<double>(maxTime) * RM_TIME_CONVERSION);
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void DebugDialogManager::RenderInternals()
{
    if (!m_internalsOpen)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(760.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Internals###Internals", &m_internalsOpen))
    {
        const int fd{GetMainFileDescriptor()};
        char iniName[MAX_PATH]{};
        IniFileDataBaseGetFileNameByDescriptor(fd, iniName, sizeof(iniName));
        ImGui::Text("INI file: %s", iniName);
        ImGui::Separator();

        // Collect the process list once (index 0 == "all").
        std::vector<std::string> processes;
        processes.emplace_back("all");
        {
            READ_NEXT_PROCESS_NAME *buffer{init_read_next_process_name(2)};
            char *name{nullptr};
            while (nullptr != (name = read_next_process_name(buffer)))
            {
                processes.emplace_back(name);
            }
            close_read_next_process_name(buffer);
        }
        if (m_internalsProcessSel >= static_cast<int>(processes.size()))
        {
            m_internalsProcessSel = 0;
        }

        if (ImGui::BeginChild("###InternalsColumns", ImVec2(0.0f, 0.0f)))
        {
            if (ImGui::CollapsingHeader("Open files", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (ImGui::BeginChild("###OpenFiles", ImVec2(0.0f, 110.0f), ImGuiChildFlags_Borders))
                {
                    const int count{get_count_open_files_infos()};
                    for (int x = 0; x < count; ++x)
                    {
                        const char *info{get_open_files_infos(x)};
                        ImGui::TextUnformatted((nullptr != info) ? info : "");
                    }
                }
                ImGui::EndChild();
            }

            if (ImGui::CollapsingHeader("Open INI files", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (ImGui::BeginChild("###OpenIniFiles", ImVec2(0.0f, 110.0f), ImGuiChildFlags_Borders))
                {
                    // Descriptors live in the fixed range 256..271.
                    for (int x = 256; x < (256 + 16); ++x)
                    {
                        char fileName[MAX_PATH]{};
                        if (0 != IniFileDataBaseGetFileNameByDescriptor(x, fileName, sizeof(fileName)))
                        {
                            std::snprintf(fileName, sizeof(fileName), "%s", "not used");
                        }
                        ImGui::Text("%d: %s", x, fileName);
                    }
                }
                ImGui::EndChild();
            }

            if (ImGui::CollapsingHeader("Memory blocks", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::SetNextItemWidth(240.0f);
                if (ImGui::BeginCombo("Process###InternalsProcess",
                                      processes[static_cast<std::size_t>(m_internalsProcessSel)].c_str()))
                {
                    for (int i = 0; i < static_cast<int>(processes.size()); ++i)
                    {
                        const bool selected{i == m_internalsProcessSel};
                        if (ImGui::Selectable(processes[static_cast<std::size_t>(i)].c_str(), selected))
                        {
                            m_internalsProcessSel = i;
                        }
                        if (selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                int filterPid{0};
                if (0 != m_internalsProcessSel)
                {
                    filterPid = get_pid_by_name(
                        processes[static_cast<std::size_t>(m_internalsProcessSel)].c_str());
                }

                long totalMem{0};
                if (ImGui::BeginChild("###MemoryBlocks", ImVec2(0.0f, 240.0f), ImGuiChildFlags_Borders,
                                      ImGuiWindowFlags_HorizontalScrollbar))
                {
                    for (struct memory_cblock *block = memory_block_list; nullptr != block;
                         block = block->next)
                    {
                        if ((0 == filterPid) || (block->pid == filterPid))
                        {
                            ImGui::Text("(%d) %p[%u] %s[%d]", block->pid, block->block, block->size,
                                        (nullptr != block->file) ? block->file : "", block->line);
                            totalMem += static_cast<long>(block->size);
                        }
                    }
                }
                ImGui::EndChild();
                ImGui::Text("Total bytes: %ld", totalMem);
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void DebugDialogManager::RenderLoadedDebugInfos()
{
    if (!m_debugInfosOpen)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(900.0f, 620.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Loaded debug infos###LoadedDebugInfos", &m_debugInfosOpen))
    {
        DEBUG_INFOS_ASSOCIATED_CONNECTION *connections{nullptr};
        DEBUG_INFOS_ASSOCIATED_PROCESSE *procs{nullptr};
        DEBUG_INFOS_DATA *datas{nullptr};
        GetInternalDebugInfoPointers(&connections, &procs, &datas);

        const ImGuiTableFlags flags{ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                    ImGuiTableFlags_ScrollX};

        if (ImGui::CollapsingHeader("Connections", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::BeginTable("###DbgConnections", 5, flags))
            {
                ImGui::TableSetupColumn("Index");
                ImGui::TableSetupColumn("Used by unique id");
                ImGui::TableSetupColumn("CallBack sched thread");
                ImGui::TableSetupColumn("CallBack par users");
                ImGui::TableSetupColumn("Associated process index");
                ImGui::TableHeadersRow();
                for (int x = 0; x < DEBUG_INFOS_MAX_CONNECTIONS; ++x)
                {
                    if (0 == connections[x].UsedFlag)
                    {
                        continue;
                    }
                    std::int64_t procIndex{connections[x].AssociatedProcess - procs};
                    if ((0 > procIndex) || (procIndex >= DEBUG_INFOS_MAX_PROCESSES))
                    {
                        procIndex = -1;
                    }
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%d", x);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%d", connections[x].UsedByUniqueId);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("0x%p",
                                reinterpret_cast<void *>(connections[x].LoadUnloadCallBackFuncsSchedThread));
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("0x%p", connections[x].LoadUnloadCallBackFuncParUsers);
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%lld", static_cast<long long>(procIndex));
                }
                ImGui::EndTable();
            }
        }

        if (ImGui::CollapsingHeader("Processes", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::BeginTable("###DbgProcesses", 9, flags))
            {
                ImGui::TableSetupColumn("Index");
                ImGui::TableSetupColumn("Attach counter");
                ImGui::TableSetupColumn("Process name");
                ImGui::TableSetupColumn("Pid");
                ImGui::TableSetupColumn("Base address");
                ImGui::TableSetupColumn("Executable name");
                ImGui::TableSetupColumn("is realtime");
                ImGui::TableSetupColumn("machine type");
                ImGui::TableSetupColumn("Associated debug infos index");
                ImGui::TableHeadersRow();
                for (int x = 0; x < DEBUG_INFOS_MAX_PROCESSES; ++x)
                {
                    if (0 == procs[x].UsedFlag)
                    {
                        continue;
                    }
                    std::int64_t dbgIndex{procs[x].AssociatedDebugInfos - datas};
                    if ((0 > dbgIndex) || (dbgIndex >= DEBUG_INFOS_MAX_DEBUG_INFOS))
                    {
                        dbgIndex = -1;
                    }
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%d", x);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%d", procs[x].AttachCounter);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted((nullptr != procs[x].ProcessName) ? procs[x].ProcessName : "");
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%d", procs[x].Pid);
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%llx", static_cast<unsigned long long>(procs[x].BaseAddress));
                    ImGui::TableSetColumnIndex(5);
                    ImGui::TextUnformatted((nullptr != procs[x].ExeOrDllName) ? procs[x].ExeOrDllName : "");
                    ImGui::TableSetColumnIndex(6);
                    ImGui::Text("%d", procs[x].IsRealtimeProcess);
                    ImGui::TableSetColumnIndex(7);
                    ImGui::Text("%d", procs[x].MachineType);
                    ImGui::TableSetColumnIndex(8);
                    ImGui::Text("%lld", static_cast<long long>(dbgIndex));
                }
                ImGui::EndTable();
            }
        }

        if (ImGui::CollapsingHeader("Debug infos", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::BeginTable("###DbgInfos", 9, flags))
            {
                ImGui::TableSetupColumn("Index");
                ImGui::TableSetupColumn("Attach counter");
                ImGui::TableSetupColumn("Try to load");
                ImGui::TableSetupColumn("Loaded");
                ImGui::TableSetupColumn("Executable file name");
                ImGui::TableSetupColumn("Debug info file name");
                ImGui::TableSetupColumn("label list entry count");
                ImGui::TableSetupColumn("Address");
                ImGui::TableSetupColumn("Type");
                ImGui::TableHeadersRow();
                for (int x = 0; x < DEBUG_INFOS_MAX_DEBUG_INFOS; ++x)
                {
                    if (0 == datas[x].UsedFlag)
                    {
                        continue;
                    }
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%d", x);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%d", datas[x].AttachCounter);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d", datas[x].TryToLoadFlag);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%d", datas[x].LoadedFlag);
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(datas[x].ExecutableFileName);
                    ImGui::TableSetColumnIndex(5);
                    ImGui::TextUnformatted(datas[x].DebugInfoFileName);
                    ImGui::TableSetColumnIndex(6);
                    ImGui::Text("%u", datas[x].label_list_entrys);
                    ImGui::TableSetColumnIndex(7);
                    ImGui::TextUnformatted((DEBUGINFO_ADDRESS_RELATIVE == datas[x].AbsoluteOrRelativeAddress)
                                               ? "relative"
                                               : "absolute");
                    ImGui::TableSetColumnIndex(8);
                    ImGui::TextUnformatted(DebugInfoTypeName(datas[x].DebugInfoType));
                }
                ImGui::EndTable();
            }
        }
    }
    ImGui::End();
}

void DebugDialogManager::RenderActiveEquations()
{
    if (!m_activeEqOpen)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(820.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Active equations###ActiveEquations", &m_activeEqOpen))
    {
        // Type-mask checkboxes (mirror the Qt filter row).
        auto maskCheckbox = [this](const char *par_Label, int par_Bit) {
            bool checked{0 != (m_activeEqTypeMask & par_Bit)};
            if (ImGui::Checkbox(par_Label, &checked))
            {
                if (checked)
                {
                    m_activeEqTypeMask |= par_Bit;
                }
                else
                {
                    m_activeEqTypeMask &= ~par_Bit;
                }
                IniFileDataBaseWriteUIntHex("BasicSettings", "ActiveEquationsWindowTypeFilterMask",
                                            static_cast<unsigned int>(m_activeEqTypeMask),
                                            GetMainFileDescriptor());
            }
        };
        maskCheckbox("Blackboard", EQU_TYPE_BLACKBOARD);
        ImGui::SameLine();
        maskCheckbox("Global", EQU_TYPE_GLOBAL_EQU_CALCULATOR);
        ImGui::SameLine();
        maskCheckbox("Before", EQU_TYPE_BEFORE_PROCESS);
        ImGui::SameLine();
        maskCheckbox("Behind", EQU_TYPE_BEHIND_PROCESS);
        maskCheckbox("CAN", EQU_TYPE_CAN);
        ImGui::SameLine();
        maskCheckbox("Analog", EQU_TYPE_ANALOG);
        ImGui::SameLine();
        maskCheckbox("Rampe", EQU_TYPE_RAMPE);
        ImGui::SameLine();
        maskCheckbox("WaitUntil", EQU_TYPE_WAIT_UNTIL);

        ImGui::SetNextItemWidth(240.0f);
        ImGui::InputText("Filter###ActiveEqFilter", m_activeEqFilter.data(), m_activeEqFilter.size());
        ImGui::Separator();

        // Build a wildcard-only include/exclude filter (zero-initialised = include all).
        INCLUDE_EXCLUDE_FILTER filter{};
        if (0 != m_activeEqFilter[0])
        {
            filter.MainFilter = m_activeEqFilter.data();
        }

        ACTIVE_EQUATION_ELEM *snap{static_cast<ACTIVE_EQUATION_ELEM *>(m_pEqSnapshot)};
        const int rows{GetActiveEquationSnapShot(&filter, m_activeEqTypeMask, &m_eqSnapshotSize, &snap,
                                                 &m_eqStringBufferSize, &m_pEqStringBuffer)};
        m_pEqSnapshot = snap;

        const ImGuiTableFlags flags{ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                    ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX};
        if (ImGui::BeginTable("###ActiveEqTable", 5, flags))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Equation", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Process");
            ImGui::TableSetupColumn("Cycle");
            ImGui::TableSetupColumn("Attach counter");
            ImGui::TableSetupColumn("Additional infos");
            ImGui::TableHeadersRow();

            for (int row = 0; (row < rows) && (nullptr != snap); ++row)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted((nullptr != snap[row].Equation) ? snap[row].Equation : "");
                ImGui::TableSetColumnIndex(1);
                std::string pids;
                for (int p = 0; p < 32; ++p)
                {
                    if (0 != snap[row].Pids[p])
                    {
                        char tmp[16];
                        std::snprintf(tmp, sizeof(tmp), "%d ", snap[row].Pids[p]);
                        pids += tmp;
                    }
                }
                ImGui::TextUnformatted(pids.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%u", snap[row].CycleLoaded);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%u", snap[row].AttachCounter);
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted((nullptr != snap[row].AdditionalInfos) ? snap[row].AdditionalInfos
                                                                              : "");
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void DebugDialogManager::RenderExtError()
{
    if (!m_extErrorOpen)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(m_extErrorMutex);
    ImGui::SetNextWindowSize(ImVec2(760.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Extended errors###ExtError", &m_extErrorOpen))
    {
        if (ImGui::Button("Delete selected"))
        {
            m_extErrorEntries.erase(
                std::remove_if(m_extErrorEntries.begin(), m_extErrorEntries.end(),
                               [](const ExtErrorEntry &e) { return e.selected; }),
                m_extErrorEntries.end());
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete all"))
        {
            m_extErrorEntries.clear();
        }
        ImGui::SameLine();
        ImGui::Text("%d message(s)", static_cast<int>(m_extErrorEntries.size()));
        ImGui::Separator();

        const ImGuiTableFlags flags{ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                    ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX};
        if (ImGui::BeginTable("###ExtErrorTable", 4, flags))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Level");
            ImGui::TableSetupColumn("Cycle");
            ImGui::TableSetupColumn("Process");
            ImGui::TableSetupColumn("Error message", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (std::size_t i = 0; i < m_extErrorEntries.size(); ++i)
            {
                ExtErrorEntry &entry{m_extErrorEntries[i]};
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                char selId[48];
                std::snprintf(selId, sizeof(selId), "%d##sel%zu", entry.level, i);
                ImGui::Selectable(selId, &entry.selected, ImGuiSelectableFlags_SpanAllColumns);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%llu", static_cast<unsigned long long>(entry.cycle));
                ImGui::TableSetColumnIndex(2);
                char procName[MAX_PATH]{};
                if (0 == GetProcessShortName(entry.pid, procName, sizeof(procName)))
                {
                    ImGui::TextUnformatted(procName);
                }
                else
                {
                    ImGui::Text("%d", entry.pid);
                }
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(entry.message.c_str());
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void DebugDialogManager::RenderBarrierHistory()
{
    if (!m_barrierHistoryOpen)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(1000.0f, 640.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Barrier history logging###BarrierHistory", &m_barrierHistoryOpen))
    {
        if (ImGui::BeginTabBar("###BarrierTabs"))
        {
            if (ImGui::BeginTabItem("Current state"))
            {
                // Column headers = barriers; rows = fixed fields then schedulers + processes.
                char *barrierString{GetAllBarrierString()};
                std::vector<std::string> barriers;
                if (nullptr != barrierString)
                {
                    const char *p{barrierString};
                    std::string cur;
                    for (;; ++p)
                    {
                        if ((0 == *p) || (';' == *p))
                        {
                            if (!cur.empty())
                            {
                                barriers.push_back(cur);
                            }
                            cur.clear();
                            if (0 == *p)
                            {
                                break;
                            }
                        }
                        else
                        {
                            cur.push_back(*p);
                        }
                    }
                    FreeAllBarrierString(barrierString);
                }

                const ImGuiTableFlags flags{ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                            ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY};
                if (ImGui::BeginTable("###BarrierState",
                                      1 + static_cast<int>(barriers.size()), flags))
                {
                    ImGui::TableSetupColumn("Field");
                    for (const std::string &b : barriers)
                    {
                        ImGui::TableSetupColumn(b.c_str());
                    }
                    ImGui::TableHeadersRow();

                    static const char *const fieldNames[]{
                        "State",       "SchedulerMask",  "ProcessMask",
                        "Switch",      "SchedulerFlags_0", "ProcessFlags_0",
                        "SchedulerFlags_1", "ProcessFlags_1", "ProcesseOrSchedulerCount",
                        "WaitOnCounter", "AddNewProcessMasksBefore", "AddNewProcessMasksBehind",
                        "AddNewSchedulerMasks", "RemoveProcessMasks", "RemoveSchedulerMasks",
                        "BarriersLoggingCounter"};

                    for (int fieldRow = 0; fieldRow < 16; ++fieldRow)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(fieldNames[fieldRow]);
                        for (int col = 0; col < static_cast<int>(barriers.size()); ++col)
                        {
                            int state{0};
                            unsigned int schedulerMask{0};
                            unsigned int processMask{0};
                            int barrierSwitch{0};
                            unsigned int schedulerFlags0{0};
                            unsigned int processFlags0{0};
                            unsigned int schedulerFlags1{0};
                            unsigned int processFlags1{0};
                            int poscCount{0};
                            int waitOnCounter{0};
                            unsigned int addProcBefore{0};
                            unsigned int addProcBehind{0};
                            unsigned int addSched{0};
                            unsigned int removeProc{0};
                            unsigned int removeSched{0};
                            unsigned int loggingCounter{0};
                            if (0 != GetBarierInfos(col, &state, &schedulerMask, &processMask,
                                                    &barrierSwitch, &schedulerFlags0, &processFlags0,
                                                    &schedulerFlags1, &processFlags1, &poscCount,
                                                    &waitOnCounter, &addProcBefore, &addProcBehind,
                                                    &addSched, &removeProc, &removeSched,
                                                    &loggingCounter))
                            {
                                continue;
                            }
                            ImGui::TableSetColumnIndex(1 + col);
                            switch (fieldRow)
                            {
                            case 0: ImGui::Text("%d", state); break;
                            case 1: ImGui::Text("%x", schedulerMask); break;
                            case 2: ImGui::Text("%x", processMask); break;
                            case 3: ImGui::Text("%d", barrierSwitch); break;
                            case 4: ImGui::Text("%x", schedulerFlags0); break;
                            case 5: ImGui::Text("%x", processFlags0); break;
                            case 6: ImGui::Text("%x", schedulerFlags1); break;
                            case 7: ImGui::Text("%x", processFlags1); break;
                            case 8: ImGui::Text("%d", poscCount); break;
                            case 9: ImGui::Text("%d", waitOnCounter); break;
                            case 10: ImGui::Text("%x", addProcBefore); break;
                            case 11: ImGui::Text("%x", addProcBehind); break;
                            case 12: ImGui::Text("%x", addSched); break;
                            case 13: ImGui::Text("%x", removeProc); break;
                            case 14: ImGui::Text("%x", removeSched); break;
                            case 15: ImGui::Text("%u", loggingCounter); break;
                            default: break;
                            }
                        }
                    }

                    // Per-scheduler and per-process waiting state rows.
                    auto splitSemicolon = [](char *par_String, std::vector<std::string> &out_Names) {
                        if (nullptr == par_String)
                        {
                            return;
                        }
                        std::string cur;
                        for (const char *p = par_String;; ++p)
                        {
                            if ((0 == *p) || (';' == *p))
                            {
                                if (!cur.empty())
                                {
                                    out_Names.push_back(cur);
                                }
                                cur.clear();
                                if (0 == *p)
                                {
                                    break;
                                }
                            }
                            else
                            {
                                cur.push_back(*p);
                            }
                        }
                    };
                    std::vector<std::string> names;
                    char *schedulerNames{GetNameOfAllSchedulers()};
                    splitSemicolon(schedulerNames, names);
                    if (nullptr != schedulerNames)
                    {
                        my_free(schedulerNames);
                    }
                    char *processNames{GetAllProcessNamesSemicolonSeparated()};
                    splitSemicolon(processNames, names);
                    if (nullptr != processNames)
                    {
                        my_free(processNames);
                    }

                    for (const std::string &name : names)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(name.c_str());
                        for (int col = 0; col < static_cast<int>(barriers.size()); ++col)
                        {
                            ImGui::TableSetColumnIndex(1 + col);
                            switch (GetBarrierState(barriers[static_cast<std::size_t>(col)].c_str(),
                                                    name.c_str()))
                            {
                            case 0: ImGui::TextUnformatted("not waiting"); break;
                            case 1: ImGui::TextUnformatted("waiting"); break;
                            default: ImGui::TextUnformatted("-"); break;
                            }
                        }
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("History"))
            {
                const ImGuiTableFlags flags{ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                            ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY};
                if (ImGui::BeginTable("###BarrierHistoryTable", 18, flags))
                {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("Action");
                    ImGui::TableSetupColumn("Barrier");
                    ImGui::TableSetupColumn("Process/Scheduler name");
                    ImGui::TableSetupColumn("Line");
                    ImGui::TableSetupColumn("SchedulerMask");
                    ImGui::TableSetupColumn("ProcessMask");
                    ImGui::TableSetupColumn("Switch");
                    ImGui::TableSetupColumn("Ib_0_SchedulerFlags");
                    ImGui::TableSetupColumn("Ib_0_ProcessFlags");
                    ImGui::TableSetupColumn("Ib_1_SchedulerFlags");
                    ImGui::TableSetupColumn("Ib_1_ProcessFlags");
                    ImGui::TableSetupColumn("ProcesseOrSchedulerCount");
                    ImGui::TableSetupColumn("AddNewProcessMasksBefore");
                    ImGui::TableSetupColumn("AddNewProcessMasksBehind");
                    ImGui::TableSetupColumn("AddNewSchedulerMasks");
                    ImGui::TableSetupColumn("RemoveProcessMasks");
                    ImGui::TableSetupColumn("RemoveSchedulerMasks");
                    ImGui::TableSetupColumn("Counter");
                    ImGui::TableHeadersRow();

                    char barrierName[MAX_PATH];
                    char procOrSchedName[MAX_PATH];
                    BARRIER_LOGGING_ACTION action{};
                    BARRIER_LOGGING_TYPE procOrSched{};
                    int lineNr{0};
                    unsigned int schedulerMask{0};
                    unsigned int processMask{0};
                    int barrierSwitch{0};
                    unsigned int ib0SchedulerFlags{0};
                    unsigned int ib0ProcessFlags{0};
                    unsigned int ib1SchedulerFlags{0};
                    unsigned int ib1ProcessFlags{0};
                    int poscCount{0};
                    unsigned int addProcBefore{0};
                    unsigned int addProcBehind{0};
                    unsigned int addSched{0};
                    unsigned int removeProc{0};
                    unsigned int removeSched{0};
                    unsigned int counter{0};

                    int index{-1};
                    while (0 <= (index = GetNextBarrierLoggingEntry(
                                     index, barrierName, sizeof(barrierName), &action, &procOrSched,
                                     procOrSchedName, sizeof(procOrSchedName), &lineNr, &schedulerMask,
                                     &processMask, &barrierSwitch, &ib0SchedulerFlags, &ib0ProcessFlags,
                                     &ib1SchedulerFlags, &ib1ProcessFlags, &poscCount, &addProcBefore,
                                     &addProcBehind, &addSched, &removeProc, &removeSched, &counter)))
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(BarrierActionName(action));
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(barrierName);
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextUnformatted(procOrSchedName);
                        ImGui::TableSetColumnIndex(3);
                        ImGui::Text("%d", lineNr);
                        ImGui::TableSetColumnIndex(4);
                        ImGui::Text("%x", schedulerMask);
                        ImGui::TableSetColumnIndex(5);
                        ImGui::Text("%x", processMask);
                        ImGui::TableSetColumnIndex(6);
                        ImGui::Text("%d", barrierSwitch);
                        ImGui::TableSetColumnIndex(7);
                        ImGui::Text("%x", ib0SchedulerFlags);
                        ImGui::TableSetColumnIndex(8);
                        ImGui::Text("%x", ib0ProcessFlags);
                        ImGui::TableSetColumnIndex(9);
                        ImGui::Text("%x", ib1SchedulerFlags);
                        ImGui::TableSetColumnIndex(10);
                        ImGui::Text("%x", ib1ProcessFlags);
                        ImGui::TableSetColumnIndex(11);
                        ImGui::Text("%d", poscCount);
                        ImGui::TableSetColumnIndex(12);
                        ImGui::Text("%x", addProcBefore);
                        ImGui::TableSetColumnIndex(13);
                        ImGui::Text("%x", addProcBehind);
                        ImGui::TableSetColumnIndex(14);
                        ImGui::Text("%x", addSched);
                        ImGui::TableSetColumnIndex(15);
                        ImGui::Text("%x", removeProc);
                        ImGui::TableSetColumnIndex(16);
                        ImGui::Text("%x", removeSched);
                        ImGui::TableSetColumnIndex(17);
                        ImGui::Text("%u", counter);
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void DebugDialogManager::RenderExternProcessCopyList()
{
    if (!m_copyListOpen)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(820.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("External process copy list###ExternProcessCopyList", &m_copyListOpen))
    {
        if (ImGui::Button("Refresh processes"))
        {
            RefreshExternalProcessNames();
            if (m_copyListProcessSel >= static_cast<int>(m_externalProcesses.size()))
            {
                m_copyListProcessSel = m_externalProcesses.empty() ? -1 : 0;
            }
        }

        const char *procPreview{(0 <= m_copyListProcessSel) &&
                                        (m_copyListProcessSel < static_cast<int>(m_externalProcesses.size()))
                                    ? m_externalProcesses[static_cast<std::size_t>(m_copyListProcessSel)].c_str()
                                    : ""};
        ImGui::SetNextItemWidth(240.0f);
        if (ImGui::BeginCombo("Process###CopyListProcess", procPreview))
        {
            for (int i = 0; i < static_cast<int>(m_externalProcesses.size()); ++i)
            {
                const bool selected{i == m_copyListProcessSel};
                if (ImGui::Selectable(m_externalProcesses[static_cast<std::size_t>(i)].c_str(), selected))
                {
                    m_copyListProcessSel = i;
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        static const char *const listTypes[]{"BB <-> Pipe", "BB -> Pipe", "BB <- Pipe"};
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::BeginCombo("Copy list###CopyListType", listTypes[m_copyListType]))
        {
            for (int i = 0; i < 3; ++i)
            {
                if (ImGui::Selectable(listTypes[i], i == m_copyListType))
                {
                    m_copyListType = i;
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText("Filter###CopyListFilter", m_copyListFilter.data(), m_copyListFilter.size());
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText("Address from (hex)###CopyAddrFrom", m_copyListAddrFrom.data(),
                         m_copyListAddrFrom.size());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText("Address to (hex)###CopyAddrTo", m_copyListAddrTo.data(),
                         m_copyListAddrTo.size());
        ImGui::Separator();

        unsigned long long addressFrom{std::strtoull(m_copyListAddrFrom.data(), nullptr, 16)};
        unsigned long long addressTo{std::strtoull(m_copyListAddrTo.data(), nullptr, 16)};
        if (0 == addressTo)
        {
            addressTo = 0xFFFFFFFFFFFFFFFFULL;
        }

        unsigned int totalSize{0};
        if ((0 <= m_copyListProcessSel) &&
            (m_copyListProcessSel < static_cast<int>(m_externalProcesses.size())))
        {
            const int pid{get_pid_by_name(
                m_externalProcesses[static_cast<std::size_t>(m_copyListProcessSel)].c_str())};
            if (0 < pid)
            {
                unsigned long long *addresses{nullptr};
                int *vids{nullptr};
                short *typeBB{nullptr};
                short *typePipe{nullptr};
                const int size{GetCopyListForProcess(pid, m_copyListType, &addresses, &vids, &typeBB,
                                                     &typePipe)};
                const ImGuiTableFlags flags{ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                            ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX};
                if (ImGui::BeginTable("###CopyListTable", 6, flags))
                {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("Index");
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Vid");
                    ImGui::TableSetupColumn("Address");
                    ImGui::TableSetupColumn("TypeBB");
                    ImGui::TableSetupColumn("TypePipe");
                    ImGui::TableHeadersRow();

                    for (int x = 0; x < size; ++x)
                    {
                        if ((addresses[x] < addressFrom) || (addresses[x] > addressTo))
                        {
                            continue;
                        }
                        char nameC[BBVARI_NAME_SIZE];
                        if (0 != GetBlackboardVariableName(vids[x], nameC, sizeof(nameC)))
                        {
                            continue;
                        }
                        if (0 != Compare2StringsWithWildcards(nameC, m_copyListFilter.data()))
                        {
                            continue;
                        }
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%d", x);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(nameC);
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%d", vids[x]);
                        ImGui::TableSetColumnIndex(3);
                        ImGui::Text("%llx", addresses[x]);
                        ImGui::TableSetColumnIndex(4);
                        ImGui::TextUnformatted(GetDataTypeName(typeBB[x]));
                        ImGui::TableSetColumnIndex(5);
                        ImGui::TextUnformatted(GetDataTypeName(typePipe[x]));
                        totalSize += GetDataTypeByteSize(typePipe[x]);
                    }
                    ImGui::EndTable();
                }
                if (0 <= size)
                {
                    my_free(addresses);
                    my_free(vids);
                    my_free(typeBB);
                    my_free(typePipe);
                }
            }
        }
        ImGui::Text("Copy size (bytes): %u", totalSize);
    }
    ImGui::End();
}

void DebugDialogManager::FillReferencedLabels(const std::string &par_Process)
{
    m_refLabelsProcess = par_Process;
    m_refLabelsRows.clear();

    char pnameWithoutPath[MAX_PATH];
    char sectionFilter[INI_MAX_SECTION_LENGTH];
    char section[INI_MAX_SECTION_LENGTH];
    char entry[32];
    char varName[BBVARI_NAME_SIZE];
    char displayName[BBVARI_NAME_SIZE];
    const int fd{GetMainFileDescriptor()};

    TruncatePathFromProcessName(pnameWithoutPath, par_Process.c_str(), sizeof(pnameWithoutPath));
    std::snprintf(sectionFilter, sizeof(sectionFilter), "referenced variables *%s", pnameWithoutPath);
    if (0 > IniFileDataBaseFindNextSectionNameRegExp(0, sectionFilter, 0, section, sizeof(section), fd))
    {
        return;
    }
    for (int row = 0;; ++row)
    {
        std::snprintf(entry, sizeof(entry), "Ref%d", row);
        if (0 == IniFileDataBaseReadString(section, entry, "", varName, BBVARI_NAME_SIZE, fd))
        {
            break;
        }
        RefLabelRow item;
        item.label = varName;
        if (0 == GetDisplayName(section, row, varName, displayName, sizeof(displayName)))
        {
            std::snprintf(item.renameTo.data(), item.renameTo.size(), "%s", displayName);
        }
        m_refLabelsRows.push_back(std::move(item));
    }
}

void DebugDialogManager::WriteReferencedLabelsToIni(const std::string &par_Process)
{
    char pnameWithoutPath[MAX_PATH];
    char sectionFilter[INI_MAX_SECTION_LENGTH];
    char section[INI_MAX_SECTION_LENGTH];
    char entry[32];
    const int fd{GetMainFileDescriptor()};

    TruncatePathFromProcessName(pnameWithoutPath, par_Process.c_str(), sizeof(pnameWithoutPath));
    std::snprintf(sectionFilter, sizeof(sectionFilter), "referenced variables *%s", pnameWithoutPath);
    if (0 > IniFileDataBaseFindNextSectionNameRegExp(0, sectionFilter, 0, section, sizeof(section), fd))
    {
        return;
    }

    const int uniqueNumber{AquireUniqueNumber()};
    int pid{0};
    // PROCESS_APPL_DATA is a typedef of DEBUG_INFOS_ASSOCIATED_CONNECTION (same type).
    DEBUG_INFOS_ASSOCIATED_CONNECTION *debugInfos{
        ConnectToProcessDebugInfos(uniqueNumber, pnameWithoutPath, &pid, nullptr, nullptr, nullptr, 0)};
    if (nullptr == debugInfos)
    {
        FreeUniqueNumber(uniqueNumber);
        return;
    }

    // Dereference / rename every label that changed relative to the persisted ini state.
    char varName[BBVARI_NAME_SIZE];
    char displayName[BBVARI_NAME_SIZE];
    for (int row = 0;; ++row)
    {
        std::snprintf(entry, sizeof(entry), "Ref%d", row);
        if (0 == IniFileDataBaseReadString(section, entry, "", varName, BBVARI_NAME_SIZE, fd))
        {
            break;
        }
        if (0 != GetDisplayName(section, row, varName, displayName, sizeof(displayName)))
        {
            displayName[0] = 0;
        }
        bool found{false};
        for (const RefLabelRow &item : m_refLabelsRows)
        {
            if (0 == CmpVariName(item.label.c_str(), varName))
            {
                if (0 != std::strcmp(item.renameTo.data(), displayName))
                {
                    char name[BBVARI_NAME_SIZE];
                    std::snprintf(name, sizeof(name), "%s", varName);
                    ConvertLabelAsapCombatible(name, sizeof(name), 2);
                    std::uint64_t address{0};
                    std::int32_t typeNr{0};
                    int type{0};
                    if (0 == appl_label(debugInfos, pid, name, &address, &typeNr))
                    {
                        type = get_base_type_bb_type_ex(typeNr, debugInfos);
                        if (0 <= type)
                        {
                            char help[BBVARI_NAME_SIZE + 100];
                            std::snprintf(help, sizeof(help), "%s",
                                          (0 != displayName[0]) ? displayName : varName);
                            ConvertLabelAsapCombatible(help, sizeof(help), 0);
                            scm_unref_vari(address, pid, help, type);
                            std::snprintf(help, sizeof(help), "%s",
                                          (0 != item.renameTo[0]) ? item.renameTo.data() : varName);
                            ConvertLabelAsapCombatible(help, sizeof(help), 0);
                            scm_ref_vari(address, pid, help, type, 0x3);
                        }
                    }
                }
                found = true;
                break;
            }
        }
        if (!found)
        {
            // Fully dereference a removed label.
            char name[BBVARI_NAME_SIZE + 100];
            std::snprintf(name, sizeof(name), "%s", varName);
            ConvertLabelAsapCombatible(name, sizeof(name), 2);
            std::uint64_t address{0};
            std::int32_t typeNr{0};
            if (0 == appl_label(debugInfos, pid, name, &address, &typeNr))
            {
                const int type{get_base_type_bb_type_ex(typeNr, debugInfos)};
                if (0 <= type)
                {
                    char help[BBVARI_NAME_SIZE + 100];
                    std::snprintf(help, sizeof(help), "%s", (0 != displayName[0]) ? displayName : name);
                    ConvertLabelAsapCombatible(help, sizeof(help), 0);
                    scm_unref_vari(address, pid, help, type);
                }
            }
            DelVarFromProcessRefList(pid, name);
        }
    }

    // Rewrite the whole section from the current row list.
    IniFileDataBaseWriteString(section, nullptr, nullptr, fd);
    for (int row = 0; row < static_cast<int>(m_refLabelsRows.size()); ++row)
    {
        const RefLabelRow &item{m_refLabelsRows[static_cast<std::size_t>(row)]};
        std::snprintf(entry, sizeof(entry), "Ref%d", row);
        IniFileDataBaseWriteString(section, entry, item.label.c_str(), fd);
        if ((0 != item.renameTo[0]) && (0 != std::strcmp(item.renameTo.data(), item.label.c_str())))
        {
            char help[INI_MAX_LINE_LENGTH];
            std::snprintf(help, sizeof(help), "%s %s", item.label.c_str(), item.renameTo.data());
            std::snprintf(entry, sizeof(entry), "Dsp%d", row);
            IniFileDataBaseWriteString(section, entry, help, fd);
        }
    }

    RemoveConnectFromProcessDebugInfos(uniqueNumber);
    FreeUniqueNumber(uniqueNumber);
}

void DebugDialogManager::RenderReferencedLabels()
{
    if (!m_refLabelsOpen)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(680.0f, 480.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Referenced labels###ReferencedLabels", &m_refLabelsOpen))
    {
        const char *procPreview{(0 <= m_refLabelsProcessSel) &&
                                        (m_refLabelsProcessSel < static_cast<int>(m_externalProcesses.size()))
                                    ? m_externalProcesses[static_cast<std::size_t>(m_refLabelsProcessSel)].c_str()
                                    : ""};
        ImGui::SetNextItemWidth(240.0f);
        if (ImGui::BeginCombo("Process###RefLabelsProcess", procPreview))
        {
            for (int i = 0; i < static_cast<int>(m_externalProcesses.size()); ++i)
            {
                const bool selected{i == m_refLabelsProcessSel};
                if (ImGui::Selectable(m_externalProcesses[static_cast<std::size_t>(i)].c_str(), selected))
                {
                    m_refLabelsProcessSel = i;
                    FillReferencedLabels(m_externalProcesses[static_cast<std::size_t>(i)]);
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply") && (!m_refLabelsProcess.empty()))
        {
            WriteReferencedLabelsToIni(m_refLabelsProcess);
        }
        ImGui::Separator();

        int deleteRow{-1};
        const ImGuiTableFlags flags{ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                    ImGuiTableFlags_ScrollY};
        if (ImGui::BeginTable("###RefLabelsTable", 3, flags))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("rename to", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableHeadersRow();

            for (int row = 0; row < static_cast<int>(m_refLabelsRows.size()); ++row)
            {
                RefLabelRow &item{m_refLabelsRows[static_cast<std::size_t>(row)]};
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(item.label.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::PushID(row);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputText("###rename", item.renameTo.data(), item.renameTo.size());
                ImGui::TableSetColumnIndex(2);
                if (ImGui::Button("Delete"))
                {
                    deleteRow = row;
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (0 <= deleteRow)
        {
            m_refLabelsRows.erase(m_refLabelsRows.begin() + deleteRow);
        }
    }
    ImGui::End();
}

} // namespace XilEnvImGui
