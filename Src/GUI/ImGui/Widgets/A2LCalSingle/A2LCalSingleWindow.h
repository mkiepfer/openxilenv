/**
 * @file Src/GUI/ImGui/Widgets/A2LCalSingle/A2LCalSingleWindow.h
 * @brief ImGui port of the Qt A2L single-value calibration widget (read/write over the A2L link).
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_WIDGETS_A2LCALSINGLE_A2LCALSINGLEWINDOW_H
#define SRC_GUI_IMGUI_WIDGETS_A2LCALSINGLE_A2LCALSINGLEWINDOW_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Framework/A2LCalBridge.h"
#include "Framework/IWindowInstance.h"
#include "Framework/IWindowType.h"
#include "Framework/WindowContext.h"

extern "C" {
#include "A2LAccessData.h"
#include "A2LLink.h"
}

namespace XilEnvImGui
{

/**
 * @class A2LCalSingleWindowType
 * @brief Window type/factory for A2L single-value calibration windows.
 * @details Mirrors the Qt A2LCalSingleType metadata: type name "A2lSingleView", ini
 *          list "GUI/AllA2lSingleViewWindows" and the medium update rate.
 */
class A2LCalSingleWindowType : public IWindowType
{
public:
    [[nodiscard]] UpdateRate GetUpdateRate() const override
    {
        return UpdateRate::Medium;
    }
    [[nodiscard]] const char *GetTypeName() const override
    {
        return "A2lSingleView";
    }
    [[nodiscard]] const char *GetIniListName() const override
    {
        return "GUI/AllA2lSingleViewWindows";
    }
    [[nodiscard]] const char *GetMenuEntryName() const override
    {
        return "A2LCal single";
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
 * @class A2LCalSingleWindow
 * @brief One A2L single-value calibration window bound to exactly one linked process.
 * @details Immediate-mode counterpart of the Qt A2LCalSingleWidget + A2LCalSingleModel +
 *          A2LCalSingleData trio. Holds a list of A2L characteristics (labels) and their
 *          current calibration values fetched asynchronously over the A2L link. Reads
 *          (dir 0) and writes (dir 1) go through the shared A2LCalBridge fetch channel; the
 *          scheduler-thread acknowledgements are marshalled back and dispatched into
 *          OnGetDataFromLinkAck. Config round-trips to the Qt "GUI/Widgets/<name>" layout.
 * @warning All methods run on the ImGui render thread only; the A2L link C-API is internally
 *          synchronised, so no additional locking is required.
 */
class A2LCalSingleWindow : public IWindowInstance, public IA2LCalWidget
{
public:
    /**
     * @brief Constructs an A2L single-value window bound to its type and name.
     * @param[in] par_Type Owning window type. Must not be nullptr.
     * @param[in] par_Name Unique window name.
     */
    A2LCalSingleWindow(IWindowType *par_Type, std::string par_Name);
    ~A2LCalSingleWindow() override;

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
    /**
     * @struct Element
     * @brief One calibration characteristic row (counterpart of the Qt A2LCalSingleData).
     */
    struct Element
    {
        std::string name{};                 /*!< Characteristic (label) name. */
        int displayType{-1};                /*!< 0 dec, 1 hex, 2 bin, 3 phys (-1 = auto). */
        int linkNo{-1};                     /*!< Owning link number. */
        int index{-1};                      /*!< Characteristic index within the link. */
        A2L_DATA *pData{nullptr};           /*!< Owned link data descriptor (FreeA2lData). */
        bool exists{false};                 /*!< Whether the label resolved on the link. */
        bool hasChanged{true};              /*!< Dirty flag driving value-string refresh. */
        bool physical{false};               /*!< True when displayType == phys. */
        CHARACTERISTIC_AXIS_INFO info{};    /*!< Owned axis/conversion info (my_free on clear). */
        std::string valueStr{};             /*!< Rendered value string. */
        std::string unit{};                 /*!< Unit string. */
        char editBuffer[128]{};             /*!< In-place value editor buffer. */
        bool editing{false};               /*!< True while the value editor holds focus. */
    };

    void SetProcessName(const std::string &par_ProcessName);
    void SetupLinkTo();
    void CloseLink();
    void ClearElements();

    void AddCharacteristic(const std::string &par_Name, int par_Row, int par_DisplayType);
    bool CheckExistance(Element &par_Element);
    void SetDisplayType(Element &par_Element, int par_DisplayType);

    void UpdateAllValuesReq(bool par_UpdateAlways);
    void UpdateOneValueReq(int par_Row, bool par_UpdateAlways);
    void WriteValueToLink(Element &par_Element, const char *par_Text);

    /**
     * @brief Re-enumerates the linked process' single-value calibration characteristics.
     * @details Fills the settings-popup browser cache via A2LGetNextSymbolFromLink with the
     *          A2L_LABEL_TYPE_SINGLE_VALUE_CALIBRATION type mask (same enumeration the Qt
     *          A2LSelectWidget model uses). Resets the per-entry selection flags.
     * @note No-op when the window is not linked to a process (m_linkNo <= 0).
     */
    void RefreshCharacteristicBrowser();

    int ToString(Element &par_Element, bool par_UpdateAlways) const;
    std::string ConvertPhysToString(double par_RawValue, const CHARACTERISTIC_AXIS_INFO &par_Info) const;
    static bool IsWritable(const Element &par_Element);
    static bool IsSingleValue(const Element &par_Element);

    IWindowType *m_pType{nullptr};   /*!< Owning window type. */

    std::string m_processName{};     /*!< Linked process name (one process per window). */
    int m_linkNo{-1};                /*!< Resolved A2L link number (-1 when unlinked). */
    int m_dataFromLinkChannelNo{-1}; /*!< Async fetch channel (-1 when closed). */

    std::vector<Element> m_elements{}; /*!< Calibration characteristics shown as table rows. */

    bool m_showUnitColumn{false};         /*!< Show the unit column. */
    bool m_showDisplayTypeColumn{false};  /*!< Show the display-type column. */

    int m_icon{0};                        /*!< Persisted icon id (round-trip only). */
    std::uint32_t m_backgroundColor{0xFFFFFFu}; /*!< Persisted 0x00bbggrr color (round-trip only). */
    std::string m_fontString{};           /*!< Persisted "family, size" font (round-trip only). */
    // Qt readFromIni defaults: AlignVCenter|AlignLeft (name/display), |AlignRight (value),
    // |AlignHCenter (unit); AlignVCenter=0x80, AlignLeft=0x1, AlignRight=0x2, AlignHCenter=0x4.
    int m_columnAlignmentName{0x0081};        /*!< Persisted name-column alignment (round-trip only). */
    int m_columnAlignmentValue{0x0082};       /*!< Persisted value-column alignment (round-trip only). */
    int m_columnAlignmentUnit{0x0084};        /*!< Persisted unit-column alignment (round-trip only). */
    int m_columnAlignmentDisplayType{0x0081}; /*!< Persisted display-column alignment (round-trip only). */

    char m_addCharacteristicBuffer[256]{}; /*!< Settings text buffer for adding a label. */
    char m_processBuffer[256]{};           /*!< Settings text buffer for the process name. */
    char m_renameBuffer[256]{};            /*!< Settings text buffer for the window rename field. */
    char m_browserFilter[128]{};           /*!< Case-insensitive substring filter for the browser. */

    std::vector<std::string> m_browserCharacteristics{}; /*!< Cached A2L characteristics of the link. */
    std::vector<char> m_browserSelected{};               /*!< Parallel multi-select flags (0/1). */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_WIDGETS_A2LCALSINGLE_A2LCALSINGLEWINDOW_H
