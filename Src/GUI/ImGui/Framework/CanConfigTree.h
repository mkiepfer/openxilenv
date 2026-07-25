/**
 * @file Src/GUI/ImGui/Framework/CanConfigTree.h
 * @brief CAN variant/object/signal tree editor (ports the Qt CanConfigDialog tree).
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_FRAMEWORK_CANCONFIGTREE_H
#define SRC_GUI_IMGUI_FRAMEWORK_CANCONFIGTREE_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace XilEnvImGui
{

/**
 * @enum class CanByteOrder
 * @brief Byte order of a CAN object or signal (mirrors Qt @c ByteOrder).
 */
enum class CanByteOrder : int
{
    LsbFirst = 0, /*!< Intel / little-endian bit numbering. */
    MsbFirst = 1  /*!< Motorola / big-endian bit numbering. */
};

/**
 * @enum class CanSignalSign
 * @brief Numeric interpretation of the raw CAN bits (Qt @c Sign combo order).
 */
enum class CanSignalSign : int
{
    Signed = 0,   /*!< Two's-complement signed. */
    Unsigned = 1, /*!< Unsigned. */
    Float = 2     /*!< IEEE floating point. */
};

/**
 * @enum class CanSignalConversion
 * @brief Raw-to-physical conversion kind of a signal (Qt @c ConversionType).
 */
enum class CanSignalConversion : int
{
    MultiplyFactorAddOffset = 0, /*!< phys = factor * raw + offset ("mx+b"). */
    AddOffsetMultiplyFactor = 1, /*!< phys = factor * (raw + offset) ("m(x+b)"). */
    Curve = 2,                   /*!< Curve conversion (convstring). */
    Equation = 3                 /*!< Equation conversion (convstring). */
};

/**
 * @enum class CanSignalType
 * @brief Multiplex role of a signal (Qt @c SignalType).
 */
enum class CanSignalType : int
{
    Normal = 0,       /*!< Always present. */
    Multiplexed = 1,  /*!< This signal is the multiplexer. */
    MultiplexedBy = 2 /*!< Present only for a given multiplexer value. */
};

/**
 * @enum class CanObjectDirection
 * @brief Transmit direction of a CAN object (Qt @c Direction).
 */
enum class CanObjectDirection : int
{
    Write = 0,                /*!< Transmit ("write"). */
    Read = 1,                 /*!< Receive ("read"). */
    WriteVariableId = 2,      /*!< Transmit with variable id. */
    ReadStartupVariableId = 3 /*!< Receive with startup variable id. */
};

/**
 * @enum class CanObjectType
 * @brief Object protocol type (Qt @c CanConfigObject::Type).
 */
enum class CanObjectType : int
{
    Normal = 0,       /*!< Plain CAN object. */
    Multiplexed = 1,  /*!< Multiplexed object ("mux"). */
    J1939 = 2,        /*!< J1939 object. */
    J1939MultiPG = 3, /*!< J1939 multi C-PG container. */
    J1939CPG = 4      /*!< J1939 single C-PG. */
};

/**
 * @enum class CanObjectTransmit
 * @brief Transmit trigger of an object (Qt @c TransmitEventType).
 */
enum class CanObjectTransmit : int
{
    Cyclic = 0,  /*!< Cyclic transmit. */
    Equation = 1 /*!< Event/equation-triggered transmit. */
};

/**
 * @enum class CanObjectDestAddr
 * @brief J1939 destination address kind (Qt @c DstAddress).
 */
enum class CanObjectDestAddr : int
{
    Fixed = 0,   /*!< Fixed destination address. */
    Variable = 1 /*!< Destination address from a blackboard variable. */
};

/**
 * @enum class CanControlBbName
 * @brief Blackboard naming scheme of a variant (Qt @c ControlBlackboardName).
 */
enum class CanControlBbName : int
{
    PrefixId = 0,          /*!< "PrefixId". */
    PrefixObjName = 1,     /*!< "PrefixObjName". */
    NoPrefixObjName = 2,   /*!< "NoPrefixObjName". */
    PrefixVarObjName = 3,  /*!< "PrefixVarObjName". */
    NoPrefixVarObjName = 4 /*!< "NoPrefixVarObjName". */
};

/**
 * @struct CanTreeSignal
 * @brief One CAN signal, round-tripped to @c CAN/Variante_%i/Object_%i/Signal_%i.
 * @details Numeric conversion factor/offset/start-value fields are kept as their exact ini
 *          text ("convert"/"offset"/"startvalue") to preserve the FloatOrInt64 formatting.
 */
struct CanTreeSignal
{
    std::string name{};        /*!< "name". */
    std::string description{}; /*!< "desc". */
    std::string unit{};        /*!< "unit". */
    CanSignalConversion conversion{CanSignalConversion::MultiplyFactorAddOffset}; /*!< "convtype". */
    std::string factor{"1"};   /*!< "convert" (raw text). */
    std::string offset{"0"};   /*!< "offset" (raw text). */
    std::string convString{};  /*!< "convstring" (curve/equation). */
    int startBit{0};           /*!< "startbit". */
    int bitSize{8};            /*!< "bitsize". */
    CanByteOrder byteOrder{CanByteOrder::MsbFirst}; /*!< "byteorder" (default msb_first). */
    bool startValueEnable{true};   /*!< "startvalue active". */
    std::string startValue{"0"};   /*!< "startvalue" (raw text). */
    CanSignalType signalType{CanSignalType::Normal}; /*!< "type". */
    std::string muxBySignal{};     /*!< "mux_by_signal". */
    int muxStartBit{0};            /*!< "mux_startbit". */
    int muxBitSize{0};             /*!< "mux_bitsize". */
    int muxValue{0};               /*!< "mux_value". */
    std::string bbType{"UNKNOWN_DOUBLE"}; /*!< "bbtype". */
    CanSignalSign sign{CanSignalSign::Unsigned}; /*!< "sign". */
};

/**
 * @struct CanTreeObject
 * @brief One CAN object/message, round-tripped to @c CAN/Variante_%i/Object_%i.
 */
struct CanTreeObject
{
    std::string name{};        /*!< "name". */
    std::string description{}; /*!< "desc". */
    uint32_t identifier{0};    /*!< "id" (written as "0x%X"). */
    std::string variableId{};  /*!< "variable_id" (optional id formula). */
    bool extendedId{false};    /*!< "extended". */
    bool bitRateSwitch{false}; /*!< "bit_rate_switch". */
    bool fdFrameFormat{false}; /*!< "fd_frame_format_switch". */
    CanByteOrder byteOrder{CanByteOrder::LsbFirst}; /*!< "byteorder". */
    int size{1};               /*!< "size". */
    CanObjectDirection direction{CanObjectDirection::Read}; /*!< "direction". */
    CanObjectType type{CanObjectType::Normal};              /*!< "type". */
    bool variableDlc{false};              /*!< "variable_dlc" present. */
    std::string variableDlcName{};        /*!< "variable_dlc" value. */
    CanObjectDestAddr destAddrType{CanObjectDestAddr::Fixed}; /*!< J1939 dst addr kind. */
    uint32_t fixedDestAddr{0xFF};         /*!< "fixed_dst_addr". */
    std::string variableDestAddrName{};   /*!< "variable_dst_addr". */
    uint32_t variableDestAddrInit{0xFF};  /*!< "init_dst_addr". */
    int muxStartBit{0};                   /*!< "mux_startbit". */
    int muxBitSize{0};                    /*!< "mux_bitsize". */
    int muxValue{0};                      /*!< "mux_value". */
    CanObjectTransmit transmitType{CanObjectTransmit::Cyclic}; /*!< "CyclicOrEvent". */
    std::string transmitCycles{"1"};      /*!< "cycles". */
    std::string transmitDelay{"0"};       /*!< "delay". */
    std::string transmitEquation{};       /*!< "EventEquation". */
    std::vector<std::string> additionalVariables{}; /*!< "additional_variable_%i". */
    std::vector<std::string> equationsBefore{};     /*!< "equ_before_%i". */
    std::vector<std::string> equationsBehind{};     /*!< "equ_behind_%i". */
    uint64_t initValue{0};                /*!< "InitData" (written as "0x%llX"). */
    std::vector<int> cPgIds{};            /*!< "C_PGs" (J1939 multi C-PG). */
    std::vector<CanTreeSignal> signals{}; /*!< Child signals. */
};

/**
 * @struct CanTreeVariant
 * @brief One CAN variant, round-tripped to @c CAN/Variante_%i.
 */
struct CanTreeVariant
{
    std::string name{};        /*!< "name". */
    std::string description{}; /*!< "desc". */
    int baudRate{500};         /*!< "baud rate". */
    double samplePoint{0.75};  /*!< "sample point". */
    bool canFdEnable{false};   /*!< "can fd". */
    int dataBaudRate{4000};    /*!< "data baud rate". */
    double dataSamplePoint{0.8}; /*!< "data sample point". */
    bool enableJ1939{false};   /*!< "j1939". */
    CanControlBbName controlBbName{CanControlBbName::PrefixId}; /*!< "ControlBbName". */
    std::vector<CanTreeObject> objects{}; /*!< Child objects. */
    std::array<bool, 16> connectedChannels{}; /*!< Channel (1..N) connection flags. */
};

/**
 * @struct CanTreeChannel
 * @brief One CAN controller/channel row from @c CAN/Global.
 */
struct CanTreeChannel
{
    int startupState{1};       /*!< can_controllerN_startup_state. */
    int busConfig{1};          /*!< can_controllerN_baudrate_config. */
    bool fd{false};            /*!< can_controllerN_user_config_fd. */
    int baudRate{500};         /*!< can_controllerN_user_config_baud_rate. */
    double samplePoint{0.75};  /*!< can_controllerN_user_config_sample_point. */
    int dataBaudRate{4000};    /*!< can_controllerN_user_config_data_baud_rate. */
    double dataSamplePoint{0.8}; /*!< can_controllerN_user_config_data_sample_point. */
};

/**
 * @enum class CanTreeSelectionKind
 * @brief Which tree level the current selection refers to.
 */
enum class CanTreeSelectionKind : int
{
    None = 0,    /*!< Nothing selected. */
    Variant = 1, /*!< A variant is selected. */
    Object = 2,  /*!< An object is selected. */
    Signal = 3   /*!< A signal is selected. */
};

/**
 * @enum class CanTreePendingOp
 * @brief Structural tree mutation deferred until after the render pass.
 * @details Deferring keeps container iteration valid while a context menu triggers add/remove.
 */
enum class CanTreePendingOp : int
{
    None = 0,       /*!< No pending mutation. */
    AddVariant,     /*!< Append a new variant. */
    DeleteVariant,  /*!< Delete variant m_pendingVariant. */
    CopyVariant,    /*!< Copy variant m_pendingVariant to the clipboard. */
    PasteVariant,   /*!< Append the clipboard variant. */
    AddObject,      /*!< Append a new object to variant m_pendingVariant. */
    DeleteObject,   /*!< Delete object (m_pendingVariant,m_pendingObject). */
    CopyObject,     /*!< Copy object to the clipboard. */
    PasteObject,    /*!< Append the clipboard object to variant m_pendingVariant. */
    AddSignal,      /*!< Append a new signal to object (m_pendingVariant,m_pendingObject). */
    DeleteSignal,   /*!< Delete signal (m_pendingVariant,m_pendingObject,m_pendingSignal). */
    CopySignal,     /*!< Copy signal to the clipboard. */
    PasteSignal     /*!< Append the clipboard signal to object (m_pendingVariant,m_pendingObject). */
};

/**
 * @enum class CanTreeFileAction
 * @brief Pending inline file-picker action for variant/object import/export.
 */
enum class CanTreeFileAction : int
{
    None = 0,        /*!< No pending picker. */
    ImportVariant,   /*!< Import a variant from a .can file. */
    ExportVariant,   /*!< Export the selected variant to a .can file. */
    ImportObject,    /*!< Import an object into the selected variant. */
    ExportObject     /*!< Export the selected object to a .can file. */
};

/**
 * @class CanConfigTreeManager
 * @brief Owns and renders the CAN variant/object/signal tree editor.
 * @details Process-wide singleton, render-thread only. Opening snapshots the @c CAN sections into
 *          the in-memory model; OK deletes every stale @c CAN/Variante* section and re-writes the
 *          model through the IniDataBase C-API using the exact section/entry keys of the Qt dialog,
 *          so configs round-trip with Qt and the existing global CAN dialog.
 * @warning Not thread-safe; all methods must be called from the render thread.
 */
class CanConfigTreeManager
{
public:
    /**
     * @brief Returns the process-wide singleton instance.
     * @return Reference to the single CanConfigTreeManager.
     */
    static CanConfigTreeManager &GetInstance();

    /** @brief Snapshots the CAN sections and opens the tree-editor modal. */
    void OpenEditor();

    /** @brief Renders the editor and every managed sub-picker for the current frame. */
    void Render();

private:
    CanConfigTreeManager() = default;

    void SnapshotFromIni();
    void ApplyToIni();

    void RenderTree();
    void RenderProperties();
    void RenderVariantProperties(CanTreeVariant &par_Variant);
    void RenderObjectProperties(CanTreeObject &par_Object);
    void RenderSignalProperties(CanTreeSignal &par_Signal);
    void RenderChannelMapping();
    void RenderFilePicker();

    void OpenFilePicker(CanTreeFileAction par_Action);
    void ExecuteFileAction(const std::string &par_Path);
    void ImportVariantFromFile(const std::string &par_Path);
    void ExportVariantToFile(const std::string &par_Path);
    void ImportObjectFromFile(const std::string &par_Path);
    void ExportObjectToFile(const std::string &par_Path);

    void SelectVariant(int par_Variant);
    void SelectObject(int par_Variant, int par_Object);
    void SelectSignal(int par_Variant, int par_Object, int par_Signal);

    void RequestOp(CanTreePendingOp par_Op, int par_Variant, int par_Object, int par_Signal);
    void ApplyPendingOp();

    std::vector<CanTreeVariant> m_variants{}; /*!< The edited variant tree. */
    std::array<CanTreeChannel, 16> m_channels{}; /*!< Per-channel CAN/Global rows. */

    int m_numberOfChannels{1};        /*!< can_controller_count(_ext). */
    bool m_doNotUseUnits{false};      /*!< "dont use units for blackboard". */
    bool m_restartImmediately{false}; /*!< "restart CAN immediately". */
    bool m_enableGatewayDeviceDriver{false}; /*!< "enable gateway device driver". */
    int m_enableGatewayVirtual{1};    /*!< "enable gateway virtual device driver". */
    int m_enableGatewayReal{1};       /*!< "enable gateway real device driver". */
    std::string m_enableGatewayVirtualEqu{}; /*!< "...virtual device driver equ". */
    std::string m_enableGatewayRealEqu{};    /*!< "...real device driver equ". */

    CanTreeSelectionKind m_selectionKind{CanTreeSelectionKind::None}; /*!< Selected level. */
    int m_selVariant{-1}; /*!< Selected variant index (-1 none). */
    int m_selObject{-1};  /*!< Selected object index (-1 none). */
    int m_selSignal{-1};  /*!< Selected signal index (-1 none). */

    bool m_open{false};          /*!< Editor modal visible. */
    bool m_openRequested{false}; /*!< OpenPopup pending. */

    CanTreePendingOp m_pendingOp{CanTreePendingOp::None}; /*!< Deferred structural mutation. */
    int m_pendingVariant{-1}; /*!< Variant index for the pending op. */
    int m_pendingObject{-1};  /*!< Object index for the pending op. */
    int m_pendingSignal{-1};  /*!< Signal index for the pending op. */

    CanTreeSelectionKind m_clipboardKind{CanTreeSelectionKind::None}; /*!< Clipboard content kind. */
    CanTreeVariant m_clipboardVariant{}; /*!< Copied variant. */
    CanTreeObject m_clipboardObject{};   /*!< Copied object. */
    CanTreeSignal m_clipboardSignal{};   /*!< Copied signal. */

    // Inline .can file picker.
    bool m_filePickerOpen{false};          /*!< File picker visible. */
    bool m_filePickerOpenRequested{false}; /*!< File picker OpenPopup pending. */
    bool m_filePickerSaveMode{false};      /*!< true: save (editable name). */
    std::string m_filePickerDir{};         /*!< Current directory. */
    std::array<char, 512> m_filePickerName{}; /*!< Editable file name (save mode). */
    std::string m_filePickerTitle{};       /*!< File picker window title. */
    CanTreeFileAction m_filePickerAction{CanTreeFileAction::None}; /*!< Action on confirm. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_FRAMEWORK_CANCONFIGTREE_H
