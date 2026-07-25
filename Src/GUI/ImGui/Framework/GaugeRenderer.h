/**
 * @file Src/GUI/ImGui/Framework/GaugeRenderer.h
 * @brief Shared round-gauge painter + value<->angle math for the Knob and Tacho widgets.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_FRAMEWORK_GAUGERENDERER_H
#define SRC_GUI_IMGUI_FRAMEWORK_GAUGERENDERER_H

#include <cstdint>

#include "imgui.h"

namespace XilEnvImGui
{

/**
 * @struct GaugeStyle
 * @brief Scale/needle/text configuration of a round gauge (Qt KnobOrTachoWidget compatible).
 * @details Angles are measured in degrees, clockwise from the 12-o'clock (top) position, so
 *          the defaults (-160..160) sweep from bottom-left up over the top to bottom-right,
 *          leaving a 40 degree dead sector at the bottom - exactly like the Qt gauge.
 */
struct GaugeStyle
{
    double minAngle{-160.0}; /*!< Angle at the lower bound (deg, clockwise from top). */
    double maxAngle{160.0};  /*!< Angle at the upper bound (deg, clockwise from top). */
    bool scaleFlag{true};    /*!< Draw the scale arc, tick marks and labels. */
    bool scaleStepFlag{false}; /*!< Use a fixed major-tick step (scaleStep) instead of auto. */
    double scaleStep{30.0};  /*!< Major-tick step in value units (when scaleStepFlag). */
    bool needleFlag{true};   /*!< Draw the needle. */
    bool textFlag{true};     /*!< Draw the value text. */
    bool nameFlag{true};     /*!< Draw the variable-name text. */
    bool drawHub{true};      /*!< Draw the central hub circle (knob look). */
};

/**
 * @brief Maps a value to its gauge angle.
 * @param[in] par_Value Current value.
 * @param[in] par_Min Lower bound.
 * @param[in] par_Max Upper bound (must be > min for a meaningful result).
 * @param[in] par_Style Gauge angle configuration.
 * @return Angle in degrees, clockwise from top, clamped into [minAngle, maxAngle].
 */
[[nodiscard]] double GaugeValueToAngle(double par_Value, double par_Min, double par_Max,
                                       const GaugeStyle &par_Style);

/**
 * @brief Converts a mouse offset from the gauge center into a gauge angle.
 * @param[in] par_Dx Horizontal offset (mouse.x - center.x).
 * @param[in] par_Dy Vertical offset in screen coordinates, y down (mouse.y - center.y).
 * @return Angle in degrees, clockwise from top, in the range (-180, 180].
 * @note Straight up yields 0, right yields 90, down yields 180, left yields -90.
 */
[[nodiscard]] double GaugeMouseToAngle(float par_Dx, float par_Dy);

/**
 * @brief Maps a gauge angle back to a value, clamped to the nearest scale end.
 * @param[in] par_AngleDeg Angle in degrees, clockwise from top (as from GaugeMouseToAngle).
 * @param[in] par_Min Lower bound.
 * @param[in] par_Max Upper bound.
 * @param[in] par_Style Gauge angle configuration.
 * @return Value in [min, max]; angles inside the bottom dead sector snap to the nearer end.
 */
[[nodiscard]] double GaugeAngleToValue(double par_AngleDeg, double par_Min, double par_Max,
                                       const GaugeStyle &par_Style);

/**
 * @struct GaugeColors
 * @brief Resolved colors used when painting a gauge (already ImGui-packed).
 */
struct GaugeColors
{
    ImU32 scale{0};  /*!< Scale arc / tick / label color. */
    ImU32 needle{0}; /*!< Needle fill color. */
    ImU32 text{0};   /*!< Name/value text color. */
};

/**
 * @brief Paints a full round gauge (arc, ticks, labels, needle, hub, name/value text).
 * @param[in,out] par_pDrawList Target draw list. Must not be nullptr.
 * @param[in] par_Center Gauge center in screen coordinates.
 * @param[in] par_Radius Scale radius in pixels. Must be > 0.
 * @param[in] par_Value Current value (clamped into [min,max] for the needle).
 * @param[in] par_Min Lower bound.
 * @param[in] par_Max Upper bound.
 * @param[in] par_Precision Decimal places for the value text.
 * @param[in] par_pName Variable name (drawn when nameFlag). May be nullptr.
 * @param[in] par_pValueText Pre-formatted value string (drawn when textFlag). May be nullptr.
 * @param[in] par_Colors Resolved paint colors.
 * @param[in] par_Style Gauge configuration.
 */
void DrawGauge(ImDrawList *par_pDrawList, const ImVec2 &par_Center, float par_Radius, double par_Value,
               double par_Min, double par_Max, int par_Precision, const char *par_pName,
               const char *par_pValueText, const GaugeColors &par_Colors, const GaugeStyle &par_Style);

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_FRAMEWORK_GAUGERENDERER_H
