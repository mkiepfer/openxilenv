#include "Widgets/A2LCalMap/A2LCalMapWindow.h"

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "imgui.h"

#include "Framework/A2LCalBridge.h"
#include "Framework/BlackboardDragDrop.h"

#include "A2LCalWidgetSync.h"

extern "C" {
#include "A2LLinkThread.h"
#include "IniDataBase.h"
#include "PrintFormatToString.h"
#include "Scheduler.h"
}

namespace XilEnvImGui
{

using AxisType = A2LCalMapData::AxisType;

// -------------------------------------------------------------------------
// A2LCalMapWindowType
// -------------------------------------------------------------------------

std::unique_ptr<IWindowInstance> A2LCalMapWindowType::CreateNew(const std::string &par_Title)
{
    return std::make_unique<A2LCalMapWindow>(this, par_Title);
}

std::unique_ptr<IWindowInstance> A2LCalMapWindowType::CreateFromConfig(int par_FileDescriptor,
                                                                       const char *par_Section,
                                                                       const std::string &par_Name)
{
    auto window = std::make_unique<A2LCalMapWindow>(this, par_Name);
    window->ReadConfig(par_FileDescriptor, par_Section);
    return window;
}

// -------------------------------------------------------------------------
// A2LCalMapWindow
// -------------------------------------------------------------------------

A2LCalMapWindow::A2LCalMapWindow(IWindowType *par_Type, std::string par_Name) : m_pType{par_Type}
{
    SetName(std::move(par_Name));
    m_renderer.InitTrans3D();
    A2LCalBridge::GetInstance().RegisterWidget(this);
}

A2LCalMapWindow::~A2LCalMapWindow()
{
    A2LCalBridge::GetInstance().UnregisterWidget(this);
}

void A2LCalMapWindow::SetupProcessAndCharacteristic()
{
    if (m_process.empty() || m_characteristic.empty())
    {
        return;
    }
    m_data.SetProcess(m_process, this);
    m_data.SetCharacteristicName(m_characteristic);
    (void)m_data.UpdateReq();
}

// -------------------------------------------------------------------------
// IA2LCalWidget (dispatched on the render thread by A2LCalBridge)
// -------------------------------------------------------------------------

void A2LCalMapWindow::OnLinkDataChanged(int par_LinkNo, int par_Index, A2L_DATA *par_Data)
{
    (void)par_LinkNo;
    (void)par_Index;
    (void)par_Data;
    // Mirrors the Qt A2LCalMapModel::NotifyDataChanged which is a no-op.
}

void A2LCalMapWindow::OnProcessStartStop(const std::string &par_ProcessName, int par_Flags, int par_Action)
{
    (void)par_Flags;
    if (0 != Compare2ProcessNames(par_ProcessName.c_str(), m_process.c_str()))
    {
        return;
    }
    if (0 == par_Action)
    {
        m_data.ProcessTerminated();
    }
    else if (1 == par_Action)
    {
        SetupProcessAndCharacteristic();
    }
}

void A2LCalMapWindow::OnGetDataFromLinkAck(void *par_IndexData, int par_FetchDataChannelNo)
{
    // Self-filter: ignore acknowledgements for channels this window did not register.
    if (par_FetchDataChannelNo != m_data.GetDataChannelNo())
    {
        return;
    }
    INDEX_DATA_BLOCK *pIndexData{static_cast<INDEX_DATA_BLOCK *>(par_IndexData)};
    if ((nullptr == pIndexData) || (nullptr == pIndexData->Data))
    {
        return;
    }
    // The map window fetches exactly one characteristic; the first element carries it.
    if ((pIndexData->Data->LinkNo == m_data.GetLinkNo()) && (pIndexData->Data->Index == m_data.GetIndex()))
    {
        (void)m_data.UpdateAck(pIndexData->Data->Data);
    }
}

// -------------------------------------------------------------------------
// Render / update
// -------------------------------------------------------------------------

void A2LCalMapWindow::OnUpdateTick(const WindowContext &par_Context)
{
    (void)par_Context;
    // Periodically re-read the grid so external changes (model / XCP) show up.
    (void)m_data.UpdateReq();
}

void A2LCalMapWindow::OnAcceptVariableDrop(const std::string &par_VariableName)
{
    // A dropped label rebinds the shown characteristic (ImGui addition; Qt used a config dialog).
    if (par_VariableName.empty() || m_process.empty())
    {
        return;
    }
    m_characteristic = par_VariableName;
    std::snprintf(m_characteristicBuffer, sizeof(m_characteristicBuffer), "%s", m_characteristic.c_str());
    m_data.SetCharacteristicName(m_characteristic);
    (void)m_data.UpdateReq();
}

void A2LCalMapWindow::Render3DView(const ImVec2 &par_Size)
{
    ImGui::BeginChild("##a2lcalmap3d", par_Size, true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 origin{ImGui::GetCursorScreenPos()};
    const ImVec2 avail{ImGui::GetContentRegionAvail()};
    ImDrawList *pDrawList{ImGui::GetWindowDrawList()};

    m_renderer.Render(pDrawList, origin, avail, m_data);

    // Interaction surface covering the whole 3D area.
    ImGui::InvisibleButton("##a2lcalmap3dhit", avail,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered{ImGui::IsItemHovered()};

    // Left-drag rotates (azimuth = horizontal, elevation = vertical).
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        const ImVec2 delta{ImGui::GetIO().MouseDelta};
        if ((0.0f != delta.x) || (0.0f != delta.y))
        {
            m_renderer.RotateZAngle(static_cast<double>(delta.x) * (M_PI / 180.0));
            m_renderer.RotateXAngle(static_cast<double>(delta.y) * (M_PI / 180.0));
            m_renderer.UpdateSinCos();
        }
    }
    // Plain left-click (released without dragging) picks the nearest point.
    else if (ImGui::IsItemDeactivated() && (2 == m_data.GetType() || 1 == m_data.GetType()))
    {
        const ImVec2 mouse{ImGui::GetIO().MousePos};
        const bool add{ImGui::GetIO().KeyCtrl};
        if (m_renderer.PickingMap(static_cast<int>(avail.x / 2.0f), static_cast<int>(avail.y / 2.0f),
                                  static_cast<int>(mouse.x - origin.x), static_cast<int>(mouse.y - origin.y), add,
                                  m_data))
        {
            m_data.SetNoRecursiveSetPickMapFlag(true);
            m_data.SetNoRecursiveSetPickMapFlag(false);
        }
    }
    // Wheel zooms.
    if (hovered)
    {
        const float wheel{ImGui::GetIO().MouseWheel};
        if (0.0f != wheel)
        {
            m_renderer.ScaleFactor((wheel > 0.0f) ? 1.1 : (1.0 / 1.1));
        }
    }
    ImGui::EndChild();
}

void A2LCalMapWindow::CommitAxisEdit()
{
    if (0 > m_editAxis)
    {
        return;
    }
    const double value{std::strtod(m_editBuffer, nullptr)};
    const AxisType axis{static_cast<AxisType>(m_editAxis)};
    if (0 == m_data.ChangeValueOP('I', value, axis, m_editX, m_editY))
    {
        (void)m_data.WriteDataToTargetLink(false);
    }
    m_editAxis = -1;
    m_editX = -1;
    m_editY = -1;
}

void A2LCalMapWindow::RenderTable(const ImVec2 &par_Size)
{
    const int xDim{m_data.GetXDim()};
    const int yDim{m_data.GetYDim()};
    const int columnCount{xDim + 1};
    const int rowCount{yDim + 1};

    ImGui::BeginChild("##a2lcalmaptable", par_Size, true);
    if (!m_data.IsValid() || (0 >= xDim) || (0 >= columnCount))
    {
        ImGui::TextDisabled("no map data");
        ImGui::EndChild();
        return;
    }

    const ImGuiTableFlags tableFlags{ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollX |
                                     ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit};
    const ImU32 headerColor{ImGui::GetColorU32(ImVec4(0.32f, 0.32f, 0.32f, 1.0f))};
    const ImU32 pickColor{ImGui::GetColorU32(ImVec4(0.55f, 0.20f, 0.20f, 1.0f))};

    if (ImGui::BeginTable("##a2lcalmapgrid", columnCount, tableFlags))
    {
        for (int col{0}; col < columnCount; ++col)
        {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        }
        for (int row{0}; row < rowCount; ++row)
        {
            ImGui::TableNextRow();
            for (int col{0}; col < columnCount; ++col)
            {
                ImGui::TableSetColumnIndex(col);
                ImGui::PushID(row * columnCount + col);

                if ((0 == row) && (0 == col))
                {
                    // Raw / phys switch.
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, headerColor);
                    if (ImGui::Selectable(m_data.IsPhysical() ? "phys" : "raw", false))
                    {
                        m_data.SetPhysical(!m_data.IsPhysical());
                    }
                }
                else if (0 == row)
                {
                    // X axis header (editable).
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, headerColor);
                    const int x{col - 1};
                    const bool editing{(0 == m_editAxis) && (m_editX == x)};
                    if (editing)
                    {
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::InputText("##edit", m_editBuffer, sizeof(m_editBuffer),
                                             ImGuiInputTextFlags_EnterReturnsTrue))
                        {
                            CommitAxisEdit();
                        }
                    }
                    else
                    {
                        const std::string text{m_data.GetValueString(AxisType::XAxis, x, 0)};
                        if (ImGui::Selectable(text.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
                            ImGui::IsMouseDoubleClicked(0))
                        {
                            m_editAxis = 0;
                            m_editX = x;
                            m_editY = 0;
                            std::snprintf(m_editBuffer, sizeof(m_editBuffer), "%s", text.c_str());
                        }
                    }
                }
                else if (0 == col)
                {
                    // Y axis header (editable).
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, headerColor);
                    const int y{row - 1};
                    const bool editing{(1 == m_editAxis) && (m_editY == y)};
                    if (editing)
                    {
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::InputText("##edit", m_editBuffer, sizeof(m_editBuffer),
                                             ImGuiInputTextFlags_EnterReturnsTrue))
                        {
                            CommitAxisEdit();
                        }
                    }
                    else
                    {
                        const std::string text{m_data.GetValueString(AxisType::YAxis, y, 0)};
                        if (ImGui::Selectable(text.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
                            ImGui::IsMouseDoubleClicked(0))
                        {
                            m_editAxis = 1;
                            m_editX = y;
                            m_editY = 0;
                            std::snprintf(m_editBuffer, sizeof(m_editBuffer), "%s", text.c_str());
                        }
                    }
                }
                else
                {
                    // Map cell (pick on click, edit on double-click).
                    const int x{col - 1};
                    const int y{row - 1};
                    const bool picked{m_data.IsPicked(x, y)};
                    if (picked)
                    {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, pickColor);
                    }
                    const bool editing{(2 == m_editAxis) && (m_editX == x) && (m_editY == y)};
                    if (editing)
                    {
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::InputText("##edit", m_editBuffer, sizeof(m_editBuffer),
                                             ImGuiInputTextFlags_EnterReturnsTrue))
                        {
                            CommitAxisEdit();
                        }
                    }
                    else
                    {
                        const std::string text{m_data.GetValueString(AxisType::Map, x, y)};
                        if (ImGui::Selectable(text.c_str(), picked, ImGuiSelectableFlags_AllowDoubleClick))
                        {
                            if (ImGui::IsMouseDoubleClicked(0))
                            {
                                m_editAxis = 2;
                                m_editX = x;
                                m_editY = y;
                                std::snprintf(m_editBuffer, sizeof(m_editBuffer), "%s", text.c_str());
                            }
                            else
                            {
                                m_data.SetPick(x, y, 3); // toggle pick
                            }
                        }
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void A2LCalMapWindow::RenderContent(const WindowContext &par_Context)
{
    (void)par_Context;

    const ImVec2 avail{ImGui::GetContentRegionAvail()};
    m_lastContentHeight = avail.y;

    constexpr float SPLITTER_THICKNESS{6.0f};
    float topHeight{(avail.y - SPLITTER_THICKNESS) * m_splitRatio};
    if (topHeight < 40.0f)
    {
        topHeight = 40.0f;
    }
    float bottomHeight{avail.y - SPLITTER_THICKNESS - topHeight};
    if (bottomHeight < 40.0f)
    {
        bottomHeight = 40.0f;
    }

    Render3DView(ImVec2(avail.x, topHeight));

    // Draggable splitter between the 3D view and the table.
    ImGui::InvisibleButton("##a2lcalmapsplitter", ImVec2(avail.x, SPLITTER_THICKNESS));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }
    if (ImGui::IsItemActive())
    {
        const float delta{ImGui::GetIO().MouseDelta.y};
        if (0.0f < avail.y)
        {
            m_splitRatio += delta / avail.y;
            m_splitRatio = (m_splitRatio < 0.1f) ? 0.1f : ((m_splitRatio > 0.9f) ? 0.9f : m_splitRatio);
        }
    }

    RenderTable(ImVec2(avail.x, bottomHeight));

    if (ImGui::BeginDragDropTarget())
    {
        BlackboardVariablePayload payload;
        if (AcceptBlackboardVariableDrop(payload))
        {
            OnAcceptVariableDrop(payload.name);
        }
        ImGui::EndDragDropTarget();
    }
}

void A2LCalMapWindow::RenderSettingsPopup()
{
    ImGui::TextUnformatted("Process:");
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("##process", m_processBuffer, sizeof(m_processBuffer));
    ImGui::TextUnformatted("Characteristic:");
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("##characteristic", m_characteristicBuffer, sizeof(m_characteristicBuffer));
    if (ImGui::Button("Apply"))
    {
        m_process = m_processBuffer;
        m_characteristic = m_characteristicBuffer;
        SetupProcessAndCharacteristic();
        if (2 == m_data.GetType())
        {
            m_renderer.InitTrans3D();
        }
        else
        {
            m_renderer.InitTrans2D();
        }
    }

    ImGui::Separator();
    if (m_data.IsPhysical())
    {
        if (ImGui::Button("switch to raw"))
        {
            m_data.SetPhysical(false);
        }
    }
    else
    {
        if (ImGui::Button("switch to physical"))
        {
            m_data.SetPhysical(true);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("adjust min/max"))
    {
        m_data.AdjustMinMax();
        m_data.CalcRawMinMaxValues();
    }
    ImGui::SameLine();
    if (ImGui::Button("reset view"))
    {
        if (2 == m_data.GetType())
        {
            m_renderer.InitTrans3D();
        }
        else
        {
            m_renderer.InitTrans2D();
        }
    }

    ImGui::Separator();
    if (ImGui::Button("select all"))
    {
        m_data.ChangeSelectionOfAll(true);
    }
    ImGui::SameLine();
    if (ImGui::Button("unselect all"))
    {
        m_data.ChangeSelectionOfAll(false);
    }
    ImGui::SameLine();
    if (ImGui::Button("read from link"))
    {
        (void)m_data.UpdateReq();
    }
}

// -------------------------------------------------------------------------
// Config round-trip (Qt "GUI/Widgets/<name>" layout)
// -------------------------------------------------------------------------

void A2LCalMapWindow::WriteConfig(int par_FileDescriptor, const char *par_Section)
{
    (void)IniFileDataBaseWriteString(par_Section, "type", m_pType->GetTypeName(), par_FileDescriptor);
    (void)IniFileDataBaseWriteString(par_Section, "process", m_process.c_str(), par_FileDescriptor);
    (void)IniFileDataBaseWriteString(par_Section, "characteristic", m_characteristic.c_str(), par_FileDescriptor);
    (void)IniFileDataBaseWriteYesNo(par_Section, "Pysical", m_data.IsPhysical() ? 1 : 0, par_FileDescriptor);
    (void)IniFileDataBaseWriteFloat(par_Section, "DecIncOffset", m_data.GetDecIncOffset(), par_FileDescriptor);

    char help[64];
    PrintFormatToString(help, sizeof(help), "%g", m_renderer.GetXAngle());
    (void)IniFileDataBaseWriteString(par_Section, "trans3d.ax", help, par_FileDescriptor);
    PrintFormatToString(help, sizeof(help), "%g", m_renderer.GetYAngle());
    (void)IniFileDataBaseWriteString(par_Section, "trans3d.ay", help, par_FileDescriptor);
    PrintFormatToString(help, sizeof(help), "%g", m_renderer.GetZAngle());
    (void)IniFileDataBaseWriteString(par_Section, "trans3d.az", help, par_FileDescriptor);
    PrintFormatToString(help, sizeof(help), "%g", m_renderer.GetDepth());
    (void)IniFileDataBaseWriteString(par_Section, "trans3d.k_depth", help, par_FileDescriptor);
    PrintFormatToString(help, sizeof(help), "%g", m_renderer.GetScale());
    (void)IniFileDataBaseWriteString(par_Section, "trans3d.scale", help, par_FileDescriptor);
    PrintFormatToString(help, sizeof(help), "%g", m_renderer.GetXOffset());
    (void)IniFileDataBaseWriteString(par_Section, "trans3d.x_off", help, par_FileDescriptor);
    PrintFormatToString(help, sizeof(help), "%g", m_renderer.GetYOffset());
    (void)IniFileDataBaseWriteString(par_Section, "trans3d.y_off", help, par_FileDescriptor);
    PrintFormatToString(help, sizeof(help), "%g", m_renderer.GetZOffset());
    (void)IniFileDataBaseWriteString(par_Section, "trans3d.z_off", help, par_FileDescriptor);

    // Persist the split as the two Qt pixel sizes derived from the last content height.
    const float contentHeight{(0.0f < m_lastContentHeight) ? m_lastContentHeight : 200.0f};
    m_splitterGraphicView = static_cast<int>(contentHeight * m_splitRatio);
    m_splitterTableView = static_cast<int>(contentHeight * (1.0f - m_splitRatio));
    (void)IniFileDataBaseWriteFloat(par_Section, "SplitterGraphicView", m_splitterGraphicView, par_FileDescriptor);
    (void)IniFileDataBaseWriteFloat(par_Section, "SplitterTableView", m_splitterTableView, par_FileDescriptor);
}

void A2LCalMapWindow::ReadConfig(int par_FileDescriptor, const char *par_Section)
{
    char buffer[256];
    (void)IniFileDataBaseReadString(par_Section, "process", "", buffer, sizeof(buffer), par_FileDescriptor);
    m_process = buffer;
    std::snprintf(m_processBuffer, sizeof(m_processBuffer), "%s", m_process.c_str());

    (void)IniFileDataBaseReadString(par_Section, "characteristic", "", buffer, sizeof(buffer), par_FileDescriptor);
    m_characteristic = buffer;
    std::snprintf(m_characteristicBuffer, sizeof(m_characteristicBuffer), "%s", m_characteristic.c_str());

    m_data.SetPhysical(0 != IniFileDataBaseReadYesNo(par_Section, "Pysical", 1, par_FileDescriptor));
    m_data.SetDecIncOffset(IniFileDataBaseReadFloat(par_Section, "DecIncOffset", 1.0, par_FileDescriptor));

    m_renderer.SetXAngle(IniFileDataBaseReadFloat(par_Section, "trans3d.ax", 0.0, par_FileDescriptor));
    m_renderer.SetYAngle(IniFileDataBaseReadFloat(par_Section, "trans3d.ay", 0.0, par_FileDescriptor));
    m_renderer.SetZAngle(IniFileDataBaseReadFloat(par_Section, "trans3d.az", -3.141592, par_FileDescriptor));
    m_renderer.UpdateSinCos();
    m_renderer.SetDepth(IniFileDataBaseReadFloat(par_Section, "trans3d.k_depth", 0.5, par_FileDescriptor));
    m_renderer.SetScale(IniFileDataBaseReadFloat(par_Section, "trans3d.scale", 1.0, par_FileDescriptor));
    m_renderer.SetXOffset(IniFileDataBaseReadFloat(par_Section, "trans3d.x_off", 0.4, par_FileDescriptor));
    m_renderer.SetYOffset(IniFileDataBaseReadFloat(par_Section, "trans3d.y_off", 0.0, par_FileDescriptor));
    m_renderer.SetZOffset(IniFileDataBaseReadFloat(par_Section, "trans3d.z_off", -0.3, par_FileDescriptor));

    m_splitterGraphicView = IniFileDataBaseReadInt(par_Section, "SplitterGraphicView", 150, par_FileDescriptor);
    m_splitterTableView = IniFileDataBaseReadInt(par_Section, "SplitterTableView", 50, par_FileDescriptor);
    const int total{m_splitterGraphicView + m_splitterTableView};
    m_splitRatio = (0 < total) ? (static_cast<float>(m_splitterGraphicView) / static_cast<float>(total)) : 0.75f;

    SetupProcessAndCharacteristic();
}

} // namespace XilEnvImGui
