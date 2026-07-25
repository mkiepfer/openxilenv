/**
 * @file Src/GUI/ImGui/Widgets/Knob/KnobWindow.h
 * @brief ImGui port of the Qt Knob gauge: an interactive round gauge with write-back.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_WIDGETS_KNOB_KNOBWINDOW_H
#define SRC_GUI_IMGUI_WIDGETS_KNOB_KNOBWINDOW_H

#include <memory>
#include <string>

#include "Framework/BoundVariable.h"
#include "Framework/GaugeRenderer.h"
#include "Framework/IWindowInstance.h"
#include "Framework/IWindowType.h"
#include "Framework/WindowContext.h"

namespace XilEnvImGui
{

/**
 * @class KnobWindowType
 * @brief Window type/factory for Knob windows (mirrors the Qt KnobType, Fast update).
 */
class KnobWindowType : public IWindowType
{
public:
    [[nodiscard]] UpdateRate GetUpdateRate() const override
    {
        return UpdateRate::Fast;
    }
    [[nodiscard]] const char *GetTypeName() const override
    {
        return "Knob";
    }
    [[nodiscard]] const char *GetIniListName() const override
    {
        return "GUI/AllKnobWindows";
    }
    [[nodiscard]] const char *GetMenuEntryName() const override
    {
        return "Knob";
    }
    [[nodiscard]] bool HasStorage() const override
    {
        return false;
    }
    [[nodiscard]] std::unique_ptr<IWindowInstance> CreateNew(const std::string &par_Title) override;
    [[nodiscard]] std::unique_ptr<IWindowInstance> CreateFromConfig(int par_FileDescriptor,
                                                                    const char *par_Section,
                                                                    const std::string &par_Name) override;
};

/**
 * @class KnobWindow
 * @brief One Knob gauge bound to a blackboard variable, with drag-to-set write-back.
 * @details Renders a scale arc, tick marks, a needle and name/value text with ImDrawList.
 *          Dragging inside the knob maps the pointer angle to a value and writes it back
 *          through BoundVariable (write_bbvari_(phys_)minmax_check). Config round-trips to
 *          the Qt KnobOrTachoWidget section keys (variable, scale angles, flags).
 */
class KnobWindow : public IWindowInstance
{
public:
    /**
     * @brief Constructs a Knob window bound to its type and name.
     * @param[in] par_Type Owning window type. Must not be nullptr.
     * @param[in] par_Name Unique window name.
     */
    KnobWindow(IWindowType *par_Type, std::string par_Name);
    ~KnobWindow() override = default;

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

private:
    IWindowType *m_pType{nullptr}; /*!< Owning window type. */
    BoundVariable m_variable{};    /*!< Bound blackboard variable. */
    GaugeStyle m_style{};          /*!< Scale/needle/text configuration. */
    double m_value{0.0};           /*!< Cached current value (updated each Fast tick). */
    bool m_dragging{false};        /*!< True while the pointer is dragging the knob. */
    char m_bindBuffer[512]{};      /*!< Settings "bind variable" text buffer. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_WIDGETS_KNOB_KNOBWINDOW_H
