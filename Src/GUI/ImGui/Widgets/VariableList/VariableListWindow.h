/**
 * @file Src/GUI/ImGui/Widgets/VariableList/VariableListWindow.h
 * @brief Blackboard-variable list window acting as the drag&drop source for all widgets.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_WIDGETS_VARIABLELIST_VARIABLELISTWINDOW_H
#define SRC_GUI_IMGUI_WIDGETS_VARIABLELIST_VARIABLELISTWINDOW_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Framework/IWindowInstance.h"
#include "Framework/IWindowType.h"
#include "Framework/WindowContext.h"

namespace XilEnvImGui
{

/**
 * @class VariableListWindowType
 * @brief Window type/factory for the blackboard-variable list (the drag source).
 * @details Has no Qt widget counterpart (Qt exposes the list only as a dialog); it
 *          completes drag&drop end-to-end by publishing rich "BBVAR" payloads.
 */
class VariableListWindowType : public IWindowType
{
public:
    [[nodiscard]] UpdateRate GetUpdateRate() const override
    {
        return UpdateRate::Slow;
    }
    [[nodiscard]] const char *GetTypeName() const override
    {
        return "BlackboardVariableList";
    }
    [[nodiscard]] const char *GetIniListName() const override
    {
        return "GUI/AllBlackboardVariableListWindows";
    }
    [[nodiscard]] const char *GetMenuEntryName() const override
    {
        return "Blackboard Variables";
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
 * @class VariableListWindow
 * @brief Lists all blackboard variables with a filter; every row is a drag source.
 * @details The cached name list is refreshed at the Slow rate. Each visible row
 *          publishes a BlackboardVariablePayload carrying name + min/max + color, so
 *          richer targets (Slider/Bargraph/Text) receive full metadata.
 */
class VariableListWindow : public IWindowInstance
{
public:
    /**
     * @brief Constructs the variable-list window.
     * @param[in] par_Type Owning window type. Must not be nullptr.
     * @param[in] par_Name Unique window name.
     */
    VariableListWindow(IWindowType *par_Type, std::string par_Name);
    ~VariableListWindow() override = default;

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
    IWindowType *m_pType{nullptr};       /*!< Owning window type. */
    std::vector<std::string> m_names{};  /*!< Cached blackboard variable names. */
    char m_filterBuffer[256]{};          /*!< Persisted typeahead filter. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_WIDGETS_VARIABLELIST_VARIABLELISTWINDOW_H
