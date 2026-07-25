/**
 * @file Src/GUI/ImGui/Widgets/Text/TextWindow.h
 * @brief ImGui port of the Qt Text widget: a table of blackboard name/value/unit rows.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_WIDGETS_TEXT_TEXTWINDOW_H
#define SRC_GUI_IMGUI_WIDGETS_TEXT_TEXTWINDOW_H

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
 * @enum class TextDisplayType
 * @brief Per-row value formatting, matching the Qt Text widget's "E<i>" type field.
 */
enum class TextDisplayType : std::int32_t
{
    Decimal = 0, /*!< Raw value in base 10. */
    Hex = 1,     /*!< Raw value in base 16 (0x...). */
    Binary = 2,  /*!< Raw value in base 2 (0b...). */
    Physical = 3 /*!< Converted physical value / text replacement. */
};

/**
 * @class TextWindowType
 * @brief Window type/factory for Text windows (mirrors the Qt TextType).
 */
class TextWindowType : public IWindowType
{
public:
    [[nodiscard]] UpdateRate GetUpdateRate() const override
    {
        return UpdateRate::Medium;
    }
    [[nodiscard]] const char *GetTypeName() const override
    {
        return "Text";
    }
    [[nodiscard]] const char *GetIniListName() const override
    {
        return "GUI/AllTextWindows";
    }
    [[nodiscard]] const char *GetMenuEntryName() const override
    {
        return "Text";
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
 * @class TextWindow
 * @brief One Text window instance showing a live table of blackboard variables.
 * @details Reads each shown variable through a cached VID (attached with
 *          add_bbvari(BB_UNKNOWN_WAIT)) and formats it per its TextDisplayType.
 *          Config round-trips to the Qt "GUI/Widgets/<name>" section layout.
 */
class TextWindow : public IWindowInstance
{
public:
    /**
     * @brief Constructs a Text window bound to its type and name.
     * @param[in] par_Type Owning window type. Must not be nullptr.
     * @param[in] par_Name Unique window name.
     */
    TextWindow(IWindowType *par_Type, std::string par_Name);
    ~TextWindow() override;

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
     * @struct Variable
     * @brief One displayed blackboard variable with its cached VID and formatted value.
     */
    struct Variable
    {
        std::string name{};                            /*!< Blackboard variable name. */
        std::int32_t vid{0};                           /*!< Cached VID (>0 when attached). */
        TextDisplayType displayType{TextDisplayType::Decimal}; /*!< Value format. */
        std::int32_t precision{0};                     /*!< Cached display precision (get_bbvari_format_prec). */
        std::string valueText{"---"};                  /*!< Cached formatted value. */
        std::string unit{};                            /*!< Cached unit string. */
        bool exists{false};                            /*!< Whether the variable is currently defined. */
    };

    void AttachVariable(Variable &par_Variable);
    void DetachVariable(Variable &par_Variable);
    void AddVariable(const std::string &par_Name, TextDisplayType par_DisplayType, bool par_AutoDetectDisplayType);
    TextDisplayType DetectDisplayType(std::int32_t par_Vid) const;
    void RefreshVariable(Variable &par_Variable);

    IWindowType *m_pType{nullptr};      /*!< Owning window type. */
    std::vector<Variable> m_variables{}; /*!< Displayed variables in row order. */
    bool m_showUnitColumn{true};        /*!< Show the Unit column. */
    bool m_showDisplayTypeColumn{false};/*!< Show the display-type column. */
    std::uint32_t m_backgroundColor{0xFFFFFFu}; /*!< 0x00bbggrr background (persisted). */
    std::string m_fontString{};         /*!< Persisted "family, size" (not applied in M3). */
    std::int32_t m_icon{0};             /*!< Persisted icon id (unused in M3). */
    std::int32_t m_columnAlignmentName{0};        /*!< Persisted name-column alignment. */
    std::int32_t m_columnAlignmentValue{0};       /*!< Persisted value-column alignment. */
    std::int32_t m_columnAlignmentUnit{0};        /*!< Persisted unit-column alignment. */
    std::int32_t m_columnAlignmentDisplayType{0}; /*!< Persisted display-type-column alignment. */
    char m_addVariableBuffer[512]{};    /*!< Text buffer for the "add variable" input. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_WIDGETS_TEXT_TEXTWINDOW_H
