#include "Framework/GaugeRenderer.h"

#include <cmath>
#include <cstdio>

namespace XilEnvImGui
{

namespace
{

constexpr double PI{3.14159265358979323846};
constexpr double DEG_TO_RAD{PI / 180.0};
constexpr int MAX_TICK_ITERATIONS{2000};

/**
 * @brief Converts a gauge angle (deg, clockwise from top) into an ImDrawList arc angle (rad).
 * @details The screen direction of gauge angle A is (sin A, -cos A) in ImGui's y-down space,
 *          which equals (cos(A-90), sin(A-90)); hence the arc parameter is simply A - 90.
 */
[[nodiscard]] float GaugeAngleToArcRad(double par_AngleDeg)
{
    return static_cast<float>((par_AngleDeg - 90.0) * DEG_TO_RAD);
}

/**
 * @brief Smallest absolute angular difference in degrees (mod 360).
 */
[[nodiscard]] double AngularDistance(double par_A, double par_B)
{
    double diff{std::fmod(par_A - par_B, 360.0)};
    if (diff < -180.0)
    {
        diff += 360.0;
    }
    if (diff > 180.0)
    {
        diff -= 360.0;
    }
    return std::fabs(diff);
}

/**
 * @brief Picks a human-friendly major-tick step (1/2/5 * 10^n) for a value range.
 */
[[nodiscard]] double NiceStep(double par_Range)
{
    if (par_Range <= 0.0)
    {
        return 1.0;
    }
    const double raw{par_Range / 8.0};
    const double magnitude{std::pow(10.0, std::floor(std::log10(raw)))};
    const double normalized{raw / magnitude};
    double nice{1.0};
    if (normalized < 1.5)
    {
        nice = 1.0;
    }
    else if (normalized < 3.0)
    {
        nice = 2.0;
    }
    else if (normalized < 7.0)
    {
        nice = 5.0;
    }
    else
    {
        nice = 10.0;
    }
    return nice * magnitude;
}

} // namespace

double GaugeValueToAngle(double par_Value, double par_Min, double par_Max, const GaugeStyle &par_Style)
{
    const double range{(par_Max > par_Min) ? (par_Max - par_Min) : 1.0};
    double clamped{par_Value};
    if (clamped < par_Min)
    {
        clamped = par_Min;
    }
    if (clamped > par_Max)
    {
        clamped = par_Max;
    }
    const double fraction{(clamped - par_Min) / range};
    return par_Style.minAngle + fraction * (par_Style.maxAngle - par_Style.minAngle);
}

double GaugeMouseToAngle(float par_Dx, float par_Dy)
{
    // atan2(dx, -dy): 0 = up, 90 = right, 180 = down, -90 = left (clockwise from top).
    return std::atan2(static_cast<double>(par_Dx), static_cast<double>(-par_Dy)) / DEG_TO_RAD;
}

double GaugeAngleToValue(double par_AngleDeg, double par_Min, double par_Max, const GaugeStyle &par_Style)
{
    const double range{(par_Max > par_Min) ? (par_Max - par_Min) : 1.0};
    const double angleRange{par_Style.maxAngle - par_Style.minAngle};
    if ((par_AngleDeg >= par_Style.minAngle) && (par_AngleDeg <= par_Style.maxAngle) && (angleRange != 0.0))
    {
        const double fraction{(par_AngleDeg - par_Style.minAngle) / angleRange};
        return par_Min + fraction * range;
    }
    // Inside the dead sector: snap to whichever scale end is angularly nearer.
    const double toMax{AngularDistance(par_AngleDeg, par_Style.maxAngle)};
    const double toMin{AngularDistance(par_AngleDeg, par_Style.minAngle)};
    return (toMax <= toMin) ? par_Max : par_Min;
}

void DrawGauge(ImDrawList *par_pDrawList, const ImVec2 &par_Center, float par_Radius, double par_Value,
               double par_Min, double par_Max, int par_Precision, const char *par_pName,
               const char *par_pValueText, const GaugeColors &par_Colors, const GaugeStyle &par_Style)
{
    (void)par_Precision;
    if ((nullptr == par_pDrawList) || (par_Radius <= 0.0f))
    {
        return;
    }

    const double range{(par_Max > par_Min) ? (par_Max - par_Min) : 1.0};

    if (par_Style.scaleFlag)
    {
        // Scale arc.
        par_pDrawList->PathArcTo(par_Center, par_Radius, GaugeAngleToArcRad(par_Style.minAngle),
                                 GaugeAngleToArcRad(par_Style.maxAngle), 96);
        par_pDrawList->PathStroke(par_Colors.scale, ImDrawFlags_None, 2.0f);

        double step{(par_Style.scaleStepFlag && (par_Style.scaleStep > 0.0)) ? par_Style.scaleStep
                                                                             : NiceStep(range)};
        if (step <= 0.0)
        {
            step = range;
        }
        const double minorStep{step / 5.0};
        const float majorLen{par_Radius * 0.14f};
        const float minorLen{par_Radius * 0.07f};

        // Minor ticks.
        int iterations{0};
        double firstMinor{std::ceil((par_Min - 1e-9) / minorStep) * minorStep};
        for (double value{firstMinor}; (value <= par_Max + 1e-9) && (iterations < MAX_TICK_ITERATIONS);
             value += minorStep, ++iterations)
        {
            const double ratio{value / step};
            if (std::fabs(ratio - std::round(ratio)) < 1e-6)
            {
                continue; // A major tick is drawn separately.
            }
            const double angle{par_Style.minAngle + (value - par_Min) / range *
                                                         (par_Style.maxAngle - par_Style.minAngle)};
            const float arc{GaugeAngleToArcRad(angle)};
            const ImVec2 dir{std::cos(arc), std::sin(arc)};
            const ImVec2 p1{par_Center.x + dir.x * par_Radius, par_Center.y + dir.y * par_Radius};
            const ImVec2 p2{par_Center.x + dir.x * (par_Radius + minorLen),
                            par_Center.y + dir.y * (par_Radius + minorLen)};
            par_pDrawList->AddLine(p1, p2, par_Colors.scale, 1.0f);
        }

        // Major ticks + labels.
        iterations = 0;
        double firstMajor{std::ceil((par_Min - 1e-9) / step) * step};
        for (double value{firstMajor}; (value <= par_Max + 1e-9) && (iterations < MAX_TICK_ITERATIONS);
             value += step, ++iterations)
        {
            const double angle{par_Style.minAngle + (value - par_Min) / range *
                                                         (par_Style.maxAngle - par_Style.minAngle)};
            const float arc{GaugeAngleToArcRad(angle)};
            const ImVec2 dir{std::cos(arc), std::sin(arc)};
            const ImVec2 p1{par_Center.x + dir.x * par_Radius, par_Center.y + dir.y * par_Radius};
            const ImVec2 p2{par_Center.x + dir.x * (par_Radius + majorLen),
                            par_Center.y + dir.y * (par_Radius + majorLen)};
            par_pDrawList->AddLine(p1, p2, par_Colors.scale, 2.0f);

            char label[32];
            const double shown{(std::fabs(value) < 1e-9) ? 0.0 : value};
            std::snprintf(label, sizeof(label), "%g", shown);
            const float labelRadius{par_Radius + majorLen + 3.0f};
            const ImVec2 anchor{par_Center.x + dir.x * labelRadius, par_Center.y + dir.y * labelRadius};
            const ImVec2 textSize{ImGui::CalcTextSize(label)};
            // Center the label on the anchor, then nudge it outward along the tick direction.
            const ImVec2 textPos{anchor.x - textSize.x * 0.5f + dir.x * textSize.x * 0.5f,
                                 anchor.y - textSize.y * 0.5f + dir.y * textSize.y * 0.5f};
            par_pDrawList->AddText(textPos, par_Colors.text, label);
        }
    }

    if (par_Style.needleFlag)
    {
        const double angle{GaugeValueToAngle(par_Value, par_Min, par_Max, par_Style)};
        const float arc{GaugeAngleToArcRad(angle)};
        const ImVec2 dir{std::cos(arc), std::sin(arc)};
        const ImVec2 perp{-dir.y, dir.x};
        const float needleLen{par_Radius * 0.88f};
        const float baseHalf{par_Radius * 0.055f};
        const float tailLen{par_Radius * 0.18f};
        const ImVec2 tip{par_Center.x + dir.x * needleLen, par_Center.y + dir.y * needleLen};
        const ImVec2 base1{par_Center.x + perp.x * baseHalf, par_Center.y + perp.y * baseHalf};
        const ImVec2 base2{par_Center.x - perp.x * baseHalf, par_Center.y - perp.y * baseHalf};
        const ImVec2 tail{par_Center.x - dir.x * tailLen, par_Center.y - dir.y * tailLen};
        par_pDrawList->AddTriangleFilled(tip, base1, base2, par_Colors.needle);
        par_pDrawList->AddLine(par_Center, tail, par_Colors.needle, 2.0f);
    }

    if (par_Style.drawHub)
    {
        const float hubRadius{par_Radius * 0.09f};
        par_pDrawList->AddCircleFilled(par_Center, hubRadius, par_Colors.needle, 20);
        par_pDrawList->AddCircle(par_Center, hubRadius, par_Colors.scale, 20, 1.5f);
    }

    if (par_Style.nameFlag && (nullptr != par_pName) && ('\0' != par_pName[0]))
    {
        const ImVec2 textSize{ImGui::CalcTextSize(par_pName)};
        const ImVec2 pos{par_Center.x - textSize.x * 0.5f,
                         par_Center.y - par_Radius - textSize.y - 2.0f};
        par_pDrawList->AddText(pos, par_Colors.text, par_pName);
    }

    if (par_Style.textFlag && (nullptr != par_pValueText) && ('\0' != par_pValueText[0]))
    {
        const ImVec2 textSize{ImGui::CalcTextSize(par_pValueText)};
        const ImVec2 pos{par_Center.x - textSize.x * 0.5f, par_Center.y + par_Radius * 0.5f};
        par_pDrawList->AddText(pos, par_Colors.text, par_pValueText);
    }
}

} // namespace XilEnvImGui
