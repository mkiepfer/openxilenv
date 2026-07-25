/**
 * @file Src/GUI/ImGui/Widgets/Oscilloscope/OscilloscopeWindow.h
 * @brief ImGui/ImPlot port of the Qt Oscilloscope: real-time plot fed by the C ring-buffer sampler.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_WIDGETS_OSCILLOSCOPE_OSCILLOSCOPEWINDOW_H
#define SRC_GUI_IMGUI_WIDGETS_OSCILLOSCOPE_OSCILLOSCOPEWINDOW_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Framework/BlackboardDragDrop.h"
#include "Framework/IWindowInstance.h"
#include "Framework/IWindowType.h"
#include "Framework/WindowContext.h"

extern "C" {
#include "OscilloscopeData.h"
}

namespace XilEnvImGui
{

/**
 * @class OscilloscopeWindowType
 * @brief Window type/factory for Oscilloscope windows (mirrors the Qt OscilloscopeType, Fast update).
 * @details HasStorage() is true because the ring buffers must survive sheet switches, exactly like
 *          the Qt oscilloscope which keeps sampling in the scheduler thread while hidden.
 */
class OscilloscopeWindowType : public IWindowType
{
public:
    [[nodiscard]] UpdateRate GetUpdateRate() const override
    {
        return UpdateRate::Fast;
    }
    [[nodiscard]] const char *GetTypeName() const override
    {
        return "Oscilloscope";
    }
    [[nodiscard]] const char *GetIniListName() const override
    {
        return "GUI/AllOscilloscopeWindows";
    }
    [[nodiscard]] const char *GetMenuEntryName() const override
    {
        return "Oscilloscope";
    }
    [[nodiscard]] bool HasStorage() const override
    {
        return true;
    }
    [[nodiscard]] std::unique_ptr<IWindowInstance> CreateNew(const std::string &par_Title) override;
    [[nodiscard]] std::unique_ptr<IWindowInstance> CreateFromConfig(int par_FileDescriptor,
                                                                    const char *par_Section,
                                                                    const std::string &par_Name) override;
};

/**
 * @class OscilloscopeWindow
 * @brief One Oscilloscope window: an ImPlot time plot of blackboard signals over a scrolling window.
 * @details Owns an OSCILLOSCOPE_DATA sampled by the scheduler-thread task in OscilloscopeCyclic.c
 *          (registered via RegisterOscilloscopeToCyclic). Every frame it walks the C ring buffers
 *          into (time, value) arrays and draws each signal as an ImPlot line in its configured color,
 *          on a left (Y1) and/or right (Y2) axis. Signals are added by dropping a "BBVAR" payload.
 *          Config round-trips to the Qt "GUI/Widgets/<name>" section (buffer_depth, time_window,
 *          sample_steps, left/right_axis_on, vl0..vl19 / vr0..vr19).
 * @warning Render-thread only; the ring-buffer reads are guarded by the per-window widget critical
 *          section shared with the scheduler-thread sampler.
 */
class OscilloscopeWindow : public IWindowInstance
{
public:
    /**
     * @enum class Axis
     * @brief Which Y axis a signal belongs to.
     */
    enum class Axis : std::uint8_t
    {
        Left = 0, /*!< Left axis (Y1), stored in vids_left. */
        Right = 1 /*!< Right axis (Y2), stored in vids_right. */
    };

    /**
     * @brief Constructs an oscilloscope window, allocating its sampler data and critical section.
     * @param[in] par_Type Owning window type. Must not be nullptr.
     * @param[in] par_Name Unique window name.
     */
    OscilloscopeWindow(IWindowType *par_Type, std::string par_Name);
    ~OscilloscopeWindow() override;

    OscilloscopeWindow(const OscilloscopeWindow &) = delete;
    OscilloscopeWindow &operator=(const OscilloscopeWindow &) = delete;

    [[nodiscard]] IWindowType *GetType() const override
    {
        return m_pType;
    }
    void RenderContent(const WindowContext &par_Context) override;
    void OnUpdateTick(const WindowContext &par_Context) override;
    void RenderSettingsPopup() override;
    void WriteConfig(int par_FileDescriptor, const char *par_Section) override;
    void ReadConfig(int par_FileDescriptor, const char *par_Section) override;
    void OnAcceptVariableDrop(const std::string &par_VariableName) override;

    /**
     * @brief Freezes acquisition and pins the trace end to a timestamp (StopOscilloscope seam).
     * @param[in] par_EndTime End timestamp in simulated nanoseconds.
     */
    void StopAcquisition(std::uint64_t par_EndTime);

private:
    void InitData();
    void ReconnectSampler();
    void StartAcquisition();
    void ClearData();
    int FindFreeSlot(Axis par_Axis) const;
    int AddSignal(Axis par_Axis, const BlackboardVariablePayload &par_Payload, int par_LineSize = 1);
    void RemoveSignal(Axis par_Axis, int par_Index);
    void FreeAllSignals();
    void ApplyBufferDepth(int par_NewDepth);

    IWindowType *m_pType{nullptr};   /*!< Owning window type. */
    OSCILLOSCOPE_DATA m_data{};      /*!< Sampler state + ring buffers (shared with the scheduler). */
    bool m_registered{false};        /*!< True once RegisterOscilloscopeToCyclic succeeded. */
    bool m_acquiring{true};          /*!< True while sampling (state != 0). */
    double m_timeWindow{10.0};       /*!< Visible time span in seconds. */

    std::vector<double> m_plotX{};   /*!< Scratch X buffer reused each frame. */
    std::vector<double> m_plotY{};   /*!< Scratch Y buffer reused each frame. */

    char m_dropBuffer[512]{}; /*!< Settings "add variable" text buffer (BBVARI_NAME_SIZE). */
    int m_dropAxis{0};        /*!< Settings target axis (0 left, 1 right). */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_WIDGETS_OSCILLOSCOPE_OSCILLOSCOPEWINDOW_H
