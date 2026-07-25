/**
 * @file Src/GUI/ImGui/Widgets/CanMessage/CanMessageWindow.h
 * @brief ImGui port of the Qt CAN-message widget: a live table of received CAN frames.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_WIDGETS_CANMESSAGE_CANMESSAGEWINDOW_H
#define SRC_GUI_IMGUI_WIDGETS_CANMESSAGE_CANMESSAGEWINDOW_H

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "Framework/IWindowInstance.h"
#include "Framework/IWindowType.h"
#include "Framework/WindowContext.h"

extern "C" {
#include "CanFifo.h"
}

namespace XilEnvImGui
{

/**
 * @class CanMessageWindowType
 * @brief Window type/factory for CAN-message windows (mirrors the Qt CANMessageWindowType).
 */
class CanMessageWindowType : public IWindowType
{
public:
    [[nodiscard]] UpdateRate GetUpdateRate() const override
    {
        return UpdateRate::Medium;
    }
    [[nodiscard]] const char *GetTypeName() const override
    {
        return "CANMessages";
    }
    [[nodiscard]] const char *GetIniListName() const override
    {
        return "GUI/AllCanMessageWindows";
    }
    [[nodiscard]] const char *GetMenuEntryName() const override
    {
        return "CAN messages";
    }
    [[nodiscard]] bool HasStorage() const override
    {
        return true;
    }
    [[nodiscard]] std::unique_ptr<IWindowInstance> CreateNew(const std::string &par_Title) override;
    [[nodiscard]] std::unique_ptr<IWindowInstance> CreateFromConfig(int par_FileDescriptor,
                                                                    const char *par_Section,
                                                                    const std::string &par_Name) override;
};

/**
 * @class CanMessageWindow
 * @brief One CAN-message window: aggregates received frames into per-identifier rows.
 * @details Owns a private CAN FIFO (CreateCanFifos) with the configured acceptance
 *          masks and drains it at the medium update rate through the CanFifo C-API,
 *          exactly like the Qt CANMessageWindowWidget. Each unique (channel, id,
 *          frame-type) triple becomes one sorted table row carrying its counter,
 *          absolute/differential timestamps and the latest payload bytes. Config
 *          round-trips to the Qt "GUI/Widgets/<name>" section layout.
 * @warning All methods run on the render thread only; the CanFifo API is internally
 *          synchronised, so no additional locking is required.
 */
class CanMessageWindow : public IWindowInstance
{
public:
    /**
     * @brief Constructs a CAN-message window bound to its type and name.
     * @param[in] par_Type Owning window type. Must not be nullptr.
     * @param[in] par_Name Unique window name.
     */
    CanMessageWindow(IWindowType *par_Type, std::string par_Name);
    ~CanMessageWindow() override;

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

private:
    /**
     * @struct MessageLine
     * @brief One aggregated CAN identifier row (counterpart of the Qt CAN_MESSAGE_LINE).
     */
    struct MessageLine
    {
        std::int32_t channel{0};       /*!< CAN channel. */
        std::uint32_t id{0};           /*!< CAN identifier. */
        std::uint8_t ext{0};           /*!< Frame-type/ext byte (0..7, 0x10 = J1939MP). */
        std::uint8_t node{0};          /*!< 1 -> transmitted by us, 0 -> received. */
        std::uint16_t size{0};         /*!< Payload length in bytes. */
        std::vector<std::uint8_t> data{}; /*!< Latest payload bytes. */
        std::uint64_t counter{0};      /*!< Number of frames seen. */
        double lastTime{0.0};          /*!< Last timestamp in milliseconds. */
        double dT{0.0};                /*!< Last inter-arrival time in milliseconds. */
        double dTMin{0.0};             /*!< Minimum inter-arrival time. */
        double dTMax{0.0};             /*!< Maximum inter-arrival time. */
    };

    void CreateFifo();
    void DestroyFifo();
    void FlushFifo();
    void ApplyAcceptanceMasks();
    void ClearLines();
    double TimestampCalc(const CAN_FIFO_ELEM_HEADER *par_Message) const;
    void ReadAndProcessMessages();
    MessageLine &FindOrInsertLine(const CAN_FIFO_ELEM_HEADER *par_Message);
    void RecordMessages(int par_ReadElements, const CAN_FIFO_ELEM_HEADER *par_Messages,
                        std::uint8_t **par_DataPtrs);
    void CloseRecorderFile();

    /**
     * @brief Column field identifiers used as stable ImGui column user-ids for sorting.
     */
    enum class ColumnField : int
    {
        Count = 0, /*!< Frame counter. */
        Time,      /*!< Absolute timestamp. */
        DiffTime,  /*!< Inter-arrival time. */
        MinMax,    /*!< Min/max inter-arrival time. */
        Dir,       /*!< Direction. */
        Channel,   /*!< CAN channel. */
        Identifier,/*!< CAN identifier. */
        Type,      /*!< Frame type. */
        Size,      /*!< Payload size. */
        Name,      /*!< Decoded object name (decode not ported). */
        Data,      /*!< Payload bytes. */
        Count_     /*!< Number of display columns (mirrors the Qt DisplayColumn*Width key set). */
    };

    void RebuildDisplayOrder(const void *par_SortSpecs);

    /**
     * @brief Captures the live pixel width of every table column into m_columnWidths.
     * @details Reads ImGuiTableColumn::WidthGiven via imgui_internal so the widths can be
     *          persisted under the same DisplayColumn*Width keys the Qt widget uses. Must be
     *          called between ImGui::BeginTable and ImGui::EndTable.
     */
    void CaptureColumnWidths();

    /**
     * @brief Renders the minimal self-contained inline file picker for the recorder file.
     * @details Lists the current directory's entries with basic navigation (no external
     *          file-dialog dependency). Writes the chosen path into m_recorderFileName.
     */
    void RenderFilePickerPopup();

    IWindowType *m_pType{nullptr};        /*!< Owning window type. */

    int m_canFifoHandle{-1};              /*!< Private CAN FIFO handle (-1 when closed). */
    std::vector<CAN_ACCEPT_MASK> m_acceptMasks{}; /*!< Acceptance-mask rows (excl. terminator). */
    double m_abtastPeriodeInMs{0.0};      /*!< Scheduler period in ms for cycle-counter frames. */

    std::vector<MessageLine> m_lines{};   /*!< Aggregated rows, sorted by (channel,id,ext). */
    std::vector<int> m_displayOrder{};    /*!< Row indices in current sort order. */
    std::vector<std::uint8_t> m_readBuffer{}; /*!< Scratch payload buffer for FIFO reads. */

    bool m_decode{true};                  /*!< Decode-to-signal flag (persisted; decode not ported). */
    bool m_columnCounter{true};           /*!< Show the counter column. */
    bool m_columnTimeAbsolute{false};     /*!< Show the absolute-time column. */
    bool m_columnTimeDiff{false};         /*!< Show the diff-time column. */
    bool m_columnTimeDiffMinMax{false};   /*!< Show the min/max diff-time column. */

    // Recorder configuration (persisted with the Qt keys).
    bool m_record2DiskIni{false};         /*!< Persisted "record on start" flag. */
    bool m_record2Disk{false};            /*!< Live recording state. */
    bool m_triggerActive{false};          /*!< Whether a trigger equation gates recording. */
    bool m_triggered{false};              /*!< Whether the trigger already fired. */
    std::string m_recorderFileName{};     /*!< Recording target file name. */
    std::string m_triggerEquation{};      /*!< Trigger equation string. */
    std::FILE *m_recorderFile{nullptr};   /*!< Open recording file (nullptr when idle). */
    bool m_waitForFirstMessage{false};    /*!< Pending absolute-time offset capture. */
    double m_tsOffset{0.0};               /*!< Absolute-time offset (ms) for the recording. */

    char m_recorderNameBuffer[512]{};     /*!< Settings text buffer for the file name. */
    char m_triggerBuffer[512]{};          /*!< Settings text buffer for the trigger equation. */

    /**
     * @brief Persisted per-column pixel widths, indexed by ColumnField.
     * @details Round-trips the Qt DisplayColumn*Width keys. A value of 0 means "use the
     *          default width"; fixed columns apply it as their TableSetupColumn init width.
     */
    int m_columnWidths[static_cast<int>(ColumnField::Count_)]{};

    std::string m_fileDialogPath{};       /*!< Directory currently browsed by the inline file picker. */
    char m_fileDialogNameBuffer[512]{};   /*!< Editable file-name field inside the inline file picker. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_WIDGETS_CANMESSAGE_CANMESSAGEWINDOW_H
