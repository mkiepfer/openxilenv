/**
 * @file Src/GUI/ImGui/Framework/ConversionDialogs.h
 * @brief Signal-conversion editor dialogs (dispatcher plus the type-specific sub-editors).
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_FRAMEWORK_CONVERSIONDIALOGS_H
#define SRC_GUI_IMGUI_FRAMEWORK_CONVERSIONDIALOGS_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace XilEnvImGui
{

/**
 * @class ConversionDialogManager
 * @brief Owns and renders the blackboard-variable conversion editor dialogs.
 * @details Process-wide singleton mirroring the Qt conversion editing found in BlackboardInfoDialog:
 *          a dispatcher that picks the conversion type (none / formula / text replace /
 *          factor+offset / offset+factor / table with/without interpolation / rational function) and
 *          opens the matching type-specific sub-editor, then applies the result to the variable through
 *          @ref set_bbvari_conversion. The dispatcher reads the current conversion via
 *          @ref get_bbvari_conversiontype / @ref get_bbvari_conversion so the produced conversion string
 *          round-trips byte-identically with the C core. All methods run on the render thread.
 * @warning Render-thread-only; not thread-safe.
 */
class ConversionDialogManager
{
public:
    /**
     * @brief Returns the process-wide singleton instance.
     * @return Reference to the single ConversionDialogManager.
     */
    static ConversionDialogManager &GetInstance();

    /**
     * @brief Loads a variable's current conversion and opens the dispatcher dialog for it.
     * @details Entry point for other code (blackboard-info dialog, widget context menu). If the name
     *          does not resolve to a blackboard variable the call is ignored.
     * @param[in] par_VariableName Blackboard variable name. Must not be nullptr.
     */
    void OpenForVariable(const char *par_VariableName);

    /**
     * @brief Opens the small variable-picker prompt that then opens the dispatcher.
     * @details Reachable from the menu bar so conversion editing is usable without a source variable.
     */
    void OpenPicker();

    /**
     * @brief Renders the picker, dispatcher and any active sub-editor for this frame.
     */
    void Render();

private:
    ConversionDialogManager() = default;

    /**
     * @struct TablePair
     * @brief One phys/raw pair of a table conversion (mirrors CONVERSION_TABLE_VALUE_PAIR).
     */
    struct TablePair
    {
        double phys{0.0}; /*!< Physical (converted) value. */
        double raw{0.0};  /*!< Raw (blackboard) value. */
    };

    /**
     * @struct TextReplaceEntry
     * @brief One enum text-replace row (from..to -> text, optional color).
     */
    struct TextReplaceEntry
    {
        std::array<char, 32> from{};   /*!< Lower bound as text ("*" for INT64_MIN). */
        std::array<char, 32> to{};     /*!< Upper bound as text ("*" for INT64_MAX). */
        std::array<char, 1024> text{}; /*!< Replacement text. */
        int color{-1};                 /*!< 0x00BBGGRR color, or -1 for no color. */
    };

    void ReadConversionFromVariable();
    void ApplyConversionToVariable();

    void RenderPicker();
    void RenderDispatcher();
    void RenderFactorOffsetEditor();
    void RenderRationalFunctionEditor();
    void RenderTableEditor();
    void RenderTextReplaceEditor();

    void OpenSubEditor();
    void ParseFactorOffset();
    void ParseRationalFunction();
    void ParseTable();
    void ParseTextReplace();
    std::string EncodeFactorOffset() const;
    std::string EncodeRationalFunction() const;
    std::string EncodeTable() const;
    std::string EncodeTextReplace() const;
    bool SyntaxCheckTextReplace(const std::string &par_String, std::string &out_Error) const;

    void SetRawBuffer(const std::string &par_Value);

    // -- Picker state -------------------------------------------------------
    std::array<char, 512> m_pickerName{};        /*!< Variable name typed in the picker. */
    bool m_pickerOpen{false};                    /*!< Picker modal is shown. */
    bool m_pickerOpenRequested{false};           /*!< OpenPopup pending for the picker. */

    // -- Dispatcher state ---------------------------------------------------
    std::string m_variableName{};                /*!< Variable being edited. */
    int m_conversionType{0};                     /*!< BB_CONV_* type of the edited conversion. */
    std::vector<char> m_rawBuffer{};             /*!< Editable raw conversion string buffer. */
    std::string m_error{};                       /*!< Validation error shown on the dispatcher. */
    bool m_dispatcherOpen{false};                /*!< Dispatcher modal is shown. */
    bool m_dispatcherOpenRequested{false};       /*!< OpenPopup pending for the dispatcher. */

    // -- Sub-editor shared --------------------------------------------------
    int m_subEditorType{-1};                     /*!< BB_CONV_* the active sub-editor edits, or -1. */
    bool m_subEditorOpenRequested{false};        /*!< OpenPopup pending for the active sub-editor. */

    // -- Factor/offset + rational-function editor state ---------------------
    std::array<double, 6> m_coefficients{};      /*!< a..f (factor/offset use [0],[1]). */

    // -- Table editor state -------------------------------------------------
    std::vector<TablePair> m_tablePairs{};       /*!< Table conversion rows. */

    // -- Text-replace editor state ------------------------------------------
    std::vector<TextReplaceEntry> m_textReplaceEntries{}; /*!< Enum text-replace rows. */
    std::string m_textReplaceError{};            /*!< Syntax-check message in the editor. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_FRAMEWORK_CONVERSIONDIALOGS_H
