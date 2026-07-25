#include "Widgets/Bargraph/BargraphWindow.h"

#include <algorithm>
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

} // namespace

std::unique_ptr<IWindowInstance> BargraphWindowType::CreateNew(const std::string &par_Title)
{
    return std::make_unique<BargraphWindow>(this, par_Title);
}

std::unique_ptr<IWindowInstance> BargraphWindowType::CreateFromConfig(int par_FileDescriptor,
                                                                      const char *par_Section,
                                                                      const std::string &par_Name)
{
    auto window = std::make_unique<BargraphWindow>(this, par_Name);
    window->ReadConfig(par_FileDescriptor, par_Section);
    return window;
}

BargraphWindow::BargraphWindow(IWindowType *par_Type, std::string par_Name) : m_pType{par_Type}
{
    SetName(std::move(par_Name));
}

void BargraphWindow::OnUpdateTick(const WindowContext &par_Context)
{
    (void)par_Context;
    if (m_variable.IsBound())
    {
        m_value = m_variable.ReadValue();
    }
}

void BargraphWindow::OnAcceptVariableDrop(const std::string &par_VariableName)
{
    m_variable.Bind(par_VariableName);
    m_value = m_variable.ReadValue();
    std::snprintf(m_bindBuffer, sizeof(m_bindBuffer), "%s", m_variable.GetName().c_str());
}

void BargraphWindow::RenderContent(const WindowContext &par_Context)
{
    (void)par_Context;

    if (!m_variable.IsBound())
    {
        ImGui::TextDisabled("Drop a blackboard variable here");
    }
    else
    {
        char unit[BBVARI_UNIT_SIZE];
        m_variable.ReadUnit(unit, sizeof(unit));
        const int precision{(0 <= m_variable.GetPrecision()) ? m_variable.GetPrecision() : 0};
        ImGui::Text("%s: %.*f %s", m_variable.GetName().c_str(), precision, m_value, unit);
    }

    const ImVec2 avail{ImGui::GetContentRegionAvail()};
    const ImVec2 origin{ImGui::GetCursorScreenPos()};
    if ((avail.x <= 1.0f) || (avail.y <= 1.0f))
    {
        ImGui::Dummy(avail);
    }
    else
    {
        ImDrawList *drawList{ImGui::GetWindowDrawList()};
        const ImU32 bgColor{ImGui::GetColorU32(ImGuiCol_FrameBg)};
        const ImU32 borderColor{ImGui::GetColorU32(ImGuiCol_Border)};
        drawList->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), bgColor);

        double fraction{0.0};
        if (m_variable.IsBound())
        {
            const double range{m_variable.GetMax() - m_variable.GetMin()};
            if (range > 0.0)
            {
                fraction = (m_value - m_variable.GetMin()) / range;
            }
            fraction = std::max(0.0, std::min(1.0, fraction));
        }

        const std::int32_t color{m_variable.GetColor()};
        const ImU32 fillColor{(0 != color) ? BlackboardColorToImU32(color)
                                           : ImGui::GetColorU32(ImGuiCol_PlotHistogram)};

        if (m_vertical)
        {
            const float filledHeight{static_cast<float>(fraction) * avail.y};
            const ImVec2 barMin{origin.x, origin.y + avail.y - filledHeight};
            const ImVec2 barMax{origin.x + avail.x, origin.y + avail.y};
            drawList->AddRectFilled(barMin, barMax, fillColor);
        }
        else
        {
            const float filledWidth{static_cast<float>(fraction) * avail.x};
            const ImVec2 barMin{origin.x, origin.y};
            const ImVec2 barMax{origin.x + filledWidth, origin.y + avail.y};
            drawList->AddRectFilled(barMin, barMax, fillColor);
        }
        drawList->AddRect(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), borderColor);

        // Reserve the drawn area so drag&drop hit-testing covers the whole bar.
        ImGui::Dummy(avail);
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

void BargraphWindow::RenderSettingsPopup()
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
    ImGui::Checkbox("Vertical", &m_vertical);

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

void BargraphWindow::WriteConfig(int par_FileDescriptor, const char *par_Section)
{
    (void)IniFileDataBaseWriteString(par_Section, "type", m_pType->GetTypeName(), par_FileDescriptor);
    m_variable.WriteQtConfig(par_FileDescriptor, par_Section);
    (void)IniFileDataBaseWriteYesNo(par_Section, "Vertical", m_vertical ? 1 : 0, par_FileDescriptor);
}

void BargraphWindow::ReadConfig(int par_FileDescriptor, const char *par_Section)
{
    m_variable.ReadQtConfig(par_FileDescriptor, par_Section);
    m_vertical = (0 != IniFileDataBaseReadYesNo(par_Section, "Vertical", 1, par_FileDescriptor));
    std::snprintf(m_bindBuffer, sizeof(m_bindBuffer), "%s", m_variable.GetName().c_str());
    m_value = m_variable.ReadValue();
}

} // namespace XilEnvImGui
