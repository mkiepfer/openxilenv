/**
 * @file Src/GUI/ImGui/Widgets/Slider/SliderWindow.h
 * @brief ImGui port of the Qt Slider widget: a bound blackboard variable with write-back.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_WIDGETS_SLIDER_SLIDERWINDOW_H
#define SRC_GUI_IMGUI_WIDGETS_SLIDER_SLIDERWINDOW_H

#include <memory>
#include <string>

#include "Framework/BoundVariable.h"
#include "Framework/IWindowInstance.h"
#include "Framework/IWindowType.h"
#include "Framework/WindowContext.h"

namespace XilEnvImGui
{

/**
 * @class SliderWindowType
 * @brief Window type/factory for Slider windows (mirrors the Qt SliderType, Fast update).
 */
class SliderWindowType : public IWindowType
{
public:
    [[nodiscard]] UpdateRate GetUpdateRate() const override
    {
        return UpdateRate::Fast;
    }
    [[nodiscard]] const char *GetTypeName() const override
    {
        return "Slider";
    }
    [[nodiscard]] const char *GetIniListName() const override
    {
        return "GUI/AllSliderWindows";
    }
    [[nodiscard]] const char *GetMenuEntryName() const override
    {
        return "Slider";
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
 * @class SliderWindow
 * @brief One Slider window: an ImGui::SliderScalar bound to a blackboard variable.
 * @details Reads the current value each frame (Fast) and writes it back on user
 *          change via the BoundVariable helper. Accepts a "BBVAR" drop to (re)bind.
 *          Config round-trips to the Qt KnobOrTachoWidget "variable" entry.
 */
class SliderWindow : public IWindowInstance
{
public:
    /**
     * @brief Constructs a Slider window bound to its type and name.
     * @param[in] par_Type Owning window type. Must not be nullptr.
     * @param[in] par_Name Unique window name.
     */
    SliderWindow(IWindowType *par_Type, std::string par_Name);
    ~SliderWindow() override = default;

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
    double m_value{0.0};           /*!< Cached current value (updated each Fast tick). */
    bool m_horizontal{true};       /*!< Orientation (horizontal vs. vertical slider). */
    char m_bindBuffer[512]{};      /*!< Settings "bind variable" text buffer. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_WIDGETS_SLIDER_SLIDERWINDOW_H
