/**
 * @file Src/GUI/ImGui/Widgets/UserDraw/UserDrawElement.h
 * @brief Scene-graph base element, equation-backed BaseValue/ColorValue and the
 *        UserDraw coordinate transform for the ImGui UserDraw port.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_WIDGETS_USERDRAW_USERDRAWELEMENT_H
#define SRC_GUI_IMGUI_WIDGETS_USERDRAW_USERDRAWELEMENT_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "imgui.h"

struct EXEC_STACK_ELEM;

namespace XilEnvImGui
{

/**
 * @brief Case-insensitive ASCII string equality (mirrors Qt::CaseInsensitive compares).
 * @param[in] par_Value Left operand.
 * @param[in] par_Literal Right operand (ASCII literal). Must not be nullptr.
 * @return true if the strings are equal ignoring ASCII case.
 */
[[nodiscard]] bool IEqualsAscii(const std::string &par_Value, const char *par_Literal);

/**
 * @class BaseValue
 * @brief A scalar drawing property that is either a fixed number or a blackboard equation.
 * @details Mirrors the Qt BaseValue. A literal is stored as a fixed double; anything else
 *          is compiled once (at parse time) into a core exec-stack via
 *          solve_equation_err_string() and re-evaluated every frame with execute_stack().
 *          The exec stack is released with remove_exec_stack() on destruction. Move-only:
 *          exclusive ownership of the exec stack avoids the Qt attach-counter reference
 *          sharing.
 */
class BaseValue
{
public:
    BaseValue() = default;
    ~BaseValue();

    BaseValue(const BaseValue &) = delete;
    BaseValue &operator=(const BaseValue &) = delete;
    BaseValue(BaseValue &&par_Other) noexcept;
    BaseValue &operator=(BaseValue &&par_Other) noexcept;

    /**
     * @brief Parses a value string into a fixed number or a compiled equation.
     * @param[in] par_Value Literal (e.g. "0.5") or equation over blackboard variables.
     * @param[in] par_OneTimeCalc When true the equation is solved once to a fixed value.
     * @return 0 on success, -1 on a parse/compile error (value falls back to 0.0).
     */
    int Parse(const std::string &par_Value, bool par_OneTimeCalc = false);

    /**
     * @brief Sets the property to a constant value, releasing any compiled equation.
     * @param[in] par_Value The fixed value.
     */
    void SetFixed(double par_Value);

    /**
     * @brief Evaluates the property against the current blackboard state.
     * @return The fixed value or the freshly executed equation result.
     */
    [[nodiscard]] double Get() const;

private:
    void Release();

    bool m_isFixedValue{true};             /*!< true: use m_fixed, false: use m_pExecStack. */
    double m_fixed{0.0};                   /*!< Constant value when fixed. */
    struct EXEC_STACK_ELEM *m_pExecStack{nullptr}; /*!< Compiled equation (owned). */
};

/**
 * @class ColorValue
 * @brief An RGB color whose three channels are each an equation-backed BaseValue.
 * @details Mirrors the Qt ColorValue; each channel is clamped to [0,255].
 */
class ColorValue
{
public:
    ColorValue() = default;

    /**
     * @brief Parses a "red:green:blue" triple (each part a literal or equation).
     * @param[in] par_Value Color definition, e.g. "0xAB:0x12:0x1A".
     * @return 0 on success, -1 on malformed input.
     */
    int Parse(const std::string &par_Value);

    /**
     * @brief Sets all three channels to fixed values.
     * @param[in] par_Red Red channel value.
     * @param[in] par_Green Green channel value.
     * @param[in] par_Blue Blue channel value.
     */
    void SetFixed(double par_Red, double par_Green, double par_Blue);

    /**
     * @brief Evaluates the channels and packs them into an ImGui color.
     * @param[in] par_Alpha Alpha channel to apply (default opaque).
     * @return Packed IM_COL32 color.
     */
    [[nodiscard]] ImU32 GetColor(std::uint8_t par_Alpha = 255u) const;

private:
    BaseValue m_red{};   /*!< Red channel. */
    BaseValue m_green{}; /*!< Green channel. */
    BaseValue m_blue{};  /*!< Blue channel. */
};

/**
 * @struct DrawParameter
 * @brief Accumulated parent transform passed down the scene graph each frame.
 * @details Faithful port of the Qt DrawParameter. m_XScale/m_YScale carry the widget
 *          pixel size for the final logical->screen mapping.
 */
struct DrawParameter
{
    DrawParameter(double par_ParentX, double par_ParentY, double par_ParentScale, double par_ParentRot,
                  double par_XScale, double par_YScale);

    double m_parentX{0.0};      /*!< Accumulated parent origin X (logical). */
    double m_parentY{0.0};      /*!< Accumulated parent origin Y (logical). */
    double m_parentScale{1.0};  /*!< Parent scale factor. */
    double m_parentRot{0.0};    /*!< Parent rotation (rad). */
    double m_parentRotSin{0.0}; /*!< sin(parentRot) cache. */
    double m_parentRotCos{1.0}; /*!< cos(parentRot) cache. */
    double m_xScale{1.0};       /*!< Widget width in pixels. */
    double m_yScale{1.0};       /*!< Widget height in pixels. */
};

/**
 * @struct TranslateParameter
 * @brief One element's own transform combined with its parent DrawParameter.
 * @details Faithful port of the Qt TranslateParameter.
 */
struct TranslateParameter
{
    TranslateParameter(double par_PosX, double par_PosY, double par_Scale, double par_Rot,
                       const DrawParameter &par_Draw);

    double m_posX{0.0};       /*!< Element X position (logical). */
    double m_posY{0.0};       /*!< Element Y position (logical). */
    double m_scale{1.0};      /*!< Element scale. */
    double m_sin{0.0};        /*!< sin(rot) cache. */
    double m_cos{1.0};        /*!< cos(rot) cache. */
    DrawParameter m_draw;     /*!< Parent transform. */
};

/**
 * @enum class TranslateType
 * @brief Bit flags selecting which transform stages Translate() applies.
 */
enum class TranslateType : std::uint8_t
{
    None = 0x0u,            /*!< No transform. */
    OnlyItself = 0x1u,      /*!< Apply the element's own pos/scale/rot. */
    OnlyParent = 0x2u,      /*!< Apply the accumulated parent transform. */
    ParentAndItself = 0x3u, /*!< Apply both, no screen mapping. */
    OnlyToScreen = 0x4u,    /*!< Map logical to widget pixels (Y flipped). */
    AllToScreen = 0x7u      /*!< Itself + parent + screen mapping. */
};

/**
 * @brief Transforms a logical point through the element/parent/screen stages.
 * @param[in,out] par_pX Point X (in/out).
 * @param[in,out] par_pY Point Y (in/out).
 * @param[in] par_Parameter Combined element+parent transform.
 * @param[in] par_Type Which transform stages to apply.
 */
void Translate(double *par_pX, double *par_pY, const TranslateParameter &par_Parameter,
               TranslateType par_Type = TranslateType::AllToScreen);

/**
 * @class UserDrawElement
 * @brief Abstract scene-graph node: base geometric/color/visibility properties + rendering.
 * @details Every element owns the shared BaseValue properties (visible, color, thickness,
 *          fill, x, y, scale, rot) and keeps the raw parameter line for verbatim config
 *          round-trip. Concrete elements implement Render() (draws via ImDrawList using the
 *          per-frame-evaluated properties) and ParseOneParameter().
 */
class UserDrawElement
{
public:
    virtual ~UserDrawElement() = default;

    /**
     * @brief Draws this element (and children) using freshly evaluated properties.
     * @param[in] par_pDrawList Target ImGui draw list. Must not be nullptr.
     * @param[in] par_Draw Accumulated parent transform.
     * @param[in] par_Origin Content-rect top-left in screen space.
     */
    virtual void Render(ImDrawList *par_pDrawList, const DrawParameter &par_Draw,
                        const ImVec2 &par_Origin) = 0;

    /**
     * @brief Parses one element-specific "key=value" parameter.
     * @param[in] par_Value The parameter token.
     * @return true if the parameter was recognized.
     */
    virtual bool ParseOneParameter(const std::string &par_Value) = 0;

    /**
     * @brief Persisted type keyword (e.g. "Line", "Group").
     * @return Type keyword. Never nullptr.
     */
    [[nodiscard]] virtual const char *GetTypeString() const = 0;

    /**
     * @brief Whether this element is a container (Root/Layer/Group).
     * @return true for group-like elements.
     */
    [[nodiscard]] virtual bool IsGroup() const
    {
        return false;
    }

    /**
     * @brief Appends a child (group-like elements only).
     * @param[in] par_Child Child to take ownership of.
     */
    virtual void AddChild(std::unique_ptr<UserDrawElement> par_Child)
    {
        (void)par_Child;
    }

    /**
     * @brief Number of direct children.
     * @return Child count (0 for leaf elements).
     */
    [[nodiscard]] virtual int GetChildCount() const
    {
        return 0;
    }

    /**
     * @brief Returns a child by index.
     * @param[in] par_Index Child index.
     * @return Non-owning child pointer or nullptr.
     */
    [[nodiscard]] virtual UserDrawElement *GetChild(int par_Index) const
    {
        (void)par_Index;
        return nullptr;
    }

    /**
     * @brief Resets all base properties to their defaults.
     */
    virtual void ResetToDefault();

    /**
     * @brief Writes this element and its subtree to the ini section (verbatim round-trip).
     * @param[in] par_Section Window section (e.g. "GUI/Widgets/<name>"). Must not be nullptr.
     * @param[in] par_Entry Entry name for this element (e.g. "x_0").
     * @param[in] par_FileDescriptor Ini-database descriptor.
     */
    void WriteToIni(const char *par_Section, const std::string &par_Entry, int par_FileDescriptor);

    /**
     * @brief Sets the raw parameter line (the "(...)" tail) used for config round-trip.
     * @param[in] par_Line Raw parameter line.
     */
    void SetParameterLine(std::string par_Line)
    {
        m_parameterLine = std::move(par_Line);
    }

protected:
    /**
     * @brief Parses one of the shared base properties (x, y, scale, rot, visible, ...).
     * @param[in] par_Key Lower-/mixed-case key.
     * @param[in] par_Value Value string.
     * @return true if the key matched a base property.
     */
    bool ParseOneBaseParameter(const std::string &par_Key, const std::string &par_Value);

    [[nodiscard]] ImU32 GetLineColor() const;
    [[nodiscard]] float GetLineThickness() const;
    [[nodiscard]] ImU32 GetFillColor() const;

    std::string m_parameterLine{}; /*!< Raw "(...)" parameter tail for round-trip. */

    BaseValue m_visible{};        /*!< Visibility (>= 0.5 means visible). */
    ColorValue m_lineColor{};     /*!< Outline color. */
    BaseValue m_lineThickness{};  /*!< Outline thickness (pixels). */
    bool m_fillColorIsSet{false}; /*!< Whether a fill color was configured. */
    ColorValue m_fillColor{};     /*!< Fill color. */
    BaseValue m_x{};              /*!< Element X position (logical). */
    BaseValue m_y{};              /*!< Element Y position (logical). */
    BaseValue m_scale{};          /*!< Element scale. */
    BaseValue m_rot{};            /*!< Element rotation (rad). */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_WIDGETS_USERDRAW_USERDRAWELEMENT_H
