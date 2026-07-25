/**
 * @file Src/GUI/ImGui/Widgets/A2LCalMap/A2LCalMapWindow.h
 * @brief ImGui port of the Qt A2L MAP/CURVE calibration editor (table + 3D surface over the link).
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_WIDGETS_A2LCALMAP_A2LCALMAPWINDOW_H
#define SRC_GUI_IMGUI_WIDGETS_A2LCALMAP_A2LCALMAPWINDOW_H

#include <cstdint>
#include <memory>
#include <string>

#include "Framework/A2LCalBridge.h"
#include "Framework/IWindowInstance.h"
#include "Framework/IWindowType.h"
#include "Framework/WindowContext.h"

#include "Widgets/A2LCalMap/A2LCalMap3DRenderer.h"
#include "Widgets/A2LCalMap/A2LCalMapData.h"

namespace XilEnvImGui
{

/**
 * @class A2LCalMapWindowType
 * @brief Window type/factory for A2L MAP/CURVE calibration windows.
 * @details Mirrors the Qt A2LCalMapType metadata: type name "A2lMapView", ini list
 *          "GUI/AllA2lMapViewWindows" and the slow update rate.
 */
class A2LCalMapWindowType : public IWindowType
{
public:
    [[nodiscard]] UpdateRate GetUpdateRate() const override
    {
        return UpdateRate::Slow;
    }
    [[nodiscard]] const char *GetTypeName() const override
    {
        return "A2lMapView";
    }
    [[nodiscard]] const char *GetIniListName() const override
    {
        return "GUI/AllA2lMapViewWindows";
    }
    [[nodiscard]] const char *GetMenuEntryName() const override
    {
        return "A2LCal map";
    }
    [[nodiscard]] bool HasStorage() const override
    {
        return false;
    }
    [[nodiscard]] std::unique_ptr<IWindowInstance> CreateNew(const std::string &par_Title) override;
    [[nodiscard]] std::unique_ptr<IWindowInstance> CreateFromConfig(int par_FileDescriptor,
                                                                    const char *par_Section,
                                                                    const std::string &par_Name) override;
};

/**
 * @class A2LCalMapWindow
 * @brief One A2L MAP/CURVE calibration window bound to a single characteristic on one process.
 * @details Immediate-mode counterpart of the Qt A2LCalMapWidget + A2LCalMapModel + A2LCalMapData +
 *          A2LCalMap3DView. The upper region shows the software 3D surface (A2LCalMap3DRenderer),
 *          the lower region an editable value table. Reads (dir 0) and writes (dir 1) flow through
 *          the shared A2LCalBridge fetch channel; scheduler-thread acknowledgements are marshalled
 *          back and dispatched into OnGetDataFromLinkAck. Config round-trips to the Qt
 *          "GUI/Widgets/<name>" layout with identical keys.
 * @warning All methods run on the ImGui render thread only; the A2L link C-API is internally
 *          synchronised, so no additional locking is required.
 */
class A2LCalMapWindow : public IWindowInstance, public IA2LCalWidget
{
public:
    /**
     * @brief Constructs an A2L MAP/CURVE window bound to its type and name.
     * @param[in] par_Type Owning window type. Must not be nullptr.
     * @param[in] par_Name Unique window name.
     */
    A2LCalMapWindow(IWindowType *par_Type, std::string par_Name);
    ~A2LCalMapWindow() override;

    [[nodiscard]] IWindowType *GetType() const override
    {
        return m_pType;
    }
    void RenderContent(const WindowContext &par_Context) override;
    void OnUpdateTick(const WindowContext &par_Context) override;
    void RenderSettingsPopup() override;
    void WriteConfig(int par_FileDescriptor, const char *par_Section) override;
    void ReadConfig(int par_FileDescriptor, const char *par_Section) override;
    void OnAcceptVariableDrop(const std::string &par_VariableName) override;

    // IA2LCalWidget (render-thread dispatch from the A2LCalBridge).
    void OnLinkDataChanged(int par_LinkNo, int par_Index, A2L_DATA *par_Data) override;
    void OnProcessStartStop(const std::string &par_ProcessName, int par_Flags, int par_Action) override;
    void OnGetDataFromLinkAck(void *par_IndexData, int par_FetchDataChannelNo) override;

private:
    void SetupProcessAndCharacteristic();
    void RenderTable(const ImVec2 &par_Size);
    void Render3DView(const ImVec2 &par_Size);
    void CommitAxisEdit();

    IWindowType *m_pType{nullptr}; /*!< Owning window type. */

    A2LCalMapData m_data{};              /*!< Fetched characteristic + link/channel state. */
    A2LCalMap3DRenderer m_renderer{};    /*!< Software 3D surface renderer. */

    std::string m_process{};        /*!< Bound process name. */
    std::string m_characteristic{}; /*!< Bound characteristic label. */

    float m_splitRatio{0.75f};  /*!< Fraction of the content height used by the 3D view. */
    int m_splitterGraphicView{0}; /*!< Persisted 3D-view pixel height (Qt key). */
    int m_splitterTableView{0};   /*!< Persisted table pixel height (Qt key). */
    float m_lastContentHeight{0.0f}; /*!< Last content-region height (for splitter round-trip). */

    int m_editAxis{-1};        /*!< Axis currently being edited (-1 none). */
    int m_editX{-1};           /*!< X index of the cell being edited. */
    int m_editY{-1};           /*!< Y index of the cell being edited. */
    char m_editBuffer[128]{};  /*!< In-place value editor buffer. */

    char m_processBuffer[256]{};        /*!< Settings text buffer for the process name. */
    char m_characteristicBuffer[256]{}; /*!< Settings text buffer for the characteristic name. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_WIDGETS_A2LCALMAP_A2LCALMAPWINDOW_H
