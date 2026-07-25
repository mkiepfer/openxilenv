/**
 * @file Src/GUI/ImGui/Framework/BlackboardDragDrop.h
 * @brief Rich blackboard-variable drag&drop payload (Qt DragAndDropInfos-compatible "BBVAR").
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_FRAMEWORK_BLACKBOARDDRAGDROP_H
#define SRC_GUI_IMGUI_FRAMEWORK_BLACKBOARDDRAGDROP_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace XilEnvImGui
{

/**
 * @brief ImGui payload type identifier for a dragged blackboard variable.
 * @details Matches the string used by the Text/M3 drop target so old (name-only)
 *          and new (rich) sources interoperate.
 */
constexpr const char *BBVAR_PAYLOAD_TYPE{"BBVAR"};

/**
 * @enum class DragDropDisplayMode
 * @brief Value-format hint carried with a dragged variable (mirrors Qt DragAndDropDisplayMode).
 */
enum class DragDropDisplayMode : std::int32_t
{
    Unknown = 0, /*!< No display mode specified. */
    Decimal = 1, /*!< Raw decimal. */
    Physical = 2,/*!< Physical (converted) value. */
    Hex = 3,     /*!< Raw hexadecimal. */
    Binary = 4   /*!< Raw binary. */
};

/**
 * @struct BlackboardVariablePayload
 * @brief Metadata carried when a blackboard variable is dragged between windows.
 * @details Serialized to the Qt "key=value;" info string so richer targets
 *          (Slider/Bargraph/Oscilloscope) get min/max/color/display-mode, while
 *          name-only targets keep working.
 */
struct BlackboardVariablePayload
{
    std::string name{};                                       /*!< Blackboard variable name. */
    bool hasMinMax{false};                                    /*!< True if min/max were provided. */
    double minValue{0.0};                                     /*!< Lower bound (when hasMinMax). */
    double maxValue{1.0};                                     /*!< Upper bound (when hasMinMax). */
    bool hasColor{false};                                     /*!< True if a color was provided. */
    std::int32_t color{0};                                    /*!< 0x00bbggrr color (when hasColor). */
    DragDropDisplayMode displayMode{DragDropDisplayMode::Unknown}; /*!< Value-format hint. */
};

/**
 * @brief Serializes a payload into the Qt DragAndDropInfos "key=value;" info string.
 * @param[in] par_Payload Payload to serialize.
 * @return Info string containing name and any provided metadata.
 */
[[nodiscard]] std::string SerializeBlackboardVariablePayload(const BlackboardVariablePayload &par_Payload);

/**
 * @brief Parses a "BBVAR" payload, accepting both the rich info string and a bare name.
 * @param[in] par_Data Payload bytes from ImGui (may be NUL-terminated).
 * @param[in] par_Size Payload size in bytes.
 * @param[out] out_Payload Parsed result. On a bare-name payload only name is filled.
 * @return true if a non-empty name was extracted, false otherwise.
 */
[[nodiscard]] bool ParseBlackboardVariablePayload(const void *par_Data, std::size_t par_Size,
                                                  BlackboardVariablePayload &out_Payload);

/**
 * @brief Builds a payload for a variable, filling min/max/color from the blackboard.
 * @param[in] par_Name Variable name. Must not be empty.
 * @param[in] par_Vid Cached VID (>0). When <=0 only the name is filled.
 * @param[in] par_DisplayMode Display-mode hint to carry.
 * @return A populated payload.
 */
[[nodiscard]] BlackboardVariablePayload MakeBlackboardVariablePayloadFromVid(
    const std::string &par_Name, std::int32_t par_Vid, DragDropDisplayMode par_DisplayMode);

/**
 * @brief Sets the ImGui drag payload for a variable (call between Begin/EndDragDropSource).
 * @param[in] par_Payload Payload to attach to the current drag source.
 */
void SetBlackboardVariableDragPayload(const BlackboardVariablePayload &par_Payload);

/**
 * @brief Accepts a "BBVAR" drop on the current target and parses it.
 * @param[out] out_Payload Receives the parsed payload on success.
 * @return true if a payload was accepted this frame, false otherwise.
 * @note Must be called between ImGui::BeginDragDropTarget and EndDragDropTarget.
 */
[[nodiscard]] bool AcceptBlackboardVariableDrop(BlackboardVariablePayload &out_Payload);

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_FRAMEWORK_BLACKBOARDDRAGDROP_H
