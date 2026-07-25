#include "Widgets/CalibrationTree/CalibrationTreeWindow.h"

#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "imgui.h"

extern "C" {
#include "BaseMessages.h"
#include "Blackboard.h"
#include "DebugInfoDB.h"
#include "IniDataBase.h"
#include "ReadWriteValue.h"
#include "Scheduler.h"
#include "UniqueNumber.h"
}

namespace XilEnvImGui
{

namespace
{

constexpr int LABEL_COLUMN{0};
constexpr int VALUE_TIMEOUT_LOCK_MS{10};

/**
 * @brief Builds the absolute-address hex string used by the Qt address column.
 */
std::string FormatAddress(std::uint64_t par_Address)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%08llX", static_cast<unsigned long long>(par_Address));
    return buffer;
}

} // namespace

// -------------------------------------------------------------------------
// CalibrationTreeWindowType
// -------------------------------------------------------------------------

std::unique_ptr<IWindowInstance> CalibrationTreeWindowType::CreateNew(const std::string &par_Title)
{
    return std::make_unique<CalibrationTreeWindow>(this, par_Title);
}

std::unique_ptr<IWindowInstance> CalibrationTreeWindowType::CreateFromConfig(int par_FileDescriptor,
                                                                            const char *par_Section,
                                                                            const std::string &par_Name)
{
    auto window = std::make_unique<CalibrationTreeWindow>(this, par_Name);
    window->ReadConfig(par_FileDescriptor, par_Section);
    return window;
}

// -------------------------------------------------------------------------
// CalibrationTreeWindow::TreeNode
// -------------------------------------------------------------------------

CalibrationTreeWindow::TreeNode::TreeNode(CalibrationTreeWindow *par_Owner, int par_What,
                                          std::int32_t par_TypeNr, bool par_IsBasisClass,
                                          std::uint64_t par_Address, bool par_AddressIsResolved,
                                          bool par_AddressIsInvalid, std::string par_Label,
                                          std::string par_DataType, TreeNode *par_Parent)
    : m_pOwner{par_Owner}, m_what{par_What}, m_typeNr{par_TypeNr}, m_address{par_Address},
      m_addressIsResolved{par_AddressIsResolved}, m_addressIsInvalid{par_AddressIsInvalid},
      m_isBasisClass{par_IsBasisClass}, m_label{std::move(par_Label)},
      m_dataType{std::move(par_DataType)}, m_pParent{par_Parent}
{
    m_valueCache.uqw = 0;
}

void CalibrationTreeWindow::TreeNode::ExpandRoot()
{
    PROCESS_APPL_DATA *pDebugInfos{m_pOwner->GetDebugInfos()};
    if (nullptr == pDebugInfos)
    {
        return;
    }
    SECTION_ADDR_RANGES_FILTER *pSectionFilter{m_pOwner->GetGlobalSectionFilter()};
    INCLUDE_EXCLUDE_FILTER *pIncExcFilter{m_pOwner->GetIncludeExcludeFilter()};

    std::int32_t idxSort{0};
    std::int32_t idx{0};
    while ((idx = get_next_sorted_label_index(&idxSort, pDebugInfos)) >= 0)
    {
        std::uint64_t address{0};
        std::int32_t typeNr{0};
        char *pLabel{get_next_label_ex(&idx, &address, &typeNr, pDebugInfos)};
        if (nullptr == pLabel)
        {
            continue;
        }
        if (0 == CheckAddressRangeFilter(address, pSectionFilter))
        {
            continue;
        }
        if (nullptr != pIncExcFilter)
        {
            if (0 == CheckIncludeExcludeFilter(pLabel, pIncExcFilter))
            {
                continue;
            }
        }

        int what{0};
        char *pType{get_typestr_by_typenr(&typeNr, &what, pDebugInfos)};
        if (nullptr == pType)
        {
            continue;
        }

        m_children.push_back(std::make_unique<TreeNode>(m_pOwner, what, typeNr, false, address, true,
                                                        false, std::string{pLabel}, std::string{pType},
                                                        this));
    }
}

void CalibrationTreeWindow::TreeNode::ExpandStructure()
{
    PROCESS_APPL_DATA *pDebugInfos{m_pOwner->GetDebugInfos()};
    const char *pName{nullptr};
    std::int32_t typeNr{0};
    std::int32_t pos{0};
    int what{0};
    int flag{1};
    std::uint32_t addressOffset{0};
    int bclass{0};

    while ((pName = get_next_struct_entry(&typeNr, &addressOffset, &bclass, m_typeNr, flag, &pos,
                                          pDebugInfos)) != nullptr)
    {
        flag = 0;
        const std::uint64_t address{m_address + addressOffset};

        const char *pTypeName{get_struct_entry_typestr(&typeNr, pos, &what, pDebugInfos)};
        if (nullptr == pTypeName)
        {
            pTypeName = "unknown)";
            what = 5;
        }

        m_children.push_back(std::make_unique<TreeNode>(m_pOwner, what, typeNr, 0 != bclass, address,
                                                        m_addressIsResolved, false, std::string{pName},
                                                        std::string{pTypeName}, this));
    }
}

void CalibrationTreeWindow::TreeNode::ExpandArray()
{
    PROCESS_APPL_DATA *pDebugInfos{m_pOwner->GetDebugInfos()};
    char typeBuffer[1024];
    std::int32_t arrayElemTypeNr{0};
    int arrayElemOfWhat{0};
    std::int32_t arraySize{0};
    std::int32_t arrayElements{0};

    if (0 == get_array_elem_type_and_size(&arrayElemTypeNr, &arrayElemOfWhat, &arraySize, &arrayElements,
                                          m_typeNr, pDebugInfos))
    {
        return;
    }
    if (0 >= arrayElements)
    {
        return;
    }

    const char *pTypeName{""};
    if (get_array_type_string(m_typeNr, 1, typeBuffer, sizeof(typeBuffer), pDebugInfos) > 0)
    {
        pTypeName = typeBuffer;
    }

    for (std::int32_t x{0}; x < arrayElements; ++x)
    {
        const std::uint64_t address{m_address +
                                    static_cast<std::uint64_t>(x * (arraySize / arrayElements))};
        char elemName[64];
        std::snprintf(elemName, sizeof(elemName), "[%d]", x);
        m_children.push_back(std::make_unique<TreeNode>(m_pOwner, arrayElemOfWhat, arrayElemTypeNr, false,
                                                        address, m_addressIsResolved, false,
                                                        std::string{elemName}, std::string{pTypeName},
                                                        this));
    }
}

void CalibrationTreeWindow::TreeNode::ExpandPointer()
{
    PROCESS_APPL_DATA *pDebugInfos{m_pOwner->GetDebugInfos()};
    const int pid{m_pOwner->GetPid()};
    char typeBuffer[1024];
    std::int32_t pointsToTypeNr{0};
    int pointsToWhat{0};

    m_pointsToAddress = 0;
    m_pointsToAddressIsResolved =
        (ReadBytesFromExternProcessStopScheduler(pid, m_address, sizeof(m_pointsToAddress),
                                                 &m_pointsToAddress, 0, 0) ==
         static_cast<int>(sizeof(m_pointsToAddress)));

    if (0 == get_points_to(&pointsToTypeNr, &pointsToWhat, m_typeNr, pDebugInfos))
    {
        return;
    }

    const char *pName{""};
    if (get_pointer_type_string(m_typeNr, 1, typeBuffer, sizeof(typeBuffer), pDebugInfos) > 0)
    {
        pName = typeBuffer;
    }

    int expand{0};
    switch (pointsToWhat)
    {
    case 1: // Base data type
        if ('*' == pName[0])
        {
            if (0 != std::strcmp(&pName[1], "void"))
            {
                expand = 4;
            }
        }
        else
        {
            expand = 1;
        }
        break;
    case 2: // Structure
        expand = 2;
        break;
    case 3: // Array
        expand = 3;
        break;
    case 4: // Pointer
        expand = 4;
        break;
    default:
        break;
    }

    if ((4 == expand) || (1 == expand))
    {
        m_children.push_back(std::make_unique<TreeNode>(m_pOwner, pointsToWhat, pointsToTypeNr, false,
                                                        m_pointsToAddress, m_pointsToAddressIsResolved,
                                                        false, std::string{"[0]"}, std::string{pName},
                                                        this));
    }
    else if (-1 != pointsToTypeNr)
    {
        const std::int32_t savedTypeNr{m_typeNr};
        const std::uint64_t savedAddress{m_address};
        const bool savedResolved{m_addressIsResolved};
        m_typeNr = pointsToTypeNr;
        m_address = m_pointsToAddress;
        m_addressIsResolved = m_pointsToAddressIsResolved;
        if (2 == expand)
        {
            ExpandStructure();
        }
        else if (3 == expand)
        {
            ExpandArray();
        }
        m_typeNr = savedTypeNr;
        m_address = savedAddress;
        m_addressIsResolved = savedResolved;
    }
}

void CalibrationTreeWindow::TreeNode::ExpandChilds()
{
    if (m_childsExpanded)
    {
        return;
    }
    switch (m_what)
    {
    case -1: // Root
        ExpandRoot();
        break;
    case 1: // Base data type (only pointer-like typenr > 100 expands)
        if (m_typeNr > 100)
        {
            ExpandPointer();
        }
        break;
    case 2: // Structure
        ExpandStructure();
        break;
    case 3: // Array
        ExpandArray();
        break;
    case 4: // Pointer
        ExpandPointer();
        break;
    default:
        break;
    }
    m_childsExpanded = true;
}

int CalibrationTreeWindow::TreeNode::ChildCount()
{
    if (!m_childsExpanded)
    {
        PROCESS_APPL_DATA *pDebugInfos{m_pOwner->GetDebugInfos()};
        switch (m_what)
        {
        case 1:
            return 0; // Base data types have no children.
        case 4:
        {
            std::int32_t pointsToTypeNr{0};
            int pointsToWhat{0};
            if (0 != get_points_to(&pointsToTypeNr, &pointsToWhat, m_typeNr, pDebugInfos))
            {
                switch (pointsToWhat)
                {
                case 0:
                case 1:
                    return 1;
                case 2:
                    return get_structure_element_count(pointsToTypeNr, pDebugInfos);
                case 3:
                    return get_array_element_count(pointsToTypeNr, pDebugInfos);
                case 4:
                    return 1;
                default:
                    break;
                }
            }
            return 0;
        }
        case 3:
            return get_array_element_count(m_typeNr, pDebugInfos);
        case 2:
            return get_structure_element_count(m_typeNr, pDebugInfos);
        case MODIFIER_ELEM:
            return 1;
        default:
            ExpandChilds();
            break;
        }
    }
    return static_cast<int>(m_children.size());
}

std::vector<std::unique_ptr<CalibrationTreeWindow::TreeNode>> &
CalibrationTreeWindow::TreeNode::GetChildren()
{
    if (!m_childsExpanded)
    {
        ExpandChilds();
    }
    return m_children;
}

bool CalibrationTreeWindow::TreeNode::IsValidBaseElement() const
{
    if ((1 != m_what) || ((0 == m_address) && m_addressIsResolved) ||
        (get_base_type_bb_type_ex(m_typeNr, m_pOwner->GetDebugInfos()) < 0))
    {
        return false;
    }
    return true;
}

std::string CalibrationTreeWindow::TreeNode::GetValueString(bool &ret_NeedsRead)
{
    ret_NeedsRead = false;
    PROCESS_APPL_DATA *pDebugInfos{m_pOwner->GetDebugInfos()};

    if ((1 != m_what) || ((0 == m_address) && m_addressIsResolved) || m_addressIsInvalid ||
        (get_base_type_bb_type_ex(m_typeNr, pDebugInfos) < 0))
    {
        return (1 != m_what) ? std::string{} : std::string{"invalid"};
    }
    if (!m_valueCachedFlag)
    {
        ret_NeedsRead = true;
        return std::string{};
    }
    const enum BB_DATA_TYPES type{static_cast<enum BB_DATA_TYPES>(get_base_type_bb_type_ex(m_typeNr, pDebugInfos))};
    char buffer[256];
    bbvari_to_string(type, m_valueCache, (0 != m_pOwner->GetHexOrDec()) ? 16 : 10, buffer, sizeof(buffer));
    return buffer;
}

std::string CalibrationTreeWindow::TreeNode::GetAddressString() const
{
    return FormatAddress(m_address);
}

std::string CalibrationTreeWindow::TreeNode::GetCompleteName() const
{
    if (nullptr != m_pParent)
    {
        std::string name{m_pParent->GetCompleteNameWithSeparator()};
        name.append(m_label);
        return name;
    }
    return m_label;
}

std::string CalibrationTreeWindow::TreeNode::GetCompleteNameWithSeparator() const
{
    if (nullptr != m_pParent)
    {
        std::string name{m_pParent->GetCompleteNameWithSeparator()};
        name.append(m_label);
        if (2 == m_what) // Structure
        {
            name.append(".");
        }
        return name;
    }
    return std::string{};
}

std::uint64_t CalibrationTreeWindow::TreeNode::ResolveAddress(bool &ret_Ok)
{
    const int pid{m_pOwner->GetPid()};
    if (!m_addressIsResolved)
    {
        if (nullptr == m_pParent)
        {
            ret_Ok = false;
            return 0;
        }
        bool ok{false};
        const std::uint64_t base{m_pParent->ResolveAddress(ok)};
        if (!ok)
        {
            ret_Ok = false;
            return 0;
        }
        m_address = base + m_address;
        m_addressIsResolved = true;
    }
    if (4 == m_what) // Pointer: return its target address.
    {
        if (m_pointsToAddressIsResolved)
        {
            ret_Ok = true;
            return m_pointsToAddress;
        }
        if (scm_read_bytes(m_address, pid, static_cast<char *>(static_cast<void *>(&m_pointsToAddress)),
                           sizeof(m_pointsToAddress)) == static_cast<int>(sizeof(m_pointsToAddress)))
        {
            m_pointsToAddressIsResolved = true;
            ret_Ok = true;
            return m_pointsToAddress;
        }
        ret_Ok = false;
        return 0;
    }
    ret_Ok = true;
    return m_pointsToAddress;
}

int CalibrationTreeWindow::TreeNode::DelayedRead()
{
    const int pid{m_pOwner->GetPid()};
    if (0 >= pid)
    {
        return -1;
    }
    if (!m_addressIsResolved)
    {
        bool ok{false};
        const std::uint64_t base{ResolveAddress(ok)};
        if (!ok)
        {
            return -1;
        }
        m_address = base + m_address;
        m_addressIsResolved = true;
    }
    union BB_VARI value;
    int type{BB_INVALID};
    value.uqw = 0;
    if (0 != ReadValueFromExternProcessViaAddress(m_address, m_pOwner->GetDebugInfos(), pid, m_typeNr,
                                                  &value, &type, nullptr))
    {
        return -1;
    }
    m_valueCachedFlag = true;
    m_valueCache = value;
    m_typeCache = static_cast<enum BB_DATA_TYPES>(type);
    return 0;
}

void CalibrationTreeWindow::TreeNode::WriteValue(const char *par_Text)
{
    if ((nullptr == par_Text) || (BB_INVALID == m_typeCache))
    {
        return;
    }
    union BB_VARI value;
    if (0 != string_to_bbvari(m_typeCache, &value, par_Text))
    {
        return;
    }
    const int pid{m_pOwner->GetPid()};
    (void)WriteValuesToExternProcessViaAddressStopScheduler(m_pOwner->GetDebugInfos(), pid, 1, &m_address,
                                                            &m_typeNr, &value, 5000, 1);
    m_valueCachedFlag = false; // Re-read on the next refresh cycle.
}

void CalibrationTreeWindow::TreeNode::InvalidateCache()
{
    m_valueCachedFlag = false;
    for (std::unique_ptr<TreeNode> &child : m_children)
    {
        child->InvalidateCache();
    }
}

// -------------------------------------------------------------------------
// CalibrationTreeWindow
// -------------------------------------------------------------------------

CalibrationTreeWindow::CalibrationTreeWindow(IWindowType *par_Type, std::string par_Name)
    : m_pType{par_Type}
{
    SetName(std::move(par_Name));
    m_uniqueNumber = AquireUniqueNumber();
    ResetTree();
}

CalibrationTreeWindow::~CalibrationTreeWindow()
{
    DisconnectDebugInfos();
    if (0 <= m_uniqueNumber)
    {
        FreeUniqueNumber(m_uniqueNumber);
        m_uniqueNumber = -1;
    }
    if (nullptr != m_pIncExcFilter)
    {
        FreeIncludeExcludeFilter(m_pIncExcFilter);
        m_pIncExcFilter = nullptr;
    }
}

void CalibrationTreeWindow::ResetTree()
{
    m_delayedReads.clear();
    m_root = std::make_unique<TreeNode>(this, -1, 0, false, 0, true, false, std::string{"label"},
                                        std::string{"type"}, nullptr);
}

void CalibrationTreeWindow::DisconnectDebugInfos()
{
    if (nullptr != m_pGlobalSectionFilter)
    {
        m_pGlobalSectionFilter = DeleteAddrRangeFilter(m_pGlobalSectionFilter);
    }
    if (nullptr != m_pDebugInfos)
    {
        (void)RemoveConnectFromProcessDebugInfos(m_uniqueNumber);
        m_pDebugInfos = nullptr;
    }
    m_pid = -1;
}

void CalibrationTreeWindow::ConnectDebugInfos()
{
    DisconnectDebugInfos();
    if (m_processName.empty())
    {
        ResetTree();
        return;
    }
    m_pid = -1;
    // No callback: process start/stop is polled in OnUpdateTick (render thread), so no
    // scheduler-thread marshalling is needed for the ImGui front-end.
    m_pDebugInfos = ConnectToProcessDebugInfos(m_uniqueNumber, m_processName.c_str(), &m_pid, nullptr,
                                               nullptr, this, 0);
    m_pGlobalSectionFilter = BuildGlobalAddrRangeFilter(m_pDebugInfos);
    ResetTree();
}

void CalibrationTreeWindow::SetProcessName(const std::string &par_ProcessName)
{
    m_processName = par_ProcessName;
    std::snprintf(m_processBuffer, sizeof(m_processBuffer), "%s", m_processName.c_str());
    ConnectDebugInfos();
}

void CalibrationTreeWindow::QueueDelayedRead(TreeNode *par_Node)
{
    for (const TreeNode *pExisting : m_delayedReads)
    {
        if (pExisting == par_Node)
        {
            return;
        }
    }
    m_delayedReads.push_back(par_Node);
}

void CalibrationTreeWindow::ExecuteDelayedReads()
{
    if (0 >= m_pid)
    {
        m_delayedReads.clear();
        return;
    }
    if (m_delayedReads.empty())
    {
        return;
    }
    // Same lock discipline as the Qt CalibrationTreeModel::ExecuteDelayedReads: short wait, no
    // error dialog, batch-read every collected node while the scheduler is halted, then unlock.
    if (0 == WaitUntilProcessIsNotActiveAndThanLockIt(m_pid, VALUE_TIMEOUT_LOCK_MS,
                                                      ERROR_BEHAVIOR_NO_ERROR_MESSAGE, "", __FILE__,
                                                      __LINE__))
    {
        for (TreeNode *pNode : m_delayedReads)
        {
            if (0 != pNode->DelayedRead())
            {
                pNode->SetAddressInvalid();
            }
        }
        UnLockProcess(m_pid);
        m_delayedReads.clear();
    }
}

void CalibrationTreeWindow::OnUpdateTick(const WindowContext &par_Context)
{
    (void)par_Context;

    if (m_processName.empty())
    {
        return;
    }

    // Poll for process start/stop (replaces the Qt scheduler-thread notify callback).
    const int currentPid{static_cast<int>(get_pid_by_name(m_processName.c_str()))};
    if (currentPid != m_pid)
    {
        m_pid = currentPid;
        if (0 < m_pid)
        {
            if (nullptr != m_pGlobalSectionFilter)
            {
                m_pGlobalSectionFilter = DeleteAddrRangeFilter(m_pGlobalSectionFilter);
            }
            m_pGlobalSectionFilter = BuildGlobalAddrRangeFilter(m_pDebugInfos);
        }
        ResetTree();
    }

    // Execute the delayed reads collected while rendering the previous frame.
    ExecuteDelayedReads();
}

void CalibrationTreeWindow::OnAcceptVariableDrop(const std::string &par_VariableName)
{
    // The Qt calibration tree is not a blackboard drop target (all change* slots are no-ops).
    (void)par_VariableName;
}

void CalibrationTreeWindow::RenderNode(TreeNode &par_Node, int par_ValueColumn, int par_AddressColumn,
                                       int par_TypeColumn, int &io_RowId)
{
    const int childCount{par_Node.ChildCount()};
    const bool isLeaf{0 == childCount};

    ImGui::TableNextRow();
    ImGui::PushID(io_RowId++);

    ImGui::TableSetColumnIndex(LABEL_COLUMN);
    ImGuiTreeNodeFlags flags{ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnArrow |
                             ImGuiTreeNodeFlags_OpenOnDoubleClick};
    if (isLeaf)
    {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    const bool open{ImGui::TreeNodeEx(par_Node.GetLabel().c_str(), flags)};

    if (0 <= par_ValueColumn)
    {
        ImGui::TableSetColumnIndex(par_ValueColumn);
        bool needsRead{false};
        const std::string value{par_Node.GetValueString(needsRead)};
        if (needsRead)
        {
            QueueDelayedRead(&par_Node);
        }
        if (par_Node.IsValidBaseElement())
        {
            char editBuffer[128];
            std::snprintf(editBuffer, sizeof(editBuffer), "%s", value.c_str());
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##val", editBuffer, sizeof(editBuffer),
                                 ImGuiInputTextFlags_EnterReturnsTrue))
            {
                par_Node.WriteValue(editBuffer);
            }
        }
        else
        {
            ImGui::TextUnformatted(value.c_str());
        }
    }
    if (0 <= par_AddressColumn)
    {
        ImGui::TableSetColumnIndex(par_AddressColumn);
        ImGui::TextUnformatted(par_Node.GetAddressString().c_str());
    }
    if (0 <= par_TypeColumn)
    {
        ImGui::TableSetColumnIndex(par_TypeColumn);
        ImGui::TextUnformatted(par_Node.GetDataType().c_str());
    }

    if (open && !isLeaf)
    {
        for (std::unique_ptr<TreeNode> &child : par_Node.GetChildren())
        {
            RenderNode(*child, par_ValueColumn, par_AddressColumn, par_TypeColumn, io_RowId);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

void CalibrationTreeWindow::RenderContent(const WindowContext &par_Context)
{
    (void)par_Context;

    // F5 reloads every value (mirrors the Qt keyPressEvent -> ReloadValuesSlot).
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_F5))
    {
        if (nullptr != m_root)
        {
            m_root->InvalidateCache();
        }
    }

    if (m_processName.empty())
    {
        ImGui::TextDisabled("No process selected (right-click -> Settings).");
        return;
    }
    if (0 >= m_pid)
    {
        ImGui::TextDisabled("Process \"%s\" is not running.", m_processName.c_str());
        return;
    }

    int columnCount{1};
    int valueColumn{-1};
    int addressColumn{-1};
    int typeColumn{-1};
    if (0 != m_showValues)
    {
        valueColumn = columnCount++;
    }
    if (0 != m_showAddress)
    {
        addressColumn = columnCount++;
    }
    if (0 != m_showType)
    {
        typeColumn = columnCount++;
    }

    const ImGuiTableFlags tableFlags{ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                     ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY};
    if (ImGui::BeginTable("##caltreetable", columnCount, tableFlags))
    {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch);
        if (0 <= valueColumn)
        {
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        }
        if (0 <= addressColumn)
        {
            ImGui::TableSetupColumn("address", ImGuiTableColumnFlags_WidthFixed);
        }
        if (0 <= typeColumn)
        {
            ImGui::TableSetupColumn("type", ImGuiTableColumnFlags_WidthFixed);
        }
        ImGui::TableHeadersRow();

        int rowId{0};
        if (nullptr != m_root)
        {
            for (std::unique_ptr<TreeNode> &child : m_root->GetChildren())
            {
                RenderNode(*child, valueColumn, addressColumn, typeColumn, rowId);
            }
        }
        ImGui::EndTable();
    }
}

void CalibrationTreeWindow::RenderSettingsPopup()
{
    ImGui::TextUnformatted("Process:");
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("##process", m_processBuffer, sizeof(m_processBuffer));
    ImGui::SameLine();
    if (ImGui::Button("Set process"))
    {
        const std::string newProcess{m_processBuffer};
        if (0 != Compare2ProcessNames(newProcess.c_str(), m_processName.c_str()))
        {
            SetProcessName(newProcess);
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Reload values (F5)"))
    {
        if (nullptr != m_root)
        {
            m_root->InvalidateCache();
        }
    }

    ImGui::Separator();
    bool showValues{0 != m_showValues};
    bool showAddress{0 != m_showAddress};
    bool showType{0 != m_showType};
    bool hexOrDec{0 != m_hexOrDec};
    if (ImGui::Checkbox("Show values", &showValues))
    {
        m_showValues = showValues ? 1 : 0;
    }
    if (ImGui::Checkbox("Show address", &showAddress))
    {
        m_showAddress = showAddress ? 1 : 0;
    }
    if (ImGui::Checkbox("Show type", &showType))
    {
        m_showType = showType ? 1 : 0;
    }
    if (ImGui::Checkbox("Hexadecimal values", &hexOrDec))
    {
        m_hexOrDec = hexOrDec ? 1 : 0;
        if (nullptr != m_root)
        {
            m_root->InvalidateCache();
        }
    }
}

// -------------------------------------------------------------------------
// Config round-trip (Qt "GUI/Widgets/<name>" section, verbatim keys)
// -------------------------------------------------------------------------

void CalibrationTreeWindow::WriteConfig(int par_FileDescriptor, const char *par_Section)
{
    (void)IniFileDataBaseWriteString(par_Section, "type", m_pType->GetTypeName(), par_FileDescriptor);
    (void)IniFileDataBaseWriteString(par_Section, "process", m_processName.c_str(), par_FileDescriptor);
    (void)IniFileDataBaseWriteString(par_Section, "filter", m_filter.c_str(), par_FileDescriptor);

    char txt[64];
    std::snprintf(txt, sizeof(txt), "%d", m_constOnly);
    (void)IniFileDataBaseWriteString(par_Section, "const only", txt, par_FileDescriptor);
    std::snprintf(txt, sizeof(txt), "%d", m_showValues);
    (void)IniFileDataBaseWriteString(par_Section, "show values", txt, par_FileDescriptor);
    std::snprintf(txt, sizeof(txt), "%d", m_showAddress);
    (void)IniFileDataBaseWriteString(par_Section, "show address", txt, par_FileDescriptor);
    std::snprintf(txt, sizeof(txt), "%d", m_showType);
    (void)IniFileDataBaseWriteString(par_Section, "show type", txt, par_FileDescriptor);
    std::snprintf(txt, sizeof(txt), "%d", m_hexOrDec);
    (void)IniFileDataBaseWriteString(par_Section, "hex or dec", txt, par_FileDescriptor);

    // Column widths are managed by ImGui at runtime; the persisted Qt values are round-tripped.
    if (0 <= m_columnWidthLabel)
    {
        std::snprintf(txt, sizeof(txt), "%d", m_columnWidthLabel);
        (void)IniFileDataBaseWriteString(par_Section, "column width label", txt, par_FileDescriptor);
    }
    if (0 <= m_columnWidthValue)
    {
        std::snprintf(txt, sizeof(txt), "%d", m_columnWidthValue);
        (void)IniFileDataBaseWriteString(par_Section, "column width value", txt, par_FileDescriptor);
    }
    if (0 <= m_columnWidthAddress)
    {
        std::snprintf(txt, sizeof(txt), "%d", m_columnWidthAddress);
        (void)IniFileDataBaseWriteString(par_Section, "column width address", txt, par_FileDescriptor);
    }
    if (0 <= m_columnWidthType)
    {
        std::snprintf(txt, sizeof(txt), "%d", m_columnWidthType);
        (void)IniFileDataBaseWriteString(par_Section, "column width type", txt, par_FileDescriptor);
    }

    if (nullptr != m_pIncExcFilter)
    {
        (void)SaveIncludeExcludeListsToIni(m_pIncExcFilter, par_Section, "", par_FileDescriptor);
    }
}

void CalibrationTreeWindow::ReadConfig(int par_FileDescriptor, const char *par_Section)
{
    char processBuffer[MAX_PATH];
    (void)IniFileDataBaseReadString(par_Section, "process", "", processBuffer, sizeof(processBuffer),
                                    par_FileDescriptor);

    char filterBuffer[512];
    (void)IniFileDataBaseReadString(par_Section, "filter", "*", filterBuffer, sizeof(filterBuffer),
                                    par_FileDescriptor);
    m_filter = filterBuffer;

    m_constOnly = IniFileDataBaseReadInt(par_Section, "const only", 0, par_FileDescriptor);
    m_showValues = IniFileDataBaseReadInt(par_Section, "show values", 1, par_FileDescriptor);
    m_showAddress = IniFileDataBaseReadInt(par_Section, "show address", 0, par_FileDescriptor);
    m_showType = IniFileDataBaseReadInt(par_Section, "show type", 0, par_FileDescriptor);
    m_hexOrDec = IniFileDataBaseReadInt(par_Section, "hex or dec", 0, par_FileDescriptor);

    m_columnWidthLabel = IniFileDataBaseReadInt(par_Section, "column width label", -1, par_FileDescriptor);
    m_columnWidthValue = IniFileDataBaseReadInt(par_Section, "column width value", -1, par_FileDescriptor);
    m_columnWidthAddress =
        IniFileDataBaseReadInt(par_Section, "column width address", -1, par_FileDescriptor);
    m_columnWidthType = IniFileDataBaseReadInt(par_Section, "column width type", -1, par_FileDescriptor);

    if (nullptr != m_pIncExcFilter)
    {
        FreeIncludeExcludeFilter(m_pIncExcFilter);
    }
    m_pIncExcFilter = BuildIncludeExcludeFilterFromIni(par_Section, "", par_FileDescriptor);

    SetProcessName(processBuffer);
}

} // namespace XilEnvImGui
