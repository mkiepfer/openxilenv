/**
 * @file Src/GUI/ImGui/Framework/GuiComponents.h
 * @brief Reusable immediate-mode building blocks (variable selector, phys value input, forms).
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_FRAMEWORK_GUICOMPONENTS_H
#define SRC_GUI_IMGUI_FRAMEWORK_GUICOMPONENTS_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace XilEnvImGui
{

/**
 * @brief Enumerates all currently defined blackboard variable names.
 * @param[out] out_Names Receives the variable names (cleared first).
 */
void EnumerateBlackboardVariables(std::vector<std::string> &out_Names);

/**
 * @brief Draws a blackboard-variable picker: an editable field plus a searchable popup.
 * @details The popup lists all blackboard variables filtered by a typeahead string.
 *          Picking one copies its name into the buffer and returns true.
 * @param[in] par_Label Widget id/label (use "##..." to hide the visible label).
 * @param[in,out] par_Buffer Text buffer holding the selected/edited name. Must not be nullptr.
 * @param[in] par_BufferSize Size of par_Buffer in bytes. Must be > 0.
 * @return true if the name changed (typed or picked) this frame, false otherwise.
 */
bool DrawBlackboardSelector(const char *par_Label, char *par_Buffer, std::size_t par_BufferSize);

/**
 * @brief Draws a numeric physical-value entry clamped to a range.
 * @param[in] par_Label Widget id/label.
 * @param[in,out] par_Value Value to edit; clamped into [min,max] on change. Must not be nullptr.
 * @param[in] par_MinValue Lower bound.
 * @param[in] par_MaxValue Upper bound.
 * @param[in] par_Precision Display precision (decimal places, clamped to [0,15]).
 * @return true if the value changed this frame, false otherwise.
 */
bool DrawPhysValueInput(const char *par_Label, double *par_Value, double par_MinValue, double par_MaxValue,
                        int par_Precision);

/**
 * @enum class FormFieldKind
 * @brief Value kind of a data-driven form field.
 */
enum class FormFieldKind : std::uint8_t
{
    Int,    /*!< Integer field bound to pIntValue. */
    Double, /*!< Double field bound to pDoubleValue (clamped to [minValue,maxValue]). */
    Text,   /*!< Text field bound to pTextBuffer/textBufferSize. */
    Bool,   /*!< Checkbox bound to pBoolValue. */
    Enum    /*!< Combo bound to pIntValue with enumLabels. */
};

/**
 * @struct FormField
 * @brief One descriptor row rendered by RenderForm (keeps settings popups DRY).
 */
struct FormField
{
    std::string label{};                 /*!< Visible label. */
    FormFieldKind kind{FormFieldKind::Int}; /*!< Value kind. */
    int *pIntValue{nullptr};             /*!< Bound int (Int/Enum). */
    double *pDoubleValue{nullptr};       /*!< Bound double (Double). */
    bool *pBoolValue{nullptr};           /*!< Bound bool (Bool). */
    char *pTextBuffer{nullptr};          /*!< Bound text buffer (Text). */
    std::size_t textBufferSize{0};       /*!< Size of pTextBuffer. */
    double minValue{0.0};                /*!< Lower bound (Double). */
    double maxValue{0.0};                /*!< Upper bound (Double); if <=min, unbounded. */
    int precision{3};                    /*!< Display precision (Double). */
    std::vector<std::string> enumLabels{}; /*!< Combo entries (Enum). */
    std::string tooltip{};               /*!< Optional hover tooltip. */
};

/**
 * @brief Renders a list of form fields as a two-column label/editor layout.
 * @param[in,out] par_Fields Fields to render. Bound pointers must stay valid during the call.
 * @return true if any field changed this frame, false otherwise.
 */
bool RenderForm(std::vector<FormField> &par_Fields);

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_FRAMEWORK_GUICOMPONENTS_H
