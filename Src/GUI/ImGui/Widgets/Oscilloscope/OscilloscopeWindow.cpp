#include "Widgets/Oscilloscope/OscilloscopeWindow.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"
#include "implot.h"

#include "Framework/BlackboardDragDrop.h"
#include "Framework/GuiComponents.h"
#include "Framework/WindowManagerBridge.h"

extern "C" {
#include "Config.h"
#include "MainValues.h"
#include "MyMemory.h"
#include "StringMaxChar.h"
#include "Scheduler.h"
#include "Blackboard.h"
#include "BlackboardAccess.h"
#include "GetNotANumber.h"
#include "IniDataBase.h"
#include "OscilloscopeCyclic.h"
}

namespace XilEnvImGui
{

namespace
{

constexpr int OSCI_MAX_SIGNALS{20};      /*!< vids_left/right slots usable for signals (20 is trigger). */
constexpr int OSCI_MIN_BUFFER_DEPTH{1024};
constexpr int OSCI_MAX_BUFFER_DEPTH{1000000};

/**
 * @brief Packs r/g/b into the XilEnv 0x00bbggrr color word (matches the Platform.h RGB macro).
 */
constexpr std::int32_t PackRgb(int par_Red, int par_Green, int par_Blue)
{
    return (par_Red & 0xFF) | ((par_Green & 0xFF) << 8) | ((par_Blue & 0xFF) << 16);
}

constexpr int RgbRed(std::int32_t par_Color)
{
    return par_Color & 0xFF;
}
constexpr int RgbGreen(std::int32_t par_Color)
{
    return (par_Color >> 8) & 0xFF;
}
constexpr int RgbBlue(std::int32_t par_Color)
{
    return (par_Color >> 16) & 0xFF;
}

/**
 * @brief Default line color palette (0x00bbggrr) cycled per signal slot.
 */
const std::int32_t OSCI_DEFAULT_COLORS[]{
    PackRgb(220, 60, 60),  PackRgb(60, 180, 75),  PackRgb(70, 130, 220), PackRgb(230, 170, 40),
    PackRgb(180, 90, 200), PackRgb(60, 200, 200), PackRgb(230, 120, 180), PackRgb(150, 150, 90),
    PackRgb(120, 200, 120), PackRgb(200, 120, 60)};

/**
 * @brief Converts a 0x00bbggrr blackboard color into an ImVec4 (RGBA, 0..1).
 */
ImVec4 BlackboardColorToImVec4(std::int32_t par_Color)
{
    return ImVec4(static_cast<float>(RgbRed(par_Color)) / 255.0f,
                  static_cast<float>(RgbGreen(par_Color)) / 255.0f,
                  static_cast<float>(RgbBlue(par_Color)) / 255.0f, 1.0f);
}

/**
 * @brief Registry of live oscilloscope windows, keyed by their identity pointer.
 * @details Lets the drained StopOscilloscope command find the target window (the Qt build
 *          hands the OscilloscopeWidget* through the same seam).
 */
std::vector<OscilloscopeWindow *> g_oscilloscopes;

void RegisterOscilloscope(OscilloscopeWindow *par_Window)
{
    g_oscilloscopes.push_back(par_Window);
}

void UnregisterOscilloscope(OscilloscopeWindow *par_Window)
{
    for (std::size_t i{0}; i < g_oscilloscopes.size(); ++i)
    {
        if (par_Window == g_oscilloscopes[i])
        {
            g_oscilloscopes.erase(g_oscilloscopes.begin() + static_cast<long>(i));
            return;
        }
    }
}

} // namespace

std::unique_ptr<IWindowInstance> OscilloscopeWindowType::CreateNew(const std::string &par_Title)
{
    return std::make_unique<OscilloscopeWindow>(this, par_Title);
}

std::unique_ptr<IWindowInstance> OscilloscopeWindowType::CreateFromConfig(int par_FileDescriptor,
                                                                          const char *par_Section,
                                                                          const std::string &par_Name)
{
    auto window = std::make_unique<OscilloscopeWindow>(this, par_Name);
    window->ReadConfig(par_FileDescriptor, par_Section);
    return window;
}

OscilloscopeWindow::OscilloscopeWindow(IWindowType *par_Type, std::string par_Name) : m_pType{par_Type}
{
    SetName(std::move(par_Name));
    InitData();
    if (0 <= m_data.CriticalSectionNumber)
    {
        m_registered = (0 == RegisterOscilloscopeToCyclic(&m_data));
        ReconnectSampler();
    }
    RegisterOscilloscope(this);
}

OscilloscopeWindow::~OscilloscopeWindow()
{
    UnregisterOscilloscope(this);

    if (0 <= m_data.CriticalSectionNumber)
    {
        FreeAllSignals();
        if (m_registered)
        {
            UnregisterOscilloscopeFromCyclic(&m_data);
        }
        DeleteOsciWidgetCriticalSection(m_data.CriticalSectionNumber);
        m_data.CriticalSectionNumber = -1;
    }
}

void OscilloscopeWindow::InitData()
{
    std::memset(&m_data, 0, sizeof(m_data));
    m_data.NotANumber = GetNotANumber();
    m_data.CriticalSectionNumber = AllocOsciWidgetCriticalSection();

    m_data.state = 1; // on
    m_data.left_axis_on = 1;
    m_data.right_axis_on = 1;

    int depth{s_main_ini_val.OscilloscopeDefaultBufferDepth};
    if (OSCI_MIN_BUFFER_DEPTH > depth)
    {
        depth = OSCI_MIN_BUFFER_DEPTH;
    }
    if (OSCI_MAX_BUFFER_DEPTH < depth)
    {
        depth = OSCI_MAX_BUFFER_DEPTH;
    }
    m_data.buffer_depth = depth;
    m_data.sample_steps = 1;
    m_data.window_step_width = 20;

    m_data.t_step = get_sched_periode_timer_clocks64();
    if (0u == m_data.t_step)
    {
        m_data.t_step = static_cast<std::uint64_t>(TIMERCLKFRQ / 100.0);
    }
    m_data.y_zoom[0] = 1.0;

    const std::uint64_t now{GetSimulatedTimeInNanoSecond()};
    m_data.t_window_size_configered = m_timeWindow;
    m_data.t_window_size = static_cast<std::uint64_t>(m_timeWindow * TIMERCLKFRQ);
    m_data.t_window_start = now;
    m_data.t_window_end = now + m_data.t_window_size;
    m_data.t_current_buffer_end = now;
    m_data.t_current_to_update_end = now;
    m_data.t_current_updated_end = now;
    m_data.t_step_win = (m_data.t_window_size / 100u) * static_cast<std::uint64_t>(m_data.window_step_width);

    for (int x{0}; x < OSCI_MAX_SIGNALS; ++x)
    {
        m_data.max_left[x] = 1.0;
        m_data.min_left[x] = -1.0;
        m_data.max_right[x] = 1.0;
        m_data.min_right[x] = -1.0;
        m_data.LineSize_left[x] = 1;
        m_data.LineSize_right[x] = 1;
    }
    m_acquiring = true;
}

void OscilloscopeWindow::ReconnectSampler()
{
    (void)update_vid_owc(&m_data);
}

int OscilloscopeWindow::FindFreeSlot(Axis par_Axis) const
{
    const long *vids{(Axis::Left == par_Axis) ? m_data.vids_left : m_data.vids_right};
    for (int x{0}; x < OSCI_MAX_SIGNALS; ++x)
    {
        if (0 >= vids[x])
        {
            return x;
        }
    }
    return -1;
}

int OscilloscopeWindow::AddSignal(Axis par_Axis, const BlackboardVariablePayload &par_Payload, int par_LineSize)
{
    if (par_Payload.name.empty() || (0 > m_data.CriticalSectionNumber))
    {
        return -1;
    }
    const int slot{FindFreeSlot(par_Axis)};
    if (0 > slot)
    {
        return -1;
    }

    const VID vid{add_bbvari(par_Payload.name.c_str(), BB_UNKNOWN_WAIT, nullptr)};
    if (0 >= vid)
    {
        return -1;
    }
    double *buffer{static_cast<double *>(my_calloc(static_cast<size_t>(m_data.buffer_depth), sizeof(double)))};
    if (nullptr == buffer)
    {
        (void)remove_bbvari_unknown_wait(vid);
        return -1;
    }

    int lineSize{par_LineSize};
    if ((1 > lineSize) || (10 < lineSize))
    {
        lineSize = 1;
    }

    const double minValue{par_Payload.hasMinMax ? par_Payload.minValue : -1.0};
    double maxValue{par_Payload.hasMinMax ? par_Payload.maxValue : 1.0};
    if (maxValue <= minValue)
    {
        maxValue = minValue + 1.0;
    }
    const std::int32_t color{par_Payload.hasColor
                                 ? par_Payload.color
                                 : OSCI_DEFAULT_COLORS[slot % static_cast<int>(sizeof(OSCI_DEFAULT_COLORS) /
                                                                               sizeof(OSCI_DEFAULT_COLORS[0]))]};
    const char physical{(DragDropDisplayMode::Physical == par_Payload.displayMode) ? static_cast<char>(1)
                                                                                   : static_cast<char>(0)};

    EnterOsciWidgetCriticalSection(m_data.CriticalSectionNumber);
    if (Axis::Left == par_Axis)
    {
        m_data.vids_left[slot] = vid;
        m_data.name_left[slot] = StringRealloc(m_data.name_left[slot], par_Payload.name.c_str());
        m_data.buffer_left[slot] = buffer;
        m_data.wr_poi_left[slot] = 0;
        m_data.depth_left[slot] = 0;
        m_data.new_since_last_draw_left[slot] = 0;
        m_data.min_left[slot] = minValue;
        m_data.max_left[slot] = maxValue;
        m_data.color_left[slot] = color;
        m_data.dec_phys_left[slot] = physical;
        m_data.LineSize_left[slot] = lineSize;
    }
    else
    {
        m_data.vids_right[slot] = vid;
        m_data.name_right[slot] = StringRealloc(m_data.name_right[slot], par_Payload.name.c_str());
        m_data.buffer_right[slot] = buffer;
        m_data.wr_poi_right[slot] = 0;
        m_data.depth_right[slot] = 0;
        m_data.new_since_last_draw_right[slot] = 0;
        m_data.min_right[slot] = minValue;
        m_data.max_right[slot] = maxValue;
        m_data.color_right[slot] = color;
        m_data.dec_phys_right[slot] = physical;
        m_data.LineSize_right[slot] = lineSize;
    }
    LeaveOsciWidgetCriticalSection(m_data.CriticalSectionNumber);

    ReconnectSampler();
    return slot;
}

void OscilloscopeWindow::RemoveSignal(Axis par_Axis, int par_Index)
{
    if ((0 > par_Index) || (OSCI_MAX_SIGNALS <= par_Index) || (0 > m_data.CriticalSectionNumber))
    {
        return;
    }

    long vid{0};
    double *buffer{nullptr};
    EnterOsciWidgetCriticalSection(m_data.CriticalSectionNumber);
    if (Axis::Left == par_Axis)
    {
        vid = m_data.vids_left[par_Index];
        buffer = m_data.buffer_left[par_Index];
        m_data.vids_left[par_Index] = 0;
        m_data.buffer_left[par_Index] = nullptr;
        if (nullptr != m_data.name_left[par_Index])
        {
            my_free(m_data.name_left[par_Index]);
            m_data.name_left[par_Index] = nullptr;
        }
        m_data.wr_poi_left[par_Index] = 0;
        m_data.depth_left[par_Index] = 0;
    }
    else
    {
        vid = m_data.vids_right[par_Index];
        buffer = m_data.buffer_right[par_Index];
        m_data.vids_right[par_Index] = 0;
        m_data.buffer_right[par_Index] = nullptr;
        if (nullptr != m_data.name_right[par_Index])
        {
            my_free(m_data.name_right[par_Index]);
            m_data.name_right[par_Index] = nullptr;
        }
        m_data.wr_poi_right[par_Index] = 0;
        m_data.depth_right[par_Index] = 0;
    }
    LeaveOsciWidgetCriticalSection(m_data.CriticalSectionNumber);

    // Disconnect the VID from the sampler before releasing its resources.
    ReconnectSampler();

    if (0 < vid)
    {
        (void)remove_bbvari_unknown_wait(vid);
    }
    if (nullptr != buffer)
    {
        my_free(buffer);
    }
}

void OscilloscopeWindow::FreeAllSignals()
{
    EnterOsciWidgetCriticalSection(m_data.CriticalSectionNumber);
    for (int i{0}; i < 21; ++i)
    {
        if (0 < m_data.vids_left[i])
        {
            (void)remove_bbvari_unknown_wait(m_data.vids_left[i]);
        }
        m_data.vids_left[i] = 0;
        if (nullptr != m_data.name_left[i])
        {
            my_free(m_data.name_left[i]);
            m_data.name_left[i] = nullptr;
        }
        if (0 < m_data.vids_right[i])
        {
            (void)remove_bbvari_unknown_wait(m_data.vids_right[i]);
        }
        m_data.vids_right[i] = 0;
        if (nullptr != m_data.name_right[i])
        {
            my_free(m_data.name_right[i]);
            m_data.name_right[i] = nullptr;
        }
    }
    LeaveOsciWidgetCriticalSection(m_data.CriticalSectionNumber);

    // Drop the now-dangling connections from the sampler before freeing buffers.
    ReconnectSampler();

    EnterOsciWidgetCriticalSection(m_data.CriticalSectionNumber);
    for (int i{0}; i < 21; ++i)
    {
        if (nullptr != m_data.buffer_left[i])
        {
            my_free(m_data.buffer_left[i]);
            m_data.buffer_left[i] = nullptr;
        }
        if (nullptr != m_data.buffer_right[i])
        {
            my_free(m_data.buffer_right[i]);
            m_data.buffer_right[i] = nullptr;
        }
    }
    LeaveOsciWidgetCriticalSection(m_data.CriticalSectionNumber);
}

void OscilloscopeWindow::StartAcquisition()
{
    if (0 > m_data.CriticalSectionNumber)
    {
        return;
    }
    EnterOsciWidgetCriticalSection(m_data.CriticalSectionNumber);
    m_data.state = 1;
    SwitchOscilloscopeOnline(&m_data, GetSimulatedTimeSinceStartedInNanoSecond(), 0);
    LeaveOsciWidgetCriticalSection(m_data.CriticalSectionNumber);
    m_acquiring = true;
}

void OscilloscopeWindow::StopAcquisition(std::uint64_t par_EndTime)
{
    if (0 > m_data.CriticalSectionNumber)
    {
        return;
    }
    EnterOsciWidgetCriticalSection(m_data.CriticalSectionNumber);
    m_data.state = 0;
    m_data.t_current_to_update_end = (0u != par_EndTime) ? par_EndTime : m_data.t_current_buffer_end;
    m_data.t_current_updated_end = m_data.t_current_to_update_end;
    m_data.t_cursor = m_data.t_current_to_update_end;
    LeaveOsciWidgetCriticalSection(m_data.CriticalSectionNumber);
    m_acquiring = false;
}

void OscilloscopeWindow::ClearData()
{
    if (0 > m_data.CriticalSectionNumber)
    {
        return;
    }
    EnterOsciWidgetCriticalSection(m_data.CriticalSectionNumber);
    for (int x{0}; x < 21; ++x)
    {
        m_data.wr_poi_left[x] = 0;
        m_data.depth_left[x] = 0;
        m_data.wr_poi_right[x] = 0;
        m_data.depth_right[x] = 0;
        m_data.new_since_last_draw_left[x] = 0;
        m_data.new_since_last_draw_right[x] = 0;
    }
    LeaveOsciWidgetCriticalSection(m_data.CriticalSectionNumber);
}

void OscilloscopeWindow::OnUpdateTick(const WindowContext &par_Context)
{
    (void)par_Context;
    // The C sampler fills the ring buffers on the scheduler thread; nothing to
    // cache here. Keep the acquisition flag in sync with the shared state field.
    m_acquiring = (0 != m_data.state);
}

void OscilloscopeWindow::OnAcceptVariableDrop(const std::string &par_VariableName)
{
    BlackboardVariablePayload payload;
    payload.name = par_VariableName;
    AddSignal(Axis::Left, payload);
}

namespace
{

/**
 * @brief Builds oldest->newest (time[s], value) arrays from one signal's ring buffer.
 * @return Number of valid samples written into out_X/out_Y.
 */
int BuildSeries(OSCILLOSCOPE_DATA &par_Data, bool par_Left, int par_Slot, std::vector<double> &out_X,
                std::vector<double> &out_Y)
{
    out_X.clear();
    out_Y.clear();
    const int bufferDepth{par_Data.buffer_depth};
    if ((0 >= bufferDepth) || (0 > par_Data.CriticalSectionNumber))
    {
        return 0;
    }

    EnterOsciWidgetCriticalSection(par_Data.CriticalSectionNumber);
    const int depth{par_Left ? par_Data.depth_left[par_Slot] : par_Data.depth_right[par_Slot]};
    const int writePos{par_Left ? par_Data.wr_poi_left[par_Slot] : par_Data.wr_poi_right[par_Slot]};
    const double *buffer{par_Left ? par_Data.buffer_left[par_Slot] : par_Data.buffer_right[par_Slot]};
    const std::uint64_t tEnd{(0 != par_Data.state) ? par_Data.t_current_buffer_end
                                                    : par_Data.t_current_updated_end};
    const std::uint64_t tStep{(0u != par_Data.t_step) ? par_Data.t_step : 1u};

    int count{depth};
    const std::uint64_t maxSamples{(tEnd / tStep) + 1u};
    if (static_cast<std::uint64_t>(count) > maxSamples)
    {
        count = static_cast<int>(maxSamples);
    }

    if ((nullptr != buffer) && (0 < count))
    {
        out_X.resize(static_cast<size_t>(count));
        out_Y.resize(static_cast<size_t>(count));
        for (int j{0}; j < count; ++j)
        {
            const int age{count - 1 - j}; // 0 = newest sample
            const std::uint64_t offset{static_cast<std::uint64_t>(age) * tStep};
            int memPos{writePos - 1 - age};
            if (0 > memPos)
            {
                memPos += bufferDepth;
            }
            out_X[static_cast<size_t>(j)] = static_cast<double>(tEnd - offset) / TIMERCLKFRQ;
            out_Y[static_cast<size_t>(j)] = buffer[memPos];
        }
    }
    else
    {
        count = 0;
    }
    LeaveOsciWidgetCriticalSection(par_Data.CriticalSectionNumber);
    return count;
}

} // namespace

void OscilloscopeWindow::RenderContent(const WindowContext &par_Context)
{
    (void)par_Context;

    // -- Acquisition toolbar (mirrors the Qt online/offline + clear actions) --
    if (m_acquiring)
    {
        if (ImGui::Button("Stop"))
        {
            StopAcquisition(GetSimulatedTimeInNanoSecond());
        }
    }
    else
    {
        if (ImGui::Button("Start"))
        {
            StartAcquisition();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
    {
        ClearData();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(drop a variable to add a signal)");

    // -- Determine axis usage --
    bool hasRight{false};
    for (int x{0}; x < OSCI_MAX_SIGNALS; ++x)
    {
        if (0 < m_data.vids_right[x])
        {
            hasRight = true;
            break;
        }
    }

    double leftMin{DBL_MAX};
    double leftMax{-DBL_MAX};
    double rightMin{DBL_MAX};
    double rightMax{-DBL_MAX};
    for (int x{0}; x < OSCI_MAX_SIGNALS; ++x)
    {
        if (0 < m_data.vids_left[x])
        {
            leftMin = std::min(leftMin, m_data.min_left[x]);
            leftMax = std::max(leftMax, m_data.max_left[x]);
        }
        if (0 < m_data.vids_right[x])
        {
            rightMin = std::min(rightMin, m_data.min_right[x]);
            rightMax = std::max(rightMax, m_data.max_right[x]);
        }
    }
    if (leftMin > leftMax)
    {
        leftMin = -1.0;
        leftMax = 1.0;
    }
    if (rightMin > rightMax)
    {
        rightMin = -1.0;
        rightMax = 1.0;
    }

    const std::string plotId{"##osci_" + GetName()};
    if (ImPlot::BeginPlot(plotId.c_str(), ImVec2(-1.0f, -1.0f), ImPlotFlags_None))
    {
        ImPlot::SetupAxis(ImAxis_X1, "t [s]");
        ImPlot::SetupAxis(ImAxis_Y1, nullptr);
        if (hasRight)
        {
            ImPlot::SetupAxis(ImAxis_Y2, nullptr, ImPlotAxisFlags_AuxDefault);
        }

        const std::uint64_t tEnd{(0 != m_data.state) ? m_data.t_current_buffer_end
                                                     : m_data.t_current_updated_end};
        const double tEndSec{static_cast<double>(tEnd) / TIMERCLKFRQ};
        // While acquiring, keep the newest samples in view (scrolling window);
        // when stopped, only set limits once so the user can freely pan/zoom.
        const ImPlotCond xCond{m_acquiring ? ImPlotCond_Always : ImPlotCond_Once};
        ImPlot::SetupAxisLimits(ImAxis_X1, tEndSec - m_timeWindow, tEndSec, xCond);
        ImPlot::SetupAxisLimits(ImAxis_Y1, leftMin, leftMax, ImPlotCond_Once);
        if (hasRight)
        {
            ImPlot::SetupAxisLimits(ImAxis_Y2, rightMin, rightMax, ImPlotCond_Once);
        }

        // -- Left axis signals --
        ImPlot::SetAxis(ImAxis_Y1);
        for (int x{0}; x < OSCI_MAX_SIGNALS; ++x)
        {
            if ((0 >= m_data.vids_left[x]) || (0 != m_data.vars_disable_left[x]))
            {
                continue;
            }
            const int n{BuildSeries(m_data, true, x, m_plotX, m_plotY)};
            if (0 >= n)
            {
                continue;
            }
            const char *label{(nullptr != m_data.name_left[x]) ? m_data.name_left[x] : "?"};
            if (0 != m_data.color_left[x])
            {
                ImPlot::SetNextLineStyle(BlackboardColorToImVec4(m_data.color_left[x]),
                                         static_cast<float>(m_data.LineSize_left[x]));
            }
            ImPlot::PlotLine(label, m_plotX.data(), m_plotY.data(), n);
        }

        // -- Right axis signals --
        if (hasRight)
        {
            ImPlot::SetAxis(ImAxis_Y2);
        }
        for (int x{0}; x < OSCI_MAX_SIGNALS; ++x)
        {
            if ((0 >= m_data.vids_right[x]) || (0 != m_data.vars_disable_right[x]))
            {
                continue;
            }
            const int n{BuildSeries(m_data, false, x, m_plotX, m_plotY)};
            if (0 >= n)
            {
                continue;
            }
            const char *label{(nullptr != m_data.name_right[x]) ? m_data.name_right[x] : "?"};
            if (0 != m_data.color_right[x])
            {
                ImPlot::SetNextLineStyle(BlackboardColorToImVec4(m_data.color_right[x]),
                                         static_cast<float>(m_data.LineSize_right[x]));
            }
            ImPlot::PlotLine(label, m_plotX.data(), m_plotY.data(), n);
        }

        // Accept a dropped blackboard variable anywhere on the plot.
        if (ImPlot::BeginDragDropTargetPlot())
        {
            BlackboardVariablePayload payload;
            if (AcceptBlackboardVariableDrop(payload))
            {
                AddSignal(Axis::Left, payload);
            }
            ImGui::EndDragDropTarget();
        }

        ImPlot::EndPlot();
    }
}

void OscilloscopeWindow::RenderSettingsPopup()
{
    ImGui::TextUnformatted("Time window [s]:");
    if (ImGui::InputDouble("##timewindow", &m_timeWindow, 0.0, 0.0, "%.3f"))
    {
        m_timeWindow = std::max(0.001, std::min(1000.0, m_timeWindow));
        m_data.t_window_size_configered = m_timeWindow;
        m_data.t_window_size = static_cast<std::uint64_t>(m_timeWindow * TIMERCLKFRQ);
        m_data.t_step_win = (m_data.t_window_size / 100u) * static_cast<std::uint64_t>(m_data.window_step_width);
    }

    int bufferDepth{m_data.buffer_depth};
    if (ImGui::InputInt("Buffer depth", &bufferDepth))
    {
        bufferDepth = std::max(OSCI_MIN_BUFFER_DEPTH, std::min(OSCI_MAX_BUFFER_DEPTH, bufferDepth));
        // New depth applies to signals added afterwards; existing buffers keep their size.
        m_data.buffer_depth = bufferDepth;
    }

    bool leftOn{0 != m_data.left_axis_on};
    if (ImGui::Checkbox("Left axis", &leftOn))
    {
        m_data.left_axis_on = leftOn ? 1 : 0;
    }
    ImGui::SameLine();
    bool rightOn{0 != m_data.right_axis_on};
    if (ImGui::Checkbox("Right axis", &rightOn))
    {
        m_data.right_axis_on = rightOn ? 1 : 0;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Add signal:");
    const char *axisItems[]{"Left", "Right"};
    ImGui::Combo("Axis", &m_dropAxis, axisItems, 2);
    if (DrawBlackboardSelector("##addvar", m_dropBuffer, sizeof(m_dropBuffer)))
    {
        // selection confirmed by pressing Add below
    }
    ImGui::SameLine();
    if (ImGui::Button("Add") && ('\0' != m_dropBuffer[0]))
    {
        BlackboardVariablePayload payload;
        payload.name = m_dropBuffer;
        AddSignal((0 == m_dropAxis) ? Axis::Left : Axis::Right, payload);
        m_dropBuffer[0] = '\0';
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Signals:");
    for (int x{0}; x < OSCI_MAX_SIGNALS; ++x)
    {
        if (0 < m_data.vids_left[x])
        {
            ImGui::PushID(x);
            ImGui::TextColored(BlackboardColorToImVec4(m_data.color_left[x]), "L: %s",
                               (nullptr != m_data.name_left[x]) ? m_data.name_left[x] : "?");
            ImGui::SameLine();
            if (ImGui::SmallButton("x"))
            {
                RemoveSignal(Axis::Left, x);
            }
            ImGui::PopID();
        }
    }
    for (int x{0}; x < OSCI_MAX_SIGNALS; ++x)
    {
        if (0 < m_data.vids_right[x])
        {
            ImGui::PushID(1000 + x);
            ImGui::TextColored(BlackboardColorToImVec4(m_data.color_right[x]), "R: %s",
                               (nullptr != m_data.name_right[x]) ? m_data.name_right[x] : "?");
            ImGui::SameLine();
            if (ImGui::SmallButton("x"))
            {
                RemoveSignal(Axis::Right, x);
            }
            ImGui::PopID();
        }
    }
}

namespace
{

/**
 * @brief Formats one signal into the Qt "vlN"/"vrN" entry string.
 */
void FormatSignalEntry(char *par_Buffer, size_t par_Size, const char *par_Name, double par_Min, double par_Max,
                       std::int32_t par_Color, char par_Physical, int par_LineSize)
{
    std::snprintf(par_Buffer, par_Size, "%s, %g, %g, (%d,%d,%d), %s, %d, l, e, l", par_Name, par_Min, par_Max,
                  RgbRed(par_Color), RgbGreen(par_Color), RgbBlue(par_Color),
                  (0 != par_Physical) ? "phys" : "dec", par_LineSize);
}

/**
 * @brief Parses a Qt "vlN"/"vrN" entry string into a drop payload plus line size.
 * @return true if at least a name was extracted.
 */
bool ParseSignalEntry(char *par_Text, BlackboardVariablePayload &out_Payload, int &out_LineSize)
{
    out_LineSize = 1;
    char nameBuffer[BBVARI_NAME_SIZE];
    char *token{std::strtok(par_Text, ",")};
    if ((nullptr == token) || (1 != std::sscanf(token, "%511s", nameBuffer)))
    {
        return false;
    }
    out_Payload.name = nameBuffer;

    token = std::strtok(nullptr, ",");
    if ((nullptr != token) && (1 == std::sscanf(token, "%lf", &out_Payload.minValue)))
    {
        token = std::strtok(nullptr, ",");
        if ((nullptr != token) && (1 == std::sscanf(token, "%lf", &out_Payload.maxValue)))
        {
            out_Payload.hasMinMax = true;
        }
    }

    // Color "(r,g,b)"
    token = std::strtok(nullptr, "(");
    if (nullptr != token)
    {
        token = std::strtok(nullptr, ")");
        int red{0};
        int green{0};
        int blue{0};
        if ((nullptr != token) && (3 == std::sscanf(token, "%d,%d,%d", &red, &green, &blue)))
        {
            out_Payload.hasColor = true;
            out_Payload.color = PackRgb(red, green, blue);
        }
    }

    // Notation "phys"/"dec"/"hex"/"bin"
    token = std::strtok(nullptr, ",");
    if (nullptr != token)
    {
        while ((' ' == *token) || ('\t' == *token))
        {
            ++token;
        }
        if (0 == std::strncmp(token, "phys", 4))
        {
            out_Payload.displayMode = DragDropDisplayMode::Physical;
        }
        // Line size
        token = std::strtok(nullptr, ",");
        if ((nullptr != token) && (1 == std::sscanf(token, "%d", &out_LineSize)))
        {
            if ((1 > out_LineSize) || (10 < out_LineSize))
            {
                out_LineSize = 1;
            }
        }
    }
    return true;
}

} // namespace

void OscilloscopeWindow::WriteConfig(int par_FileDescriptor, const char *par_Section)
{
    (void)IniFileDataBaseWriteString(par_Section, "type", m_pType->GetTypeName(), par_FileDescriptor);
    (void)IniFileDataBaseWriteInt(par_Section, "buffer_depth", m_data.buffer_depth, par_FileDescriptor);
    (void)IniFileDataBaseWriteInt(par_Section, "sample_steps", m_data.sample_steps, par_FileDescriptor);
    (void)IniFileDataBaseWriteInt(par_Section, "left_axis_on", m_data.left_axis_on, par_FileDescriptor);
    (void)IniFileDataBaseWriteInt(par_Section, "right_axis_on", m_data.right_axis_on, par_FileDescriptor);
    (void)IniFileDataBaseWriteInt(par_Section, "window_step_width", m_data.window_step_width, par_FileDescriptor);
    (void)IniFileDataBaseWriteFloat(par_Section, "time_window", m_data.t_window_size_configered, par_FileDescriptor);

    char entry[16];
    char value[BBVARI_NAME_SIZE + 100];
    for (int x{0}; x < OSCI_MAX_SIGNALS; ++x)
    {
        std::snprintf(entry, sizeof(entry), "vl%d", x);
        if ((0 < m_data.vids_left[x]) && (nullptr != m_data.name_left[x]))
        {
            FormatSignalEntry(value, sizeof(value), m_data.name_left[x], m_data.min_left[x], m_data.max_left[x],
                              m_data.color_left[x], m_data.dec_phys_left[x], m_data.LineSize_left[x]);
            (void)IniFileDataBaseWriteString(par_Section, entry, value, par_FileDescriptor);
        }
        else
        {
            (void)IniFileDataBaseWriteString(par_Section, entry, nullptr, par_FileDescriptor);
        }

        std::snprintf(entry, sizeof(entry), "vr%d", x);
        if ((0 < m_data.vids_right[x]) && (nullptr != m_data.name_right[x]))
        {
            FormatSignalEntry(value, sizeof(value), m_data.name_right[x], m_data.min_right[x], m_data.max_right[x],
                              m_data.color_right[x], m_data.dec_phys_right[x], m_data.LineSize_right[x]);
            (void)IniFileDataBaseWriteString(par_Section, entry, value, par_FileDescriptor);
        }
        else
        {
            (void)IniFileDataBaseWriteString(par_Section, entry, nullptr, par_FileDescriptor);
        }
    }
}

void OscilloscopeWindow::ReadConfig(int par_FileDescriptor, const char *par_Section)
{
    int bufferDepth{IniFileDataBaseReadInt(par_Section, "buffer_depth", m_data.buffer_depth, par_FileDescriptor)};
    bufferDepth = std::max(OSCI_MIN_BUFFER_DEPTH, std::min(OSCI_MAX_BUFFER_DEPTH, bufferDepth));
    m_data.buffer_depth = bufferDepth;
    m_data.sample_steps = IniFileDataBaseReadInt(par_Section, "sample_steps", 1, par_FileDescriptor);
    m_data.left_axis_on = IniFileDataBaseReadInt(par_Section, "left_axis_on", 1, par_FileDescriptor);
    m_data.right_axis_on = IniFileDataBaseReadInt(par_Section, "right_axis_on", 1, par_FileDescriptor);
    m_data.window_step_width = IniFileDataBaseReadInt(par_Section, "window_step_width", 20, par_FileDescriptor);

    m_timeWindow = IniFileDataBaseReadFloat(par_Section, "time_window", 10.0, par_FileDescriptor);
    m_timeWindow = std::max(0.001, std::min(1000.0, m_timeWindow));
    m_data.t_window_size_configered = m_timeWindow;
    m_data.t_window_size = static_cast<std::uint64_t>(m_timeWindow * TIMERCLKFRQ);
    m_data.t_window_end = m_data.t_window_start + m_data.t_window_size;
    m_data.t_step_win = (m_data.t_window_size / 100u) * static_cast<std::uint64_t>(m_data.window_step_width);

    char entry[16];
    char text[BBVARI_NAME_SIZE + 100];
    for (int x{0}; x < OSCI_MAX_SIGNALS; ++x)
    {
        std::snprintf(entry, sizeof(entry), "vl%d", x);
        if (0 < IniFileDataBaseReadString(par_Section, entry, "", text, sizeof(text), par_FileDescriptor))
        {
            BlackboardVariablePayload payload;
            int lineSize{1};
            if (ParseSignalEntry(text, payload, lineSize))
            {
                (void)AddSignal(Axis::Left, payload, lineSize);
            }
        }
        std::snprintf(entry, sizeof(entry), "vr%d", x);
        if (0 < IniFileDataBaseReadString(par_Section, entry, "", text, sizeof(text), par_FileDescriptor))
        {
            BlackboardVariablePayload payload;
            int lineSize{1};
            if (ParseSignalEntry(text, payload, lineSize))
            {
                (void)AddSignal(Axis::Right, payload, lineSize);
            }
        }
    }

    // Windows are loaded in the "on" state (state == 1) exactly like the Qt build.
    StartAcquisition();
}

} // namespace XilEnvImGui

// -------------------------------------------------------------------------
// Bridge free function used by the command-queue seam (StopOscilloscope).
// Defined here so the WindowManager stays decoupled from the concrete type.
// -------------------------------------------------------------------------
namespace XilEnvImGui
{

int WmStopOscilloscope(void *par_Widget, std::uint64_t par_Time)
{
    for (OscilloscopeWindow *window : g_oscilloscopes)
    {
        if (static_cast<void *>(window) == par_Widget)
        {
            window->StopAcquisition(par_Time);
            return 1;
        }
    }
    return 0;
}

} // namespace XilEnvImGui
