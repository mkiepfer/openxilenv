/**
 * @file Src/GUI/ImGui/Framework/BusConfigDialogs.h
 * @brief Menu-driven CAN / CCP / XCP / XCP-over-Ethernet configuration and DBC-import dialogs.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_FRAMEWORK_BUSCONFIGDIALOGS_H
#define SRC_GUI_IMGUI_FRAMEWORK_BUSCONFIGDIALOGS_H

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace XilEnvImGui
{

/**
 * @brief Number of protocol connections/targets configurable per bus protocol.
 * @details Mirrors the four fixed target tabs of the Qt CCP/XCP/XCP-over-Ethernet dialogs.
 */
constexpr int BUS_CONNECTION_COUNT{4};

/**
 * @struct VarLabelEditorState
 * @brief Edit buffers of the shared measurement/calibration label editor used by CCP and XCP.
 * @details Reproduces the Qt "Data type / Name / Address / Unit / Conversion / Min-Max" row editor;
 *          a line is serialised as @c "type,name,address[,unit][,conversion][,min,max]".
 */
struct VarLabelEditorState
{
    int dataTypeIndex{0};              /*!< Index into the BYTE..DOUBLE data-type combo. */
    std::array<char, 256> name{};      /*!< Blackboard label name. */
    std::array<char, 128> address{};   /*!< Target address (hex text). */
    std::array<char, 64> unit{};       /*!< Optional unit string. */
    std::array<char, 256> conversion{};/*!< Optional conversion formula. */
    std::array<char, 64> minValue{};   /*!< Optional minimum value. */
    std::array<char, 64> maxValue{};   /*!< Optional maximum value. */
    bool useUnit{false};               /*!< Unit column enabled (also stored as UseUnit). */
    bool useConversion{false};         /*!< Conversion column enabled (also stored as UseConversion). */
    bool useMinMax{false};             /*!< Min/Max columns enabled (also stored as UseMinMax). */
};

/**
 * @struct CcpTargetState
 * @brief Per-target CCP configuration edit state (section "CCP Configuration for Target N").
 */
struct CcpTargetState
{
    int protocolVersionIndex{1};       /*!< 0:2.0 1:2.1 2:2.1 ESV1 3:2.1 S_STATUS. */
    int byteOrderIndex{0};             /*!< 0:lsb_first 1:msb_first. */
    int channel{0};                    /*!< CAN channel index. */
    bool debug{false};                 /*!< Debug messages (ini "Debug"). */
    int timeout{1000};                 /*!< Command timeout [ms]. */
    int eventChannel{0};               /*!< DAQ event channel number. */
    int prescaler{1};                  /*!< DAQ prescaler. */
    bool extIds{false};                /*!< Extended (29-bit) identifiers. */
    std::array<char, 32> croId{};      /*!< CRO CAN id (hex text). */
    std::array<char, 32> crmId{};      /*!< CRM CAN id (hex text). */
    std::array<char, 32> dtoId{};      /*!< DTO CAN id (hex text). */
    std::array<char, 32> stationAddress{}; /*!< CCP station address (hex text). */
    bool seedAndKey{false};            /*!< Seed & key enabled. */
    std::array<char, 512> seedKeyDll{};/*!< DAQ seed&key DLL path. */
    std::array<char, 512> seedKeyDllCal{}; /*!< Calibration seed&key DLL path. */
    bool calibrationEnable{false};     /*!< Calibration enabled. */
    bool moveRomToRam{false};          /*!< Move ROM to RAM on connect. */
    bool setCalibrationPage{false};    /*!< Select calibration page. */
    bool readParameterAfterCalib{false};/*!< Read parameters after calibration. */
    std::array<char, 32> calibRomAddr{};/*!< Calibration ROM start address (hex text). */
    std::array<char, 32> calibRamAddr{};/*!< Calibration RAM start address (hex text). */
    std::array<char, 32> calibRomRamSize{}; /*!< Calibration ROM/RAM size (hex text). */
    int onlyBytePointerInDaq{0};       /*!< Preserved ini flag (no dedicated control). */
    std::vector<std::string> measurements{}; /*!< Measurement list entries (v%i). */
    std::vector<std::string> calibrations{};  /*!< Calibration list entries (p%i). */
    int measurementCountOld{0};        /*!< Original measurement count for stale-entry cleanup. */
    int calibrationCountOld{0};        /*!< Original calibration count for stale-entry cleanup. */
    VarLabelEditorState editor{};      /*!< Shared label editor state. */
    int activeTab{0};                  /*!< 0:measurements 1:calibrations. */
    int selectedRow{-1};               /*!< Selected list row (-1 none). */
};

/**
 * @struct XcpTargetState
 * @brief Per-target XCP configuration edit state (section "XCP Configuration for Target N").
 */
struct XcpTargetState
{
    int byteOrderIndex{0};             /*!< 0:lsb_first 1:msb_first. */
    int channel{0};                    /*!< CAN channel index. */
    bool debug{false};                 /*!< Debug messages (ini "DebugMessages"). */
    int timeout{1000};                 /*!< Command timeout [ms]. */
    int eventChannel{0};               /*!< DAQ event channel number. */
    int prescaler{1};                  /*!< DAQ prescaler. */
    bool extIds{false};                /*!< Extended (29-bit) identifiers. */
    std::array<char, 32> croId{};      /*!< CRO CAN id (hex text). */
    std::array<char, 32> crmId{};      /*!< CRM CAN id (hex text). */
    std::array<char, 32> dtoId{};      /*!< DTO CAN id (hex text). */
    std::array<char, 512> seedKeyDll{};/*!< Seed & key DLL path. */
    bool calibrationEnable{true};      /*!< Calibration enabled. */
    bool moveRomToRam{true};           /*!< Move ROM to RAM on connect. */
    bool setCalibrationPage{true};     /*!< Select calibration page. */
    bool readParameterAfterCalib{false};/*!< Read parameters after calibration. */
    std::array<char, 32> calibRomSegment{}; /*!< Calibration ROM segment (hex text). */
    std::array<char, 32> calibRomPage{};    /*!< Calibration ROM page number (hex text). */
    std::array<char, 32> calibRamSegment{}; /*!< Calibration RAM segment (hex text). */
    std::array<char, 32> calibRamPage{};    /*!< Calibration RAM page number (hex text). */
    int onlyBytePointerInDaq{0};       /*!< Preserved ini flag (no dedicated control). */
    std::vector<std::string> measurements{}; /*!< Measurement list entries (v%i). */
    std::vector<std::string> calibrations{};  /*!< Calibration list entries (p%i). */
    int measurementCountOld{0};        /*!< Original measurement count for stale-entry cleanup. */
    int calibrationCountOld{0};        /*!< Original calibration count for stale-entry cleanup. */
    VarLabelEditorState editor{};      /*!< Shared label editor state. */
    int activeTab{0};                  /*!< 0:measurements 1:calibrations. */
    int selectedRow{-1};               /*!< Selected list row (-1 none). */
};

/**
 * @struct XcpEthTargetState
 * @brief Per-target XCP-over-Ethernet edit state ("XCP over Ethernet Configuration for Target N").
 */
struct XcpEthTargetState
{
    bool enable{false};                /*!< Connection enabled. */
    std::array<char, 256> identification{}; /*!< Identification string. */
    std::array<char, 256> associatedProcess{}; /*!< Associated process name. */
    std::array<char, 256> dataSegmentName{};   /*!< Calibration data segment name. */
    std::array<char, 256> codeSegmentName{};   /*!< Code segment name. */
    int port{1802};                    /*!< TCP/UDP port. */
    bool addressTranslationForDlls{false}; /*!< Address translation for DLL processes. */
    bool debugFile{false};             /*!< Write debug file. */
    std::array<char, 512> debugFileName{}; /*!< Debug file path. */
    std::vector<std::string> tasks{};  /*!< Measurement task entries (daq_%i). */
    int taskCountOld{0};               /*!< Original task count for stale-entry cleanup. */
    int timestampIndex{0};             /*!< Timestamp combo index. */
    std::array<char, 256> taskName{};  /*!< Editor: task name. */
    std::array<char, 64> taskCycle{};  /*!< Editor: cycle. */
    std::array<char, 64> taskPrio{};   /*!< Editor: priority. */
    int selectedRow{-1};               /*!< Selected list row (-1 none). */
};

/**
 * @struct CanChannelState
 * @brief One CAN controller/channel row in the CAN global bus configuration.
 */
struct CanChannelState
{
    int startupState{1};               /*!< can_controllerN_startup_state. */
    int busConfig{1};                  /*!< can_controllerN_baudrate_config. */
    bool fd{false};                    /*!< can_controllerN_user_config_fd (USER config). */
    int baudRate{500};                 /*!< can_controllerN_user_config_baud_rate. */
    double samplePoint{0.75};          /*!< can_controllerN_user_config_sample_point. */
    int dataBaudRate{4000};            /*!< can_controllerN_user_config_data_baud_rate. */
    double dataSamplePoint{0.8};       /*!< can_controllerN_user_config_data_sample_point. */
    std::string varianteList{};        /*!< Preserved can_controllerN_variante assignment. */
};

/**
 * @struct CanGlobalState
 * @brief CAN global bus configuration edit state (section "CAN/Global").
 */
struct CanGlobalState
{
    int numberOfChannels{1};           /*!< can_controller_count(_ext). */
    bool dontUseUnits{false};          /*!< "dont use units for blackboard". */
    bool restartImmediately{false};    /*!< "restart CAN immediately". */
    bool enableGatewayDeviceDriver{false}; /*!< "enable gateway device driver". */
    bool enableGatewayVirtual{true};   /*!< "enable gateway virtual device driver". */
    bool enableGatewayReal{true};      /*!< "enable gateway real device driver". */
    std::array<CanChannelState, 16> channels{}; /*!< Per-channel configuration. */
};

/**
 * @struct DbcImportState
 * @brief DBC-import options edit state (mirrors the Qt DbcImportDialog).
 */
struct DbcImportState
{
    std::string filePath{};            /*!< Source .dbc file. */
    std::vector<std::string> available{}; /*!< Available bus members. */
    std::vector<std::string> transmit{};  /*!< Selected transmit members. */
    std::vector<std::string> receive{};   /*!< Selected receive members. */
    int availableSel{-1};              /*!< Selected available row. */
    int transmitSel{-1};               /*!< Selected transmit row. */
    int receiveSel{-1};                /*!< Selected receive row. */
    bool userDefinedName{false};       /*!< Use user-defined variant name. */
    std::array<char, 256> variantName{}; /*!< User-defined variant name. */
    bool additionalPrefix{false};      /*!< Add signal prefix. */
    std::array<char, 128> prefix{};    /*!< Signal name prefix. */
    bool additionalPostfix{false};     /*!< Add signal postfix. */
    std::array<char, 128> postfix{};   /*!< Signal name postfix. */
    bool extendSignalWithObject{false};/*!< Extend signal name with object name. */
    bool objectNameDoublePoint{false}; /*!< Object name with double point. */
    bool transferSettings{false};      /*!< Transfer settings from an existing variant. */
    int transferVariantIndex{-1};      /*!< Existing variant index for transfer. */
    std::vector<std::string> variantNames{}; /*!< Existing variant names for transfer combo. */
    bool objectAdditionalEquations{false}; /*!< Copy object equations. */
    bool objectInitData{false};        /*!< Copy object init data. */
    bool signalInitValues{false};      /*!< Copy signal init values. */
    bool signalDataType{false};        /*!< Copy signal data type. */
    bool signalEquations{false};       /*!< Copy signal equations. */
    bool sortSignals{false};           /*!< Sort signals. */
    bool exportToFile{false};          /*!< Also export to a .can file. */
    std::array<char, 512> exportFileName{}; /*!< Export .can file path. */
};

/**
 * @enum class BusFileAction
 * @brief Pending inline-file-picker action dispatched once a file is chosen.
 */
enum class BusFileAction : int
{
    None,            /*!< No pending picker. */
    CcpImport,       /*!< Load a .ccp config into a connection. */
    CcpExport,       /*!< Save a connection's measurement/calibration list to .ccp. */
    XcpImport,       /*!< Load a .xcp config into a connection. */
    XcpExport,       /*!< Save a connection's list to .xcp. */
    DbcSelect,       /*!< Select a .dbc file to import. */
    DbcExport        /*!< Choose the .can export target for DBC import. */
};

/**
 * @class BusConfigDialogManager
 * @brief Owns and renders the CAN/CCP/XCP/XCP-over-Ethernet configuration and DBC-import dialogs.
 * @details Process-wide singleton, render-thread only. Each Open* method snapshots the relevant
 *          ini sections into edit buffers and shows a modal; OK writes the values back through the
 *          IniDataBase C-API using the exact section/entry keys of the Qt dialogs and, where the Qt
 *          path does so, pushes the change to the running CAN server or CCP/XCP subsystem.
 * @warning Not thread-safe; all methods must be called from the render thread.
 */
class BusConfigDialogManager
{
public:
    /**
     * @brief Returns the process-wide singleton instance.
     * @return Reference to the single BusConfigDialogManager.
     */
    static BusConfigDialogManager &GetInstance();

    /** @brief Snapshots CAN/Global and opens the CAN bus configuration modal. */
    void OpenCanConfig();
    /** @brief Opens the inline .dbc file picker that leads into the DBC-import modal. */
    void OpenDbcImport();
    /** @brief Snapshots the four CCP targets and opens the CCP configuration modal. */
    void OpenCcpConfig();
    /** @brief Snapshots the four XCP targets and opens the XCP configuration modal. */
    void OpenXcpConfig();
    /** @brief Snapshots the four XCP-over-Ethernet targets and opens their modal. */
    void OpenXcpOverEthernetConfig();
    /** @brief Opens the CCP import connection/file picker. */
    void OpenCcpImport();
    /** @brief Opens the CCP export connection/file picker. */
    void OpenCcpExport();
    /** @brief Opens the XCP import connection/file picker. */
    void OpenXcpImport();
    /** @brief Opens the XCP export connection/file picker. */
    void OpenXcpExport();

    /** @brief Renders every managed modal for the current frame. */
    void Render();

private:
    BusConfigDialogManager() = default;

    void SnapshotCcp();
    void SnapshotXcp();
    void SnapshotXcpEth();
    void SnapshotCanGlobal();
    void ApplyCcp();
    void ApplyXcp();
    void ApplyXcpEth();
    void ApplyCanGlobal();

    void RenderCanConfig();
    void RenderCcpConfig();
    void RenderXcpConfig();
    void RenderXcpOverEthernetConfig();
    void RenderDbcImport();
    void RenderConnectionFilePicker();
    void RenderFilePicker();

    /** @brief Draws the shared measurement/calibration label list + editor for one CCP/XCP target. */
    void RenderVarListEditor(const char *par_Id, VarLabelEditorState &par_Editor,
                             std::vector<std::string> &par_Measurements,
                             std::vector<std::string> &par_Calibrations, int &par_ActiveTab,
                             int &par_SelectedRow);

    std::array<CcpTargetState, BUS_CONNECTION_COUNT> m_ccp{};   /*!< CCP target edit state. */
    std::array<XcpTargetState, BUS_CONNECTION_COUNT> m_xcp{};   /*!< XCP target edit state. */
    std::array<XcpEthTargetState, BUS_CONNECTION_COUNT> m_xcpEth{}; /*!< XCP-over-Ethernet state. */
    CanGlobalState m_can{};                                      /*!< CAN global edit state. */
    DbcImportState m_dbc{};                                      /*!< DBC-import edit state. */

    int m_activeCcpTab{0};    /*!< Selected CCP target tab. */
    int m_activeXcpTab{0};    /*!< Selected XCP target tab. */
    int m_activeXcpEthTab{0}; /*!< Selected XCP-over-Ethernet target tab. */

    bool m_canOpen{false};              /*!< CAN modal visible. */
    bool m_canOpenRequested{false};     /*!< CAN OpenPopup pending. */
    bool m_ccpOpen{false};              /*!< CCP modal visible. */
    bool m_ccpOpenRequested{false};     /*!< CCP OpenPopup pending. */
    bool m_xcpOpen{false};              /*!< XCP modal visible. */
    bool m_xcpOpenRequested{false};     /*!< XCP OpenPopup pending. */
    bool m_xcpEthOpen{false};           /*!< XCP-over-Ethernet modal visible. */
    bool m_xcpEthOpenRequested{false};  /*!< XCP-over-Ethernet OpenPopup pending. */
    bool m_dbcOpen{false};              /*!< DBC-import modal visible. */
    bool m_dbcOpenRequested{false};     /*!< DBC-import OpenPopup pending. */

    // Connection selector (CCP/XCP import/export) modal.
    bool m_connPickerOpen{false};       /*!< Connection selector visible. */
    bool m_connPickerOpenRequested{false}; /*!< Connection selector OpenPopup pending. */
    int m_connPickerConnection{0};      /*!< Chosen connection (0..3). */
    BusFileAction m_connPickerAction{BusFileAction::None}; /*!< Action after connection selection. */
    std::string m_connPickerTitle{};    /*!< Connection selector window title. */

    // Inline file picker.
    bool m_filePickerOpen{false};       /*!< File picker visible. */
    bool m_filePickerOpenRequested{false}; /*!< File picker OpenPopup pending. */
    bool m_filePickerSaveMode{false};   /*!< true: save (editable name), false: open. */
    std::string m_filePickerDir{};      /*!< Current directory. */
    std::string m_filePickerExt{};      /*!< Extension filter (e.g. ".ccp"). */
    std::array<char, 512> m_filePickerName{}; /*!< Editable file name (save mode). */
    std::string m_filePickerTitle{};    /*!< File picker window title. */
    BusFileAction m_filePickerAction{BusFileAction::None}; /*!< Action to dispatch on confirm. */
    int m_filePickerConnection{0};      /*!< Connection index the picked file applies to. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_FRAMEWORK_BUSCONFIGDIALOGS_H
