#include "Widgets/CanMessage/CanMessageWindow.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "imgui.h"

extern "C" {
#include "CanFifo.h"
#include "EquationParser.h"
#include "Files.h"
#include "IniDataBase.h"
#include "MainValues.h"
#include "RemoteMasterControlProcess.h"
}

namespace XilEnvImGui
{

namespace
{

constexpr int MAX_MESSAGES_PER_READ{64};
constexpr int READ_BUFFER_SIZE{64 * 1024};

/**
 * @brief Maps the CAN frame-type/ext byte to its short label (matches the Qt widget).
 */
const char *FrameTypeLabel(std::uint8_t par_Ext)
{
    switch (par_Ext)
    {
    case 0:
        return "n";
    case 1:
        return "e";
    case 2:
        return "bn";
    case 3:
        return "be";
    case 4:
        return "fn";
    case 5:
        return "fe";
    case 6:
        return "fbn";
    case 7:
        return "fbe";
    case 0x10:
        return "J1939MP";
    default:
        return "";
    }
}

} // namespace

// -------------------------------------------------------------------------
// CanMessageWindowType
// -------------------------------------------------------------------------

std::unique_ptr<IWindowInstance> CanMessageWindowType::CreateNew(const std::string &par_Title)
{
    return std::make_unique<CanMessageWindow>(this, par_Title);
}

std::unique_ptr<IWindowInstance> CanMessageWindowType::CreateFromConfig(int par_FileDescriptor,
                                                                        const char *par_Section,
                                                                        const std::string &par_Name)
{
    auto window = std::make_unique<CanMessageWindow>(this, par_Name);
    window->ReadConfig(par_FileDescriptor, par_Section);
    return window;
}

// -------------------------------------------------------------------------
// CanMessageWindow
// -------------------------------------------------------------------------

CanMessageWindow::CanMessageWindow(IWindowType *par_Type, std::string par_Name) : m_pType{par_Type}
{
    SetName(std::move(par_Name));
    m_readBuffer.resize(static_cast<size_t>(READ_BUFFER_SIZE));
    m_abtastPeriodeInMs = GetSchedulingPeriode() * 1000.0;
    CreateFifo();
}

CanMessageWindow::~CanMessageWindow()
{
    CloseRecorderFile();
    DestroyFifo();
}

void CanMessageWindow::CreateFifo()
{
    if (-1 != m_canFifoHandle)
    {
        return;
    }
    if (0 != s_main_ini_val.ConnectToRemoteMaster)
    {
        m_canFifoHandle = CreateCanFifos(10000, 0x1);
    }
    else
    {
        // do not lose any message: ((uint32_t)-1 << 16)
        m_canFifoHandle = CreateCanFifos(100000, static_cast<int>((static_cast<std::uint32_t>(-1) << 16) | 0x1u));
    }
    ApplyAcceptanceMasks();
    FlushFifo();
}

void CanMessageWindow::DestroyFifo()
{
    if (-1 != m_canFifoHandle)
    {
        (void)DeleteCanFifos(m_canFifoHandle);
        m_canFifoHandle = -1;
    }
}

void CanMessageWindow::FlushFifo()
{
    if (-1 == m_canFifoHandle)
    {
        return;
    }
    CAN_FIFO_ELEM drain[MAX_MESSAGES_PER_READ];
    while (ReadCanMessagesFromFifo2Process(m_canFifoHandle, drain, MAX_MESSAGES_PER_READ) >= MAX_MESSAGES_PER_READ)
    {
        // keep draining until fewer than a full batch remains
    }
}

void CanMessageWindow::ApplyAcceptanceMasks()
{
    if (-1 == m_canFifoHandle)
    {
        return;
    }
    // Build the mask array with the trailing {-1,...} terminator entry, mirroring
    // the Qt widget's fixed-size m_cam[] handling.
    std::vector<CAN_ACCEPT_MASK> masks{m_acceptMasks};
    CAN_ACCEPT_MASK terminator{};
    terminator.Channel = -1;
    terminator.Start = 0;
    terminator.Stop = 0;
    terminator.Flags = 0;
    masks.push_back(terminator);
    (void)SetAcceptMask4CanFifo(m_canFifoHandle, masks.data(), static_cast<int>(masks.size()));
}

void CanMessageWindow::ClearLines()
{
    m_lines.clear();
    m_displayOrder.clear();
}

double CanMessageWindow::TimestampCalc(const CAN_FIFO_ELEM_HEADER *par_Message) const
{
    double ts{0.0};
    if (0 != par_Message->node)
    {
        // Transmitted frames carry the cycle counter as their timestamp.
        ts = static_cast<double>(par_Message->timestamp) * m_abtastPeriodeInMs;
    }
    else if (0 != s_main_ini_val.ConnectToRemoteMaster)
    {
        switch (GetCanCardType(par_Message->channel))
        {
        default:
            ts = static_cast<double>(par_Message->timestamp) * 0.000001;
            break;
        case 1: // I_PCI-165
        case 2: // I_PCI-XC16
        {
            const std::uint32_t cycle{static_cast<std::uint32_t>(par_Message->timestamp >> 32)};
            const std::uint16_t t4Master{
                static_cast<std::uint16_t>(static_cast<std::uint32_t>(par_Message->timestamp) >> 16)};
            const std::uint16_t t4{static_cast<std::uint16_t>(par_Message->timestamp)};
            ts = static_cast<double>(cycle) * m_abtastPeriodeInMs +
                 static_cast<double>(static_cast<std::int16_t>(t4 - t4Master)) / 1250.0;
            break;
        }
        case 6: // Flexcard: high dword UCx, low dword FRCx
            ts = static_cast<double>((par_Message->timestamp << 32) | (par_Message->timestamp >> 32)) / 80000.0;
            break;
        case 8: // SocketCAN
            ts = static_cast<double>(par_Message->timestamp) / 1000.0;
            break;
        }
    }
    else
    {
        // Locally received frames use a 1 ns resolution timestamp.
        ts = static_cast<double>(par_Message->timestamp) * 0.000001;
    }
    return ts;
}

CanMessageWindow::MessageLine &CanMessageWindow::FindOrInsertLine(const CAN_FIFO_ELEM_HEADER *par_Message)
{
    // Rows are kept sorted by (channel, id, ext) so a binary search finds the slot.
    int low{0};
    int high{static_cast<int>(m_lines.size())};
    while (low < high)
    {
        const int mid{(low + high) / 2};
        const MessageLine &line{m_lines[static_cast<size_t>(mid)]};
        bool less{false};
        bool greater{false};
        if (line.channel != par_Message->channel)
        {
            less = (line.channel < par_Message->channel);
            greater = !less;
        }
        else if (line.id != par_Message->id)
        {
            less = (line.id < par_Message->id);
            greater = !less;
        }
        else if (line.ext != par_Message->ext)
        {
            less = (line.ext < par_Message->ext);
            greater = !less;
        }
        else
        {
            return m_lines[static_cast<size_t>(mid)];
        }
        if (less)
        {
            low = mid + 1;
        }
        else if (greater)
        {
            high = mid;
        }
    }
    MessageLine newLine;
    newLine.channel = par_Message->channel;
    newLine.id = par_Message->id;
    newLine.ext = par_Message->ext;
    m_lines.insert(m_lines.begin() + low, std::move(newLine));
    m_displayOrder.clear(); // force a display-order rebuild next frame
    return m_lines[static_cast<size_t>(low)];
}

void CanMessageWindow::ReadAndProcessMessages()
{
    if (-1 == m_canFifoHandle)
    {
        return;
    }
    CAN_FIFO_ELEM_HEADER headers[MAX_MESSAGES_PER_READ];
    std::uint8_t *dataPtrs[MAX_MESSAGES_PER_READ];
    int readElements{0};
    do
    {
        readElements = ReadCanFlexMessagesFromFifo2Process(m_canFifoHandle, headers, MAX_MESSAGES_PER_READ,
                                                           dataPtrs, m_readBuffer.data(), READ_BUFFER_SIZE);
        if (0 >= readElements)
        {
            break;
        }
        RecordMessages(readElements, headers, dataPtrs);
        for (int i{0}; i < readElements; ++i)
        {
            const CAN_FIFO_ELEM_HEADER *message{&headers[i]};
            const double ts{TimestampCalc(message)};
            MessageLine &line{FindOrInsertLine(message)};
            line.node = message->node;
            line.size = message->size;
            const int copySize{(message->size <= 64) ? static_cast<int>(message->size) : 64};
            line.data.assign(dataPtrs[i], dataPtrs[i] + copySize);
            if (0 != line.counter)
            {
                const double diff{ts - line.lastTime};
                if (1 == line.counter)
                {
                    line.dTMin = diff;
                    line.dTMax = diff;
                }
                else if (diff < line.dTMin)
                {
                    line.dTMin = diff;
                }
                else if (diff > line.dTMax)
                {
                    line.dTMax = diff;
                }
                line.dT = diff;
            }
            ++line.counter;
            line.lastTime = ts;
        }
    } while (MAX_MESSAGES_PER_READ == readElements);
}

void CanMessageWindow::RecordMessages(int par_ReadElements, const CAN_FIFO_ELEM_HEADER *par_Messages,
                                      std::uint8_t **par_DataPtrs)
{
    if (!m_record2Disk)
    {
        CloseRecorderFile();
        return;
    }
    if (m_triggerActive)
    {
        if (!m_triggered && !m_triggerEquation.empty())
        {
            m_triggered = (0 != static_cast<int>(direct_solve_equation_no_error_message(m_triggerEquation.c_str())));
        }
    }
    else
    {
        m_triggered = true;
    }
    if (!m_triggered)
    {
        return;
    }
    if (nullptr == m_recorderFile)
    {
        if (m_recorderFileName.empty())
        {
            m_record2Disk = false;
            return;
        }
        m_recorderFile = OpenFile4WriteWithPrefix(m_recorderFileName.c_str(), "wt");
        if (nullptr == m_recorderFile)
        {
            m_record2Disk = false;
            return;
        }
        m_waitForFirstMessage = true;
        std::fprintf(m_recorderFile, "Time\tDir\tCh\tId\tType\tSize\tData[0...n]\n");
    }
    for (int i{0}; i < par_ReadElements; ++i)
    {
        const CAN_FIFO_ELEM_HEADER *message{&par_Messages[i]};
        const double ts{TimestampCalc(message)};
        if (m_waitForFirstMessage)
        {
            const auto now{std::chrono::system_clock::now().time_since_epoch()};
            const double msOfEpoch{
                static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count())};
            m_tsOffset = msOfEpoch - ts;
            m_waitForFirstMessage = false;
        }
        const double t{ts + m_tsOffset};
        const auto totalMs{static_cast<std::uint64_t>(t)};
        const std::uint32_t hour{static_cast<std::uint32_t>(totalMs / 3600000ull)};
        const std::uint32_t minute{static_cast<std::uint32_t>((totalMs / 60000ull) % 60ull)};
        const double seconds{(t - static_cast<double>(hour) * 3600000.0 - static_cast<double>(minute) * 60000.0) *
                             0.001};
        std::fprintf(m_recorderFile, "%02u:%02u:%09.6f\t", hour, minute, seconds);
        std::fprintf(m_recorderFile, "%s\t", (0 != message->node) ? "->" : "<-");
        std::fprintf(m_recorderFile, "%i\t", static_cast<int>(message->channel));
        std::fprintf(m_recorderFile, "0x%X\t", static_cast<int>(message->id));
        std::fprintf(m_recorderFile, "%s\t", FrameTypeLabel(message->ext));
        std::fprintf(m_recorderFile, "%i\t", static_cast<int>(message->size));
        const int size{(message->size <= 64) ? static_cast<int>(message->size) : 64};
        for (int b{0}; b < size; ++b)
        {
            std::fprintf(m_recorderFile, "0x%02X ", static_cast<int>(par_DataPtrs[i][b]));
        }
        std::fprintf(m_recorderFile, "\n");
    }
}

void CanMessageWindow::CloseRecorderFile()
{
    if (nullptr != m_recorderFile)
    {
        close_file(m_recorderFile);
        m_recorderFile = nullptr;
    }
}

void CanMessageWindow::OnUpdateTick(const WindowContext &par_Context)
{
    (void)par_Context;
    ReadAndProcessMessages();
}

void CanMessageWindow::OnAcceptVariableDrop(const std::string &par_VariableName)
{
    (void)par_VariableName; // The CAN-message window does not accept blackboard drops.
}

void CanMessageWindow::RebuildDisplayOrder(const void *par_SortSpecs)
{
    const int lineCount{static_cast<int>(m_lines.size())};
    m_displayOrder.resize(static_cast<size_t>(lineCount));
    for (int i{0}; i < lineCount; ++i)
    {
        m_displayOrder[static_cast<size_t>(i)] = i;
    }

    const ImGuiTableSortSpecs *sortSpecs{static_cast<const ImGuiTableSortSpecs *>(par_SortSpecs)};
    if ((nullptr == sortSpecs) || (0 == sortSpecs->SpecsCount))
    {
        return; // Natural (channel,id,ext) order.
    }
    const ImGuiTableColumnSortSpecs &primary{sortSpecs->Specs[0]};
    const ColumnField field{static_cast<ColumnField>(primary.ColumnUserID)};
    const bool ascending{ImGuiSortDirection_Ascending == primary.SortDirection};

    auto compare = [this, field](int par_LhsIndex, int par_RhsIndex) -> int {
        const MessageLine &lhs{m_lines[static_cast<size_t>(par_LhsIndex)]};
        const MessageLine &rhs{m_lines[static_cast<size_t>(par_RhsIndex)]};
        switch (field)
        {
        case ColumnField::Count:
            return (lhs.counter < rhs.counter) ? -1 : ((lhs.counter > rhs.counter) ? 1 : 0);
        case ColumnField::Time:
            return (lhs.lastTime < rhs.lastTime) ? -1 : ((lhs.lastTime > rhs.lastTime) ? 1 : 0);
        case ColumnField::DiffTime:
            return (lhs.dT < rhs.dT) ? -1 : ((lhs.dT > rhs.dT) ? 1 : 0);
        case ColumnField::MinMax:
            return (lhs.dTMin < rhs.dTMin) ? -1 : ((lhs.dTMin > rhs.dTMin) ? 1 : 0);
        case ColumnField::Dir:
            return static_cast<int>(lhs.node) - static_cast<int>(rhs.node);
        case ColumnField::Channel:
            return lhs.channel - rhs.channel;
        case ColumnField::Type:
            return static_cast<int>(lhs.ext) - static_cast<int>(rhs.ext);
        case ColumnField::Size:
            return static_cast<int>(lhs.size) - static_cast<int>(rhs.size);
        case ColumnField::Identifier:
        case ColumnField::Name:
        case ColumnField::Data:
        default:
            return (lhs.id < rhs.id) ? -1 : ((lhs.id > rhs.id) ? 1 : 0);
        }
    };
    std::sort(m_displayOrder.begin(), m_displayOrder.end(),
              [&compare, ascending](int par_Lhs, int par_Rhs) {
                  const int result{compare(par_Lhs, par_Rhs)};
                  return ascending ? (result < 0) : (result > 0);
              });
}

void CanMessageWindow::RenderContent(const WindowContext &par_Context)
{
    (void)par_Context;

    int columnCount{0};
    if (m_columnCounter)
    {
        ++columnCount;
    }
    if (m_columnTimeAbsolute)
    {
        ++columnCount;
    }
    if (m_columnTimeDiff)
    {
        ++columnCount;
    }
    if (m_columnTimeDiffMinMax)
    {
        ++columnCount;
    }
    columnCount += 6; // Dir, Channel, Identifier, Type, Size, Data
    if (m_decode)
    {
        ++columnCount; // Name column
    }

    const ImGuiTableFlags tableFlags{ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                     ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                                     ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti};

    const ImVec2 avail{ImGui::GetContentRegionAvail()};
    ImGui::BeginChild("##cancontent", avail, false);

    if (ImGui::BeginTable("##cantable", columnCount, tableFlags))
    {
        if (m_columnCounter)
        {
            ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 0.0f,
                                    static_cast<ImGuiID>(ColumnField::Count));
        }
        if (m_columnTimeAbsolute)
        {
            ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 0.0f,
                                    static_cast<ImGuiID>(ColumnField::Time));
        }
        if (m_columnTimeDiff)
        {
            ImGui::TableSetupColumn("dt", ImGuiTableColumnFlags_WidthFixed, 0.0f,
                                    static_cast<ImGuiID>(ColumnField::DiffTime));
        }
        if (m_columnTimeDiffMinMax)
        {
            ImGui::TableSetupColumn("dt min/max", ImGuiTableColumnFlags_WidthFixed, 0.0f,
                                    static_cast<ImGuiID>(ColumnField::MinMax));
        }
        ImGui::TableSetupColumn("Dir", ImGuiTableColumnFlags_WidthFixed, 0.0f,
                                static_cast<ImGuiID>(ColumnField::Dir));
        ImGui::TableSetupColumn("Ch", ImGuiTableColumnFlags_WidthFixed, 0.0f,
                                static_cast<ImGuiID>(ColumnField::Channel));
        ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed, 0.0f,
                                static_cast<ImGuiID>(ColumnField::Identifier));
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 0.0f,
                                static_cast<ImGuiID>(ColumnField::Type));
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 0.0f,
                                static_cast<ImGuiID>(ColumnField::Size));
        if (m_decode)
        {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.0f,
                                    static_cast<ImGuiID>(ColumnField::Name));
        }
        ImGui::TableSetupColumn("Data", ImGuiTableColumnFlags_WidthStretch, 0.0f,
                                static_cast<ImGuiID>(ColumnField::Data));
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        ImGuiTableSortSpecs *sortSpecs{ImGui::TableGetSortSpecs()};
        // Rebuild the display order every frame: rows are added/updated live, so the
        // ordering must reflect both new rows and any active column sort.
        RebuildDisplayOrder(sortSpecs);
        if (nullptr != sortSpecs)
        {
            sortSpecs->SpecsDirty = false;
        }

        char buffer[256];
        for (const int rowIndex : m_displayOrder)
        {
            if ((rowIndex < 0) || (rowIndex >= static_cast<int>(m_lines.size())))
            {
                continue;
            }
            const MessageLine &line{m_lines[static_cast<size_t>(rowIndex)]};
            ImGui::TableNextRow();
            int column{0};
            if (m_columnCounter)
            {
                ImGui::TableSetColumnIndex(column++);
                ImGui::Text("%llu", static_cast<unsigned long long>(line.counter));
            }
            if (m_columnTimeAbsolute)
            {
                ImGui::TableSetColumnIndex(column++);
                ImGui::Text("%.3f", line.lastTime);
            }
            if (m_columnTimeDiff)
            {
                ImGui::TableSetColumnIndex(column++);
                if (2 <= line.counter)
                {
                    ImGui::Text("%.3f", line.dT);
                }
                else
                {
                    ImGui::TextUnformatted("undef");
                }
            }
            if (m_columnTimeDiffMinMax)
            {
                ImGui::TableSetColumnIndex(column++);
                if (2 <= line.counter)
                {
                    ImGui::Text("%.3f/%.3f", line.dTMin, line.dTMax);
                }
                else
                {
                    ImGui::TextUnformatted("undef");
                }
            }
            ImGui::TableSetColumnIndex(column++);
            ImGui::TextUnformatted((0 != line.node) ? "->" : "<-");
            ImGui::TableSetColumnIndex(column++);
            ImGui::Text("%d", line.channel);
            ImGui::TableSetColumnIndex(column++);
            ImGui::Text("%X", line.id);
            ImGui::TableSetColumnIndex(column++);
            ImGui::TextUnformatted(FrameTypeLabel(line.ext));
            ImGui::TableSetColumnIndex(column++);
            ImGui::Text("%u", static_cast<unsigned int>(line.size));
            if (m_decode)
            {
                ImGui::TableSetColumnIndex(column++);
                ImGui::TextUnformatted(""); // Signal decode not ported (see report).
            }
            ImGui::TableSetColumnIndex(column++);
            buffer[0] = '\0';
            char *p{buffer};
            const char *end{buffer + sizeof(buffer)};
            for (std::uint8_t byte : line.data)
            {
                if ((end - p) < 4)
                {
                    break;
                }
                p += std::snprintf(p, static_cast<size_t>(end - p), "%02X ", static_cast<int>(byte));
            }
            ImGui::TextUnformatted(buffer);
        }
        ImGui::EndTable();
    }

    ImGui::EndChild();
}

void CanMessageWindow::RenderSettingsPopup()
{
    if (ImGui::Button("Clear"))
    {
        ClearLines();
        FlushFifo();
    }
    ImGui::SameLine();
    if (m_record2Disk)
    {
        if (ImGui::Button("Stop recording"))
        {
            m_record2Disk = false;
            CloseRecorderFile();
        }
    }
    else if (ImGui::Button("Start recording"))
    {
        m_record2Disk = true;
        m_triggered = false;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Columns:");
    ImGui::Checkbox("Counter", &m_columnCounter);
    ImGui::Checkbox("Absolute time", &m_columnTimeAbsolute);
    ImGui::Checkbox("Diff time", &m_columnTimeDiff);
    ImGui::Checkbox("Min/max diff time", &m_columnTimeDiffMinMax);
    ImGui::Checkbox("Decode (show Name column)", &m_decode);

    ImGui::Separator();
    ImGui::TextUnformatted("Acceptance masks (channel, start-id, stop-id, flags):");
    int removeIndex{-1};
    for (int i{0}; i < static_cast<int>(m_acceptMasks.size()); ++i)
    {
        CAN_ACCEPT_MASK &mask{m_acceptMasks[static_cast<size_t>(i)]};
        ImGui::PushID(i);
        ImGui::SetNextItemWidth(60.0f);
        int channel{mask.Channel};
        if (ImGui::InputInt("##ch", &channel, 0, 0))
        {
            mask.Channel = channel;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        int start{static_cast<int>(mask.Start)};
        if (ImGui::InputInt("##start", &start, 0, 0, ImGuiInputTextFlags_CharsHexadecimal))
        {
            mask.Start = static_cast<std::uint32_t>(start);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        int stop{static_cast<int>(mask.Stop)};
        if (ImGui::InputInt("##stop", &stop, 0, 0, ImGuiInputTextFlags_CharsHexadecimal))
        {
            mask.Stop = static_cast<std::uint32_t>(stop);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        int flags{mask.Flags};
        if (ImGui::InputInt("##flags", &flags, 0, 0, ImGuiInputTextFlags_CharsHexadecimal))
        {
            mask.Flags = flags;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
        {
            removeIndex = i;
        }
        ImGui::PopID();
    }
    if (0 <= removeIndex)
    {
        m_acceptMasks.erase(m_acceptMasks.begin() + removeIndex);
    }
    if (ImGui::Button("Add mask"))
    {
        CAN_ACCEPT_MASK mask{};
        mask.Channel = 0;
        mask.Start = 0;
        mask.Stop = 0x7FF;
        mask.Flags = 0;
        m_acceptMasks.push_back(mask);
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply masks"))
    {
        ApplyAcceptanceMasks();
        ClearLines();
        FlushFifo();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Recorder:");
    if (ImGui::InputText("File name", m_recorderNameBuffer, sizeof(m_recorderNameBuffer)))
    {
        m_recorderFileName = m_recorderNameBuffer;
    }
    ImGui::Checkbox("Record on start", &m_record2DiskIni);
    ImGui::Checkbox("Trigger active", &m_triggerActive);
    if (ImGui::InputText("Trigger equation", m_triggerBuffer, sizeof(m_triggerBuffer)))
    {
        m_triggerEquation = m_triggerBuffer;
    }
}

// -------------------------------------------------------------------------
// Config round-trip (Qt "GUI/Widgets/<name>" layout)
// -------------------------------------------------------------------------

void CanMessageWindow::WriteConfig(int par_FileDescriptor, const char *par_Section)
{
    (void)IniFileDataBaseWriteString(par_Section, "type", m_pType->GetTypeName(), par_FileDescriptor);

    char entry[32];
    char line[256];
    int index{0};
    for (; index < static_cast<int>(m_acceptMasks.size()) && (index < (MAX_CAN_ACCEPTANCE_MASK_ELEMENTS - 1));
         ++index)
    {
        const CAN_ACCEPT_MASK &mask{m_acceptMasks[static_cast<size_t>(index)]};
        std::snprintf(entry, sizeof(entry), "cam%i", index);
        std::snprintf(line, sizeof(line), "%i:0x%X,0x%X,0x%X", mask.Channel, mask.Start, mask.Stop,
                      static_cast<unsigned int>(mask.Flags));
        (void)IniFileDataBaseWriteString(par_Section, entry, line, par_FileDescriptor);
    }
    // Clear any stale cam entries left over from a longer list.
    for (; index < (MAX_CAN_ACCEPTANCE_MASK_ELEMENTS - 1); ++index)
    {
        std::snprintf(entry, sizeof(entry), "cam%i", index);
        (void)IniFileDataBaseWriteString(par_Section, entry, nullptr, par_FileDescriptor);
    }

    (void)IniFileDataBaseWriteInt(par_Section, "Record2Disk", m_record2DiskIni ? 1 : 0, par_FileDescriptor);
    (void)IniFileDataBaseWriteInt(par_Section, "TriggerActiv", m_triggerActive ? 1 : 0, par_FileDescriptor);
    (void)IniFileDataBaseWriteString(par_Section, "FileName", m_recorderFileName.c_str(), par_FileDescriptor);
    (void)IniFileDataBaseWriteString(par_Section, "TriggerEquation", m_triggerEquation.c_str(), par_FileDescriptor);

    (void)IniFileDataBaseWriteInt(par_Section, "Decode", m_decode ? 1 : 0, par_FileDescriptor);
    (void)IniFileDataBaseWriteInt(par_Section, "DisplayColumnCounter", m_columnCounter ? 1 : 0, par_FileDescriptor);
    (void)IniFileDataBaseWriteInt(par_Section, "DisplayColumnTimeAbsolute", m_columnTimeAbsolute ? 1 : 0,
                                  par_FileDescriptor);
    (void)IniFileDataBaseWriteInt(par_Section, "DisplayColumnTimeDiff", m_columnTimeDiff ? 1 : 0,
                                  par_FileDescriptor);
    (void)IniFileDataBaseWriteInt(par_Section, "DisplayColumnTimeDiffMinMax", m_columnTimeDiffMinMax ? 1 : 0,
                                  par_FileDescriptor);
}

void CanMessageWindow::ReadConfig(int par_FileDescriptor, const char *par_Section)
{
    m_acceptMasks.clear();
    char entry[32];
    char txt[256];
    for (int i{0}; i < (MAX_CAN_ACCEPTANCE_MASK_ELEMENTS - 1); ++i)
    {
        std::snprintf(entry, sizeof(entry), "cam%i", i);
        if (0 == IniFileDataBaseReadString(par_Section, entry, "", txt, sizeof(txt), par_FileDescriptor))
        {
            break;
        }
        char *p{txt};
        char *endptr{nullptr};
        const long channel{std::strtol(p, &endptr, 10)};
        if ((nullptr == endptr) || (':' != *endptr))
        {
            break;
        }
        p = endptr + 1;
        const unsigned long start{std::strtoul(p, &endptr, 0)};
        if ((nullptr == endptr) || (',' != *endptr))
        {
            break;
        }
        p = endptr + 1;
        const unsigned long stop{std::strtoul(p, &endptr, 0)};
        unsigned long flags{0};
        if ((nullptr != endptr) && (',' == *endptr))
        {
            p = endptr + 1;
            flags = std::strtoul(p, &endptr, 0);
        }
        CAN_ACCEPT_MASK mask{};
        mask.Channel = static_cast<std::int32_t>(channel);
        mask.Start = static_cast<std::uint32_t>(start);
        mask.Stop = static_cast<std::uint32_t>(stop);
        mask.Flags = static_cast<std::int32_t>(flags);
        m_acceptMasks.push_back(mask);
    }

    m_record2DiskIni = (0 != IniFileDataBaseReadInt(par_Section, "Record2Disk", 0, par_FileDescriptor));
    m_record2Disk = m_record2DiskIni;
    m_triggerActive = (0 != IniFileDataBaseReadInt(par_Section, "TriggerActiv", 0, par_FileDescriptor));
    (void)IniFileDataBaseReadString(par_Section, "FileName", "", txt, sizeof(txt), par_FileDescriptor);
    m_recorderFileName = txt;
    char equation[256];
    (void)IniFileDataBaseReadString(par_Section, "TriggerEquation", "", equation, sizeof(equation),
                                    par_FileDescriptor);
    m_triggerEquation = equation;

    m_decode = (0 != IniFileDataBaseReadInt(par_Section, "Decode", 1, par_FileDescriptor));
    m_columnCounter = (0 != IniFileDataBaseReadInt(par_Section, "DisplayColumnCounter", 1, par_FileDescriptor));
    m_columnTimeAbsolute =
        (0 != IniFileDataBaseReadInt(par_Section, "DisplayColumnTimeAbsolute", 0, par_FileDescriptor));
    m_columnTimeDiff = (0 != IniFileDataBaseReadInt(par_Section, "DisplayColumnTimeDiff", 0, par_FileDescriptor));
    m_columnTimeDiffMinMax =
        (0 != IniFileDataBaseReadInt(par_Section, "DisplayColumnTimeDiffMinMax", 0, par_FileDescriptor));

    std::snprintf(m_recorderNameBuffer, sizeof(m_recorderNameBuffer), "%s", m_recorderFileName.c_str());
    std::snprintf(m_triggerBuffer, sizeof(m_triggerBuffer), "%s", m_triggerEquation.c_str());

    ApplyAcceptanceMasks();
    FlushFifo();
}

} // namespace XilEnvImGui
