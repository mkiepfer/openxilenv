#include "Framework/CanErrorDialog.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <regex>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_stdlib.h"

extern "C" {
#include "Blackboard.h"
#include "BlackboardAccess.h"
#include "ConfigurablePrefix.h"
#include "IniDataBase.h"

int ScriptSetCanErr(int Channel, int Id, int Startbit, int Bitsize, const char *Byteorder,
                    uint32_t Cycles, uint64_t BitErrValue);
int ScriptClearCanErr(void);
}

namespace XilEnvImGui
{

namespace
{

constexpr int START_BIT_CHANGE_SIZE{-1}; /*!< Startbit sentinel selecting the change-size action. */
constexpr int START_BIT_INTERRUPT{-2};   /*!< Startbit sentinel selecting the interrupt action. */

int64_t ParseInteger(const std::string &par_Text)
{
    return static_cast<int64_t>(std::strtoll(par_Text.c_str(), nullptr, 0));
}

uint64_t ParseUnsigned(const std::string &par_Text)
{
    return std::strtoull(par_Text.c_str(), nullptr, 0);
}

std::string ToHex(uint32_t par_Value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%X", par_Value);
    return std::string{buffer};
}

} // namespace

CanErrorDialogManager &CanErrorDialogManager::GetInstance()
{
    static CanErrorDialogManager instance;
    return instance;
}

void CanErrorDialogManager::OpenDialog()
{
    m_variants = SnapshotCanVariantsFromIni(m_numberOfChannels);
    m_selVariant = -1;
    m_selObject = -1;
    m_selSignal = -1;
    m_searchFilter.clear();
    m_searchVariant = -1;
    m_searchObject = -1;
    m_searchSignal = -1;
    RegisterCounter();
    LoadHistory();
    m_open = true;
    m_openRequested = true;
}

void CanErrorDialogManager::RegisterCounter()
{
    if (0 <= m_counterVid)
    {
        return;
    }
    char name[BBVARI_NAME_SIZE];
    m_counterVid = add_bbvari(
        ExtendsWithConfigurablePrefix(CONFIGURABLE_PREFIX_TYPE_CAN_NAMES, ".CANBitError.Counter",
                                      name, static_cast<int>(sizeof(name))),
        BB_UNKNOWN, "");
}

void CanErrorDialogManager::ReleaseCounter()
{
    if (0 <= m_counterVid)
    {
        (void)remove_bbvari(m_counterVid);
        m_counterVid = -1;
    }
}

void CanErrorDialogManager::LoadHistory()
{
    char buffer[1024];
    buffer[0] = '\0';
    (void)IniFileDataBaseReadString("CAN/Global", "can_biterr_history0", "", buffer,
                                    static_cast<int>(sizeof(buffer)), GetMainFileDescriptor());
    std::string line{buffer};
    std::vector<std::string> parts;
    std::size_t start{0};
    while (start <= line.size())
    {
        const std::size_t comma{line.find(',', start)};
        const std::size_t end{(std::string::npos == comma) ? line.size() : comma};
        parts.push_back(line.substr(start, end - start));
        if (std::string::npos == comma)
        {
            break;
        }
        start = comma + 1;
    }
    if (8 <= parts.size())
    {
        m_channel = static_cast<int>(ParseInteger(parts[0]));
        m_idText = parts[1];
        m_startBitText = parts[2];
        m_bitSizeText = parts[3];
        m_byteOrder = (parts[4] == "msb_first") ? 1 : 0;
        m_bitValueText = parts[5];
        m_sizeText = parts[6];
        m_cyclesText = parts[7];
        if ((m_channel < 0) || (m_channel >= m_numberOfChannels))
        {
            m_channel = 0;
        }
    }
}

void CanErrorDialogManager::SaveHistory()
{
    char line[512];
    std::snprintf(line, sizeof(line), "%d,0x%X,%d,%d,%s,%llu,%d,%u", m_channel, m_id, m_startBit,
                  m_bitSize, (m_byteOrder ? "msb_first" : "lsb_first"),
                  static_cast<unsigned long long>(m_bitValue), m_size, m_cycles);
    (void)IniFileDataBaseWriteString("CAN/Global", "can_biterr_history0", line,
                                     GetMainFileDescriptor());
}

void CanErrorDialogManager::CollectForStart(CanErrorMode par_Mode)
{
    m_id = static_cast<uint32_t>(ParseUnsigned(m_idText));
    m_cycles = static_cast<uint32_t>(ParseUnsigned(m_cyclesText));
    switch (par_Mode)
    {
    case CanErrorMode::ChangeBits:
        m_startBit = static_cast<int>(ParseInteger(m_startBitText));
        m_bitSize = static_cast<int>(ParseInteger(m_bitSizeText));
        m_bitValue = ParseUnsigned(m_bitValueText);
        break;
    case CanErrorMode::ChangeSize:
        m_startBit = START_BIT_CHANGE_SIZE;
        m_bitSize = static_cast<int>(ParseInteger(m_sizeText));
        m_size = m_bitSize;
        m_byteOrder = 0;
        m_bitValue = 0;
        break;
    case CanErrorMode::Interrupt:
        m_startBit = START_BIT_INTERRUPT;
        m_bitSize = 0;
        m_byteOrder = 0;
        m_bitValue = 0;
        break;
    }
}

void CanErrorDialogManager::FillFromObject(const CanTreeVariant &par_Variant,
                                           const CanTreeObject &par_Object,
                                           const CanTreeSignal *par_Signal)
{
    m_id = par_Object.identifier;
    m_idText = ToHex(par_Object.identifier);
    m_sizeText = std::to_string(par_Object.size);

    for (int c = 0; c < m_numberOfChannels; ++c)
    {
        if (par_Variant.connectedChannels.at(static_cast<std::size_t>(c)))
        {
            m_channel = c;
            break;
        }
    }
    if (nullptr != par_Signal)
    {
        m_startBitText = std::to_string(par_Signal->startBit);
        m_bitSizeText = std::to_string(par_Signal->bitSize);
        m_byteOrder = (CanByteOrder::MsbFirst == par_Signal->byteOrder) ? 1 : 0;
    }
}

bool CanErrorDialogManager::SearchNextSignal()
{
    if (m_searchFilter.empty())
    {
        return false;
    }
    std::regex regex;
    try
    {
        regex = std::regex{m_searchFilter, std::regex::ECMAScript};
    }
    catch (const std::regex_error &)
    {
        return false;
    }
    for (std::size_t v = 0; v < m_variants.size(); ++v)
    {
        const CanTreeVariant &variant = m_variants[v];
        for (std::size_t o = 0; o < variant.objects.size(); ++o)
        {
            const CanTreeObject &object = variant.objects[o];
            for (std::size_t s = 0; s < object.signals.size(); ++s)
            {
                const bool afterCursor{
                    (static_cast<int>(v) > m_searchVariant) ||
                    ((static_cast<int>(v) == m_searchVariant) &&
                     (static_cast<int>(o) > m_searchObject)) ||
                    ((static_cast<int>(v) == m_searchVariant) &&
                     (static_cast<int>(o) == m_searchObject) &&
                     (static_cast<int>(s) > m_searchSignal))};
                if (!afterCursor)
                {
                    continue;
                }
                if (std::regex_search(object.signals[s].name, regex))
                {
                    m_searchVariant = static_cast<int>(v);
                    m_searchObject = static_cast<int>(o);
                    m_searchSignal = static_cast<int>(s);
                    m_selVariant = static_cast<int>(v);
                    m_selObject = static_cast<int>(o);
                    m_selSignal = static_cast<int>(s);
                    FillFromObject(variant, object, &object.signals[s]);
                    return true;
                }
            }
        }
    }
    return false;
}

void CanErrorDialogManager::RenderTree()
{
    ImGui::SetNextItemWidth(200.0f);
    const bool entered{ImGui::InputTextWithHint("##canerr_search", "search signal (regex)",
                                                &m_searchFilter,
                                                ImGuiInputTextFlags_EnterReturnsTrue)};
    ImGui::SameLine();
    if (ImGui::Button("Find next") || entered)
    {
        if (!SearchNextSignal())
        {
            m_searchVariant = -1;
            m_searchObject = -1;
            m_searchSignal = -1;
        }
    }

    if (ImGui::BeginChild("##canerr_tree", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
    {
        for (std::size_t v = 0; v < m_variants.size(); ++v)
        {
            const CanTreeVariant &variant = m_variants[v];
            ImGui::PushID(static_cast<int>(v));
            const std::string variantLabel{variant.name.empty() ? ("Variant_" + std::to_string(v))
                                                                : variant.name};
            const bool variantOpen{ImGui::TreeNodeEx("##variant",
                                                     ImGuiTreeNodeFlags_SpanAvailWidth |
                                                         ImGuiTreeNodeFlags_DefaultOpen,
                                                     "%s", variantLabel.c_str())};
            if (variantOpen)
            {
                for (std::size_t o = 0; o < variant.objects.size(); ++o)
                {
                    const CanTreeObject &object = variant.objects[o];
                    ImGui::PushID(static_cast<int>(o));
                    ImGuiTreeNodeFlags objectFlags{ImGuiTreeNodeFlags_OpenOnArrow |
                                                   ImGuiTreeNodeFlags_SpanAvailWidth};
                    if ((static_cast<int>(v) == m_selVariant) &&
                        (static_cast<int>(o) == m_selObject) && (0 > m_selSignal))
                    {
                        objectFlags |= ImGuiTreeNodeFlags_Selected;
                    }
                    char objectLabel[352];
                    std::snprintf(objectLabel, sizeof(objectLabel), "[0x%X] %s", object.identifier,
                                  object.name.c_str());
                    const bool objectOpen{
                        ImGui::TreeNodeEx("##object", objectFlags, "%s", objectLabel)};
                    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                    {
                        m_selVariant = static_cast<int>(v);
                        m_selObject = static_cast<int>(o);
                        m_selSignal = -1;
                        FillFromObject(variant, object, nullptr);
                    }
                    if (objectOpen)
                    {
                        for (std::size_t s = 0; s < object.signals.size(); ++s)
                        {
                            const CanTreeSignal &signal = object.signals[s];
                            ImGui::PushID(static_cast<int>(s));
                            ImGuiTreeNodeFlags signalFlags{ImGuiTreeNodeFlags_Leaf |
                                                           ImGuiTreeNodeFlags_SpanAvailWidth |
                                                           ImGuiTreeNodeFlags_NoTreePushOnOpen};
                            if ((static_cast<int>(v) == m_selVariant) &&
                                (static_cast<int>(o) == m_selObject) &&
                                (static_cast<int>(s) == m_selSignal))
                            {
                                signalFlags |= ImGuiTreeNodeFlags_Selected;
                            }
                            ImGui::TreeNodeEx("##signal", signalFlags, "%s",
                                              signal.name.empty() ? "<signal>" : signal.name.c_str());
                            if (ImGui::IsItemClicked())
                            {
                                m_selVariant = static_cast<int>(v);
                                m_selObject = static_cast<int>(o);
                                m_selSignal = static_cast<int>(s);
                                FillFromObject(variant, object, &signal);
                            }
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

void CanErrorDialogManager::RenderTab(CanErrorMode par_Mode)
{
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::BeginCombo("Channel", std::to_string(m_channel).c_str()))
    {
        for (int c = 0; c < m_numberOfChannels; ++c)
        {
            const bool selected{c == m_channel};
            if (ImGui::Selectable(std::to_string(c).c_str(), selected))
            {
                m_channel = c;
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::InputText("Object Id", &m_idText, ImGuiInputTextFlags_CharsHexadecimal);

    if (CanErrorMode::ChangeBits == par_Mode)
    {
        ImGui::InputText("Start bit", &m_startBitText, ImGuiInputTextFlags_CharsDecimal);
        ImGui::InputText("Bit size", &m_bitSizeText, ImGuiInputTextFlags_CharsDecimal);
        const char *const BYTE_ORDER_LABELS[]{"lsb_first", "msb_first"};
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::BeginCombo("Byte order", BYTE_ORDER_LABELS[(0 != m_byteOrder) ? 1 : 0]))
        {
            for (int i = 0; i < 2; ++i)
            {
                if (ImGui::Selectable(BYTE_ORDER_LABELS[i], i == m_byteOrder))
                {
                    m_byteOrder = i;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::InputText("Bit value", &m_bitValueText, ImGuiInputTextFlags_CharsDecimal);
    }
    else if (CanErrorMode::ChangeSize == par_Mode)
    {
        ImGui::InputText("Change size to", &m_sizeText, ImGuiInputTextFlags_CharsDecimal);
    }

    ImGui::InputText("Cycles", &m_cyclesText, ImGuiInputTextFlags_CharsHexadecimal);

    if (ImGui::Button("Start", ImVec2(90.0f, 0.0f)))
    {
        CollectForStart(par_Mode);
        SaveHistory();
        (void)ScriptSetCanErr(m_channel, static_cast<int>(m_id), m_startBit, m_bitSize,
                              (m_byteOrder ? "msb_first" : "lsb_first"), m_cycles, m_bitValue);
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop", ImVec2(90.0f, 0.0f)))
    {
        (void)ScriptClearCanErr();
    }
    ImGui::SameLine();
    ImGui::Text("Counter: %llu", static_cast<unsigned long long>(m_counterValue));
}

void CanErrorDialogManager::Render()
{
    if (m_openRequested)
    {
        ImGui::OpenPopup("Insert CAN error");
        m_openRequested = false;
    }
    if (!m_open)
    {
        return;
    }

    if (0 <= m_counterVid)
    {
        m_counterValue = static_cast<uint64_t>(read_bbvari_convert_double(m_counterVid));
    }

    ImGui::SetNextWindowSize(ImVec2(880.0f, 520.0f), ImGuiCond_FirstUseEver);
    bool open{m_open};
    if (ImGui::BeginPopupModal("Insert CAN error", &open, ImGuiWindowFlags_NoSavedSettings))
    {
        const float footerHeight{ImGui::GetFrameHeightWithSpacing() +
                                 ImGui::GetStyle().ItemSpacing.y};
        if (ImGui::BeginChild("##canerr_body", ImVec2(0.0f, -footerHeight)))
        {
            if (ImGui::BeginTable("##canerr_split", 2,
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
            {
                ImGui::TableSetupColumn("Tree", ImGuiTableColumnFlags_WidthStretch, 0.45f);
                ImGui::TableSetupColumn("Error", ImGuiTableColumnFlags_WidthStretch, 0.55f);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                RenderTree();

                ImGui::TableSetColumnIndex(1);
                if (ImGui::BeginTabBar("##canerr_tabs"))
                {
                    if (ImGui::BeginTabItem("Change bits"))
                    {
                        RenderTab(CanErrorMode::ChangeBits);
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Change size"))
                    {
                        RenderTab(CanErrorMode::ChangeSize);
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Interrupt"))
                    {
                        RenderTab(CanErrorMode::Interrupt);
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)))
        {
            m_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!open)
    {
        m_open = false;
    }
    if (!m_open)
    {
        ReleaseCounter();
    }
}

} // namespace XilEnvImGui
