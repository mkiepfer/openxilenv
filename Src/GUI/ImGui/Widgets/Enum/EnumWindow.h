/**
 * @file Src/GUI/ImGui/Widgets/Enum/EnumWindow.h
 * @brief ImGui port of the Qt Enum widget: enum-state buttons that write the raw value.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_WIDGETS_ENUM_ENUMWINDOW_H
#define SRC_GUI_IMGUI_WIDGETS_ENUM_ENUMWINDOW_H

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
 * @class EnumWindowType
 * @brief Window type/factory for Enum windows (mirrors the Qt EnumType, Medium update).
 */
class EnumWindowType : public IWindowType
{
public:
    [[nodiscard]] UpdateRate GetUpdateRate() const override
    {
        return UpdateRate::Medium;
    }
    [[nodiscard]] const char *GetTypeName() const override
    {
        return "Enum";
    }
    [[nodiscard]] const char *GetIniListName() const override
    {
        return "GUI/AllEnumWindows";
    }
    [[nodiscard]] const char *GetMenuEntryName() const override
    {
        return "Enum";
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
 * @class EnumWindow
 * @brief One Enum window listing one or more variables as clickable enum-state buttons.
 * @details Each variable's textreplace conversion is decoded into (label, low, high,
 *          color) entries; the active entry is highlighted and clicking one writes its
 *          low raw value. Accepts a "BBVAR" drop to add a variable. Config round-trips
 *          to the Qt "Variable%i" / "VariableCount" entries.
 */
class EnumWindow : public IWindowInstance
{
public:
    /**
     * @brief Constructs an Enum window bound to its type and name.
     * @param[in] par_Type Owning window type. Must not be nullptr.
     * @param[in] par_Name Unique window name.
     */
    EnumWindow(IWindowType *par_Type, std::string par_Name);
    ~EnumWindow() override;

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
    /**
     * @struct EnumEntry
     * @brief One decoded enum state (button) from the variable's textreplace conversion.
     */
    struct EnumEntry
    {
        std::string label{};    /*!< Enum state text. */
        double lowValue{0.0};   /*!< Inclusive lower raw bound (written on click). */
        double highValue{0.0};  /*!< Inclusive upper raw bound. */
        std::uint32_t color{0}; /*!< 0x00bbggrr enum color. */
    };

    /**
     * @struct EnumVariable
     * @brief One bound variable with its cached enum entries and current value.
     */
    struct EnumVariable
    {
        std::string name{};                /*!< Variable name. */
        std::int32_t vid{0};               /*!< Cached VID (>0 when attached). */
        std::vector<EnumEntry> entries{};  /*!< Decoded enum states. */
        double currentValue{0.0};          /*!< Cached current value. */
    };

    void AddVariable(const std::string &par_Name);
    void RemoveVariable(std::size_t par_Index);
    void RefreshEntries(EnumVariable &par_Variable);

    IWindowType *m_pType{nullptr};              /*!< Owning window type. */
    std::vector<EnumVariable> m_variables{};    /*!< Bound variables. */
    char m_addBuffer[512]{};                    /*!< Settings "add variable" text buffer. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_WIDGETS_ENUM_ENUMWINDOW_H
