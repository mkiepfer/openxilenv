/**
 * @file Src/GUI/ImGui/Widgets/Bargraph/BargraphWindow.h
 * @brief ImGui port of the Qt Bargraph widget: a filled bar scaled by a bound variable.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_WIDGETS_BARGRAPH_BARGRAPHWINDOW_H
#define SRC_GUI_IMGUI_WIDGETS_BARGRAPH_BARGRAPHWINDOW_H

#include <memory>
#include <string>

#include "Framework/BoundVariable.h"
#include "Framework/IWindowInstance.h"
#include "Framework/IWindowType.h"
#include "Framework/WindowContext.h"

namespace XilEnvImGui
{

/**
 * @class BargraphWindowType
 * @brief Window type/factory for Bargraph windows (mirrors the Qt BargraphType, Fast update).
 */
class BargraphWindowType : public IWindowType
{
public:
    [[nodiscard]] UpdateRate GetUpdateRate() const override
    {
        return UpdateRate::Fast;
    }
    [[nodiscard]] const char *GetTypeName() const override
    {
        return "Bargraph";
    }
    [[nodiscard]] const char *GetIniListName() const override
    {
        return "GUI/AllBargraphWindows";
    }
    [[nodiscard]] const char *GetMenuEntryName() const override
    {
        return "Bargraph";
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
 * @class BargraphWindow
 * @brief One Bargraph window: a filled bar drawn with ImDrawList, scaled min..max.
 * @details Display-only. Reads the current value each frame (Fast) via BoundVariable.
 *          Accepts a "BBVAR" drop to (re)bind. Config round-trips to the Qt
 *          KnobOrTachoWidget "variable" entry.
 */
class BargraphWindow : public IWindowInstance
{
public:
    /**
     * @brief Constructs a Bargraph window bound to its type and name.
     * @param[in] par_Type Owning window type. Must not be nullptr.
     * @param[in] par_Name Unique window name.
     */
    BargraphWindow(IWindowType *par_Type, std::string par_Name);
    ~BargraphWindow() override = default;

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
    bool m_vertical{true};         /*!< Fill direction (vertical bottom-up vs. horizontal). */
    char m_bindBuffer[512]{};      /*!< Settings "bind variable" text buffer. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_WIDGETS_BARGRAPH_BARGRAPHWINDOW_H
