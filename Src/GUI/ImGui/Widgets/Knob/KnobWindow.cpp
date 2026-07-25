#include "Widgets/Knob/KnobWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "imgui.h"

#include "Framework/BlackboardDragDrop.h"
#include "Framework/GaugeRenderer.h"
#include "Framework/GuiComponents.h"

extern "C" {
#include "Blackboard.h"
#include "IniDataBase.h"
}

namespace XilEnvImGui
{

namespace
{

/**
 * @brief Packs a 0x00bbggrr blackboard color into an ImGui ABGR color.
 */
ImU32 BlackboardColorToImU32(std::int32_t par_Color)
{
    const int red{par_Color & 0x000000FF};
    const int green{(par_Color & 0x0000FF00) >> 8};
    const int blue{(par_Color & 0x00FF0000) >> 16};
    return IM_COL32(red, green, blue, 255);
}

/**
 * @brief Resolves the paint colors for a gauge from the theme and the bound variable.
 */
GaugeColors ResolveGaugeColors(std::int32_t par_VariableColor)
{
    GaugeColors colors;
    colors.scale = ImGui::GetColorU32(ImGuiCol_Text);
    colors.text = ImGui::GetColorU32(ImGuiCol_Text);
    colors.needle =
        (0 != par_VariableColor) ? BlackboardColorToImU32(par_VariableColor) : IM_COL32(220, 60, 50, 255);
    return colors;
}

} // namespace

std::unique_ptr<IWindowInstance> KnobWindowType::CreateNew(const std::string &par_Title)
{
    return std::make_unique<KnobWindow>(this, par_Title);
}

std::unique_ptr<IWindowInstance> KnobWindowType::CreateFromConfig(int par_FileDescriptor,
                                                                  const char *par_Section,
                                                                  const std::string &par_Name)
{
    auto window = std::make_unique<KnobWindow>(this, par_Name);
    window->ReadConfig(par_FileDescriptor, par_Section);
    return window;
}

KnobWindow::KnobWindow(IWindowType *par_Type, std::string par_Name) : m_pType{par_Type}
{
    SetName(std::move(par_Name));
}

void KnobWindow::OnUpdateTick(const WindowContext &par_Context)
{
    (void)par_Context;
    // While the user drags the knob, the value is driven by the pointer, not the blackboard.
    if (!m_dragging && m_variable.IsBound())
    {
        m_value = m_variable.ReadValue();
    }
}

void KnobWindow::OnAcceptVariableDrop(const std::string &par_VariableName)
{
    m_variable.Bind(par_VariableName);
    m_value = m_variable.ReadValue();
    std::snprintf(m_bindBuffer, sizeof(m_bindBuffer), "%s", m_variable.GetName().c_str());
}

void KnobWindow::RenderContent(const WindowContext &par_Context)
{
    (void)par_Context;

    const ImVec2 avail{ImGui::GetContentRegionAvail()};
    const ImVec2 origin{ImGui::GetCursorScreenPos()};
    const float minDim{std::min(avail.x, avail.y)};

    // One invisible button over the whole content rect: captures the knob drag and doubles
    // as the blackboard drop target (drag&drop hit-testing needs an item to cover the area).
    ImGui::InvisibleButton("##knob", ImVec2(std::max(avail.x, 1.0f), std::max(avail.y, 1.0f)));

    const ImVec2 center{origin.x + avail.x * 0.5f, origin.y + avail.y * 0.5f};
    const float radius{minDim * 0.36f};

    if (minDim > 8.0f)
    {
        const double minValue{m_variable.GetMin()};
        const double maxValue{m_variable.GetMax()};
        const int precision{(0 <= m_variable.GetPrecision()) ? m_variable.GetPrecision() : 0};

        // Interactive write-back: start a drag only when the press lands inside the knob.
        if (m_variable.IsBound())
        {
            if (ImGui::IsItemActivated())
            {
                const ImVec2 mouse{ImGui::GetIO().MousePos};
                const float dx{mouse.x - center.x};
                const float dy{mouse.y - center.y};
                m_dragging = ((dx * dx + dy * dy) <= (radius * radius));
            }
            if (m_dragging && ImGui::IsItemActive())
            {
                const ImVec2 mouse{ImGui::GetIO().MousePos};
                const double angle{GaugeMouseToAngle(mouse.x - center.x, mouse.y - center.y)};
                // Overwind freeze (mirrors Qt): while the cursor is inside the bottom dead
                // sector, hold the last in-scale value instead of snapping min<->max and
                // writing that glitch to the setpoint signal.
                if (GaugeAngleIsInScale(angle, m_style))
                {
                    m_value = GaugeAngleToValue(angle, minValue, maxValue, m_style);
                    m_variable.WriteValue(m_value);
                }
            }
            if (ImGui::IsItemDeactivated())
            {
                m_dragging = false;
            }
        }

        char unit[BBVARI_UNIT_SIZE]{};
        m_variable.ReadUnit(unit, sizeof(unit));

        char valueText[BBVARI_UNIT_SIZE + 32]{};
        std::string name;
        if (m_variable.IsBound())
        {
            std::snprintf(valueText, sizeof(valueText), "%.*f %s", precision, m_value, unit);
            name = m_variable.GetName();
        }

        const GaugeColors colors{ResolveGaugeColors(m_variable.GetColor())};
        DrawGauge(ImGui::GetWindowDrawList(), center, radius, m_value, minValue, maxValue, precision,
                  name.empty() ? nullptr : name.c_str(), m_variable.IsBound() ? valueText : nullptr,
                  colors, m_style);
    }

    if (!m_variable.IsBound())
    {
        ImGui::SetCursorScreenPos(origin);
        ImGui::TextDisabled("Drop a blackboard variable here");
    }

    if (ImGui::BeginDragDropTarget())
    {
        BlackboardVariablePayload payload;
        if (AcceptBlackboardVariableDrop(payload))
        {
            m_variable.BindFromPayload(payload);
            m_value = m_variable.ReadValue();
            std::snprintf(m_bindBuffer, sizeof(m_bindBuffer), "%s", m_variable.GetName().c_str());
        }
        ImGui::EndDragDropTarget();
    }
}

void KnobWindow::RenderSettingsPopup()
{
    ImGui::TextUnformatted("Variable:");
    if (DrawBlackboardSelector("##bind", m_bindBuffer, sizeof(m_bindBuffer)))
    {
        m_variable.Bind(m_bindBuffer);
        m_value = m_variable.ReadValue();
    }

    ImGui::Separator();

    bool physical{m_variable.IsPhysical()};
    if (ImGui::Checkbox("Physical value", &physical))
    {
        m_variable.SetPhysical(physical);
    }

    double minValue{m_variable.GetMin()};
    double maxValue{m_variable.GetMax()};
    const int precision{(0 <= m_variable.GetPrecision()) ? m_variable.GetPrecision() : 0};
    bool rangeChanged{false};
    rangeChanged |= DrawPhysValueInput("Min", &minValue, -1e30, 1e30, precision);
    rangeChanged |= DrawPhysValueInput("Max", &maxValue, -1e30, 1e30, precision);
    if (rangeChanged)
    {
        m_variable.SetMinMax(minValue, maxValue);
    }

    ImGui::Separator();

    ImGui::Checkbox("Show scale", &m_style.scaleFlag);
    ImGui::Checkbox("Show needle", &m_style.needleFlag);
    ImGui::Checkbox("Show name", &m_style.nameFlag);
    ImGui::Checkbox("Show value", &m_style.textFlag);

    DrawPhysValueInput("Min angle", &m_style.minAngle, -360.0, 360.0, 1);
    DrawPhysValueInput("Max angle", &m_style.maxAngle, -360.0, 360.0, 1);

    ImGui::Checkbox("Fixed scale step", &m_style.scaleStepFlag);
    if (m_style.scaleStepFlag)
    {
        DrawPhysValueInput("Scale step", &m_style.scaleStep, 0.0, 1e30, precision);
    }
}

void KnobWindow::WriteConfig(int par_FileDescriptor, const char *par_Section)
{
    // Never clear the section: non-rendered Qt keys (fonts, bitmap, drag status) survive.
    (void)IniFileDataBaseWriteString(par_Section, "type", m_pType->GetTypeName(), par_FileDescriptor);
    m_variable.WriteQtConfig(par_FileDescriptor, par_Section);
    (void)IniFileDataBaseWriteYesNo(par_Section, "ScaleFlag", m_style.scaleFlag ? 1 : 0, par_FileDescriptor);
    (void)IniFileDataBaseWriteFloat(par_Section, "MinAngle", m_style.minAngle, par_FileDescriptor);
    (void)IniFileDataBaseWriteFloat(par_Section, "MaxAngle", m_style.maxAngle, par_FileDescriptor);
    (void)IniFileDataBaseWriteYesNo(par_Section, "ScaleStepFlag", m_style.scaleStepFlag ? 1 : 0,
                                    par_FileDescriptor);
    (void)IniFileDataBaseWriteFloat(par_Section, "ScaleStep", m_style.scaleStep, par_FileDescriptor);
    (void)IniFileDataBaseWriteYesNo(par_Section, "NeedleFlag", m_style.needleFlag ? 1 : 0, par_FileDescriptor);
    (void)IniFileDataBaseWriteYesNo(par_Section, "TextFlag", m_style.textFlag ? 1 : 0, par_FileDescriptor);
    (void)IniFileDataBaseWriteYesNo(par_Section, "NameFlag", m_style.nameFlag ? 1 : 0, par_FileDescriptor);
}

void KnobWindow::ReadConfig(int par_FileDescriptor, const char *par_Section)
{
    m_variable.ReadQtConfig(par_FileDescriptor, par_Section);
    m_style.scaleFlag = (0 != IniFileDataBaseReadYesNo(par_Section, "ScaleFlag", 1, par_FileDescriptor));
    m_style.minAngle = IniFileDataBaseReadFloat(par_Section, "MinAngle", -160.0, par_FileDescriptor);
    m_style.maxAngle = IniFileDataBaseReadFloat(par_Section, "MaxAngle", 160.0, par_FileDescriptor);
    m_style.scaleStepFlag =
        (0 != IniFileDataBaseReadYesNo(par_Section, "ScaleStepFlag", 0, par_FileDescriptor));
    m_style.scaleStep = IniFileDataBaseReadFloat(par_Section, "ScaleStep", 30.0, par_FileDescriptor);
    m_style.needleFlag = (0 != IniFileDataBaseReadYesNo(par_Section, "NeedleFlag", 1, par_FileDescriptor));
    m_style.textFlag = (0 != IniFileDataBaseReadYesNo(par_Section, "TextFlag", 1, par_FileDescriptor));
    m_style.nameFlag = (0 != IniFileDataBaseReadYesNo(par_Section, "NameFlag", 1, par_FileDescriptor));
    m_style.drawHub = true;
    std::snprintf(m_bindBuffer, sizeof(m_bindBuffer), "%s", m_variable.GetName().c_str());
    m_value = m_variable.ReadValue();
}

} // namespace XilEnvImGui
