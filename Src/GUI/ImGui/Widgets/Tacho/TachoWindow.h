/**
 * @file Src/GUI/ImGui/Widgets/Tacho/TachoWindow.h
 * @brief ImGui port of the Qt Tacho gauge: a display-only round gauge.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_WIDGETS_TACHO_TACHOWINDOW_H
#define SRC_GUI_IMGUI_WIDGETS_TACHO_TACHOWINDOW_H

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
 * @class TachoWindowType
 * @brief Window type/factory for Tacho windows (mirrors the Qt TachoType, Fast update).
 */
class TachoWindowType : public IWindowType
{
public:
    [[nodiscard]] UpdateRate GetUpdateRate() const override
    {
        return UpdateRate::Fast;
    }
    [[nodiscard]] const char *GetTypeName() const override
    {
        return "Tacho";
    }
    [[nodiscard]] const char *GetIniListName() const override
    {
        return "GUI/AllTachometerWindows";
    }
    [[nodiscard]] const char *GetMenuEntryName() const override
    {
        return "Tacho";
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
 * @class TachoWindow
 * @brief One Tacho gauge bound to a blackboard variable (display-only).
 * @details Renders a scale arc, tick marks, a needle and name/value text via the shared
 *          GaugeRenderer - identical to the Knob but without pointer write-back. Config
 *          round-trips to the Qt KnobOrTachoWidget section keys.
 */
class TachoWindow : public IWindowInstance
{
public:
    /**
     * @brief Constructs a Tacho window bound to its type and name.
     * @param[in] par_Type Owning window type. Must not be nullptr.
     * @param[in] par_Name Unique window name.
     */
    TachoWindow(IWindowType *par_Type, std::string par_Name);
    ~TachoWindow() override = default;

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
    char m_bindBuffer[512]{};      /*!< Settings "bind variable" text buffer. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_WIDGETS_TACHO_TACHOWINDOW_H
