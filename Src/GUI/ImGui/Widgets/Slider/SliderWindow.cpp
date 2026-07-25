#include "Widgets/Slider/SliderWindow.h"

#include <cfloat>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "imgui.h"

#include "Framework/BlackboardDragDrop.h"
#include "Framework/GuiComponents.h"

extern "C" {
#include "Blackboard.h"
#include "IniDataBase.h"
}

namespace XilEnvImGui
{

std::unique_ptr<IWindowInstance> SliderWindowType::CreateNew(const std::string &par_Title)
{
    return std::make_unique<SliderWindow>(this, par_Title);
}

std::unique_ptr<IWindowInstance> SliderWindowType::CreateFromConfig(int par_FileDescriptor,
                                                                    const char *par_Section,
                                                                    const std::string &par_Name)
{
    auto window = std::make_unique<SliderWindow>(this, par_Name);
    window->ReadConfig(par_FileDescriptor, par_Section);
    return window;
}

SliderWindow::SliderWindow(IWindowType *par_Type, std::string par_Name) : m_pType{par_Type}
{
    SetName(std::move(par_Name));
}

void SliderWindow::OnUpdateTick(const WindowContext &par_Context)
{
    (void)par_Context;
    if (m_variable.IsBound())
    {
        m_value = m_variable.ReadValue();
    }
}

void SliderWindow::OnAcceptVariableDrop(const std::string &par_VariableName)
{
    m_variable.Bind(par_VariableName);
    m_value = m_variable.ReadValue();
    std::snprintf(m_bindBuffer, sizeof(m_bindBuffer), "%s", m_variable.GetName().c_str());
}

void SliderWindow::RenderContent(const WindowContext &par_Context)
{
    (void)par_Context;

    const ImVec2 avail{ImGui::GetContentRegionAvail()};

    if (!m_variable.IsBound())
    {
        ImGui::TextDisabled("Drop a blackboard variable here");
    }
    else
    {
        char unit[BBVARI_UNIT_SIZE];
        m_variable.ReadUnit(unit, sizeof(unit));

        ImGui::Text("%s", m_variable.GetName().c_str());

        char format[BBVARI_UNIT_SIZE + 16];
        std::snprintf(format, sizeof(format), "%%.%df %s",
                      (0 <= m_variable.GetPrecision()) ? m_variable.GetPrecision() : 0, unit);

        double minValue{m_variable.GetMin()};
        double maxValue{m_variable.GetMax()};
        double editValue{m_value};

        ImGui::SetNextItemWidth(-FLT_MIN);
        bool changed{false};
        if (m_horizontal)
        {
            changed = ImGui::SliderScalar("##slider", ImGuiDataType_Double, &editValue, &minValue,
                                          &maxValue, format);
        }
        else
        {
            const ImVec2 sliderSize{ImGui::GetFrameHeight(), avail.y - ImGui::GetFrameHeightWithSpacing()};
            changed = ImGui::VSliderScalar("##slider", sliderSize, ImGuiDataType_Double, &editValue,
                                           &minValue, &maxValue, format);
        }
        if (changed)
        {
            m_value = editValue;
            m_variable.WriteValue(m_value);
        }
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

void SliderWindow::RenderSettingsPopup()
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
    ImGui::Checkbox("Horizontal", &m_horizontal);

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
}

void SliderWindow::WriteConfig(int par_FileDescriptor, const char *par_Section)
{
    (void)IniFileDataBaseWriteString(par_Section, "type", m_pType->GetTypeName(), par_FileDescriptor);
    m_variable.WriteQtConfig(par_FileDescriptor, par_Section);
    (void)IniFileDataBaseWriteYesNo(par_Section, "Horizontal", m_horizontal ? 1 : 0, par_FileDescriptor);
}

void SliderWindow::ReadConfig(int par_FileDescriptor, const char *par_Section)
{
    m_variable.ReadQtConfig(par_FileDescriptor, par_Section);
    m_horizontal = (0 != IniFileDataBaseReadYesNo(par_Section, "Horizontal", 1, par_FileDescriptor));
    std::snprintf(m_bindBuffer, sizeof(m_bindBuffer), "%s", m_variable.GetName().c_str());
    m_value = m_variable.ReadValue();
}

} // namespace XilEnvImGui
