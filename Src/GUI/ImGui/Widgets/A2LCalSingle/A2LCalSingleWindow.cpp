#include "Widgets/A2LCalSingle/A2LCalSingleWindow.h"

#include <cctype>
#include <cfloat>
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
#include "Framework/WindowManagerBridge.h"

#include "A2LCalWidgetSync.h"

extern "C" {
#include "A2LLinkThread.h"
#include "A2LValue.h"
#include "BlackboardConversion.h"
#include "EquationParser.h"
#include "IniDataBase.h"
#include "MainValues.h"
#include "MyMemory.h"
#include "Scheduler.h"
#include "TextReplace.h"
}

namespace XilEnvImGui
{

namespace
{

constexpr uint64_t FETCH_CHANNEL_TIMEOUT_NS{100000000u};
constexpr int LABEL_NAME_COLUMN{0};
constexpr int VALUE_COLUMN{1};

// Qt::Alignment flag bits (mirrors Qt's A2LCalSingleWidget::readFromIni defaults).
constexpr int QT_ALIGN_LEFT{0x0001};
constexpr int QT_ALIGN_RIGHT{0x0002};
constexpr int QT_ALIGN_HCENTER{0x0004};
constexpr int QT_ALIGN_VCENTER{0x0080};
constexpr int DEFAULT_ALIGN_NAME{QT_ALIGN_VCENTER | QT_ALIGN_LEFT};
constexpr int DEFAULT_ALIGN_VALUE{QT_ALIGN_VCENTER | QT_ALIGN_RIGHT};
constexpr int DEFAULT_ALIGN_UNIT{QT_ALIGN_VCENTER | QT_ALIGN_HCENTER};
constexpr int DEFAULT_ALIGN_DISPLAYTYPE{QT_ALIGN_VCENTER | QT_ALIGN_LEFT};

/**
 * @brief Case-insensitive substring test used by the settings-popup characteristic filter.
 * @param[in] par_Haystack Candidate characteristic name. Must not be nullptr.
 * @param[in] par_Needle Filter text; an empty needle matches everything. Must not be nullptr.
 * @return true when par_Needle occurs in par_Haystack ignoring ASCII case.
 */
bool ContainsCaseInsensitive(const char *par_Haystack, const char *par_Needle)
{
    if ('\0' == par_Needle[0])
    {
        return true;
    }
    for (const char *pStart{par_Haystack}; '\0' != *pStart; ++pStart)
    {
        const char *pHay{pStart};
        const char *pNeedle{par_Needle};
        while (('\0' != *pNeedle) &&
               (std::tolower(static_cast<unsigned char>(*pHay)) ==
                std::tolower(static_cast<unsigned char>(*pNeedle))))
        {
            ++pHay;
            ++pNeedle;
        }
        if ('\0' == *pNeedle)
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief Formats a double with the round-trip precision used by the Qt widget (%.*g, 15..18).
 */
std::string DoubleToString(double par_Value)
{
    char buffer[128];
    int prec{15};
    while (true)
    {
        std::snprintf(buffer, sizeof(buffer), "%.*g", prec, par_Value);
        if ((18 == prec++) || (par_Value == std::strtod(buffer, nullptr)))
        {
            break;
        }
    }
    return buffer;
}

/**
 * @brief Frees the my_malloc-owned strings inside an axis-info descriptor and zeroes them.
 */
void FreeAxisInfo(CHARACTERISTIC_AXIS_INFO &par_Info)
{
    if (nullptr != par_Info.m_Unit)
    {
        my_free(par_Info.m_Unit);
        par_Info.m_Unit = nullptr;
    }
    if (nullptr != par_Info.m_Input)
    {
        my_free(par_Info.m_Input);
        par_Info.m_Input = nullptr;
    }
    if (nullptr != par_Info.m_Conversion)
    {
        my_free(par_Info.m_Conversion);
        par_Info.m_Conversion = nullptr;
    }
    par_Info.m_ConvType = 0;
}

/**
 * @brief Strips a leading base prefix ("0x"/"0b", optional sign) so strtoll/strtoull can parse.
 */
const char *StripBasePrefix(const char *par_Text, int par_Base)
{
    const char *p{par_Text};
    while ((' ' == *p) || ('\t' == *p))
    {
        ++p;
    }
    if (2 == par_Base)
    {
        const char *q{p};
        if (('-' == *q) || ('+' == *q))
        {
            ++q;
        }
        if (('0' == q[0]) && (('b' == q[1]) || ('B' == q[1])))
        {
            // Drop the "0b" but keep any sign for the caller's negation handling below.
            return q + 2;
        }
    }
    return p;
}

} // namespace

// -------------------------------------------------------------------------
// A2LCalSingleWindowType
// -------------------------------------------------------------------------

std::unique_ptr<IWindowInstance> A2LCalSingleWindowType::CreateNew(const std::string &par_Title)
{
    return std::make_unique<A2LCalSingleWindow>(this, par_Title);
}

std::unique_ptr<IWindowInstance> A2LCalSingleWindowType::CreateFromConfig(int par_FileDescriptor,
                                                                          const char *par_Section,
                                                                          const std::string &par_Name)
{
    auto window = std::make_unique<A2LCalSingleWindow>(this, par_Name);
    window->ReadConfig(par_FileDescriptor, par_Section);
    return window;
}

// -------------------------------------------------------------------------
// A2LCalSingleWindow
// -------------------------------------------------------------------------

A2LCalSingleWindow::A2LCalSingleWindow(IWindowType *par_Type, std::string par_Name) : m_pType{par_Type}
{
    SetName(std::move(par_Name));
    m_showUnitColumn = (0 != s_main_ini_val.TextDefaultShowUnitColumn);
    m_showDisplayTypeColumn = (0 != s_main_ini_val.TextDefaultShowDispayTypeColumn);
    A2LCalBridge::GetInstance().RegisterWidget(this);
}

A2LCalSingleWindow::~A2LCalSingleWindow()
{
    A2LCalBridge::GetInstance().UnregisterWidget(this);
    ClearElements();
    CloseLink();
}

void A2LCalSingleWindow::ClearElements()
{
    for (Element &element : m_elements)
    {
        if (nullptr != element.pData)
        {
            FreeA2lData(element.pData);
            element.pData = nullptr;
        }
        FreeAxisInfo(element.info);
    }
    m_elements.clear();
}

void A2LCalSingleWindow::CloseLink()
{
    if (0 <= m_dataFromLinkChannelNo)
    {
        (void)DeleteDataFromLinkChannel(m_dataFromLinkChannelNo);
        m_dataFromLinkChannelNo = -1;
    }
    if (0 < m_linkNo)
    {
        (void)A2LCloseLink(m_linkNo, m_processName.c_str());
        m_linkNo = -1;
    }
}

void A2LCalSingleWindow::SetProcessName(const std::string &par_ProcessName)
{
    m_processName = par_ProcessName;
    SetupLinkTo();
}

void A2LCalSingleWindow::SetupLinkTo()
{
    m_linkNo = A2LGetLinkToExternProcess(m_processName.c_str());
    if (0 <= m_dataFromLinkChannelNo)
    {
        (void)DeleteDataFromLinkChannel(m_dataFromLinkChannelNo);
        m_dataFromLinkChannelNo = -1;
    }
    if (0 < m_linkNo)
    {
        m_dataFromLinkChannelNo = NewDataFromLinkChannel(m_linkNo, FETCH_CHANNEL_TIMEOUT_NS,
                                                         GlobalNotifiyGetDataFromLinkAckCallback, this);
        for (Element &element : m_elements)
        {
            (void)CheckExistance(element);
        }
    }
}

bool A2LCalSingleWindow::IsSingleValue(const Element &par_Element)
{
    if (nullptr == par_Element.pData)
    {
        return false;
    }
    return (1 == GetLinkDataType(par_Element.pData));
}

bool A2LCalSingleWindow::IsWritable(const Element &par_Element)
{
    if (!IsSingleValue(par_Element))
    {
        return false;
    }
    A2L_SINGLE_VALUE *pValue{GetLinkSingleValueData(par_Element.pData)};
    if (nullptr == pValue)
    {
        return false;
    }
    return (0 == (pValue->Flags & (A2L_VALUE_FLAG_READ_ONLY | A2L_VALUE_FLAG_ONLY_VIRTUAL)));
}

bool A2LCalSingleWindow::CheckExistance(Element &par_Element)
{
    par_Element.linkNo = m_linkNo;
    if (0 < m_linkNo)
    {
        if (0 > par_Element.index)
        {
            par_Element.index = A2LGetIndexFromLink(m_linkNo, par_Element.name.c_str(),
                                                    A2L_LABEL_TYPE_SINGLE_VALUE_CALIBRATION);
            if (0 <= par_Element.index)
            {
                FreeAxisInfo(par_Element.info);
                if (0 != A2LGetCharacteristicAxisInfos(m_linkNo, par_Element.index, 0, &par_Element.info))
                {
                    par_Element.index = -1;
                    par_Element.exists = false;
                }
                else
                {
                    par_Element.exists = true;
                }
            }
            else
            {
                par_Element.exists = true;
            }
        }
        else
        {
            par_Element.exists = true;
        }
    }
    else
    {
        par_Element.exists = false;
    }
    if (-1 == par_Element.displayType)
    {
        // Default to the physical display type when a conversion is available, else raw.
        par_Element.displayType = (0 < par_Element.info.m_ConvType) ? 3 : 0;
    }
    par_Element.physical = (3 == par_Element.displayType);
    return par_Element.exists;
}

void A2LCalSingleWindow::SetDisplayType(Element &par_Element, int par_DisplayType)
{
    par_Element.displayType = par_DisplayType;
    par_Element.physical = (3 == par_DisplayType);
}

void A2LCalSingleWindow::AddCharacteristic(const std::string &par_Name, int par_Row, int par_DisplayType)
{
    if (par_Name.empty())
    {
        return;
    }
    // Reject duplicates (mirrors the Qt "not already registered" drop guard).
    for (const Element &existing : m_elements)
    {
        if (0 == existing.name.compare(par_Name))
        {
            return;
        }
    }

    Element element;
    element.name = par_Name;
    element.displayType = par_DisplayType; // -1 => auto-detected in CheckExistance

    size_t insertPos{m_elements.size()};
    if ((0 <= par_Row) && (par_Row < static_cast<int>(m_elements.size())))
    {
        insertPos = static_cast<size_t>(par_Row);
    }
    m_elements.insert(m_elements.begin() + static_cast<std::ptrdiff_t>(insertPos), std::move(element));

    (void)CheckExistance(m_elements[insertPos]);
    UpdateOneValueReq(static_cast<int>(insertPos), true);
}

void A2LCalSingleWindow::UpdateAllValuesReq(bool par_UpdateAlways)
{
    const int count{static_cast<int>(m_elements.size())};
    if ((0 >= m_linkNo) || (0 > m_dataFromLinkChannelNo) || (0 == count))
    {
        return;
    }
    INDEX_DATA_BLOCK *pIdb{GetIndexDataBlock(count)};
    if (nullptr == pIdb)
    {
        return;
    }
    for (int i{0}; i < count; ++i)
    {
        Element &element{m_elements[static_cast<size_t>(i)]};
        pIdb->Data[i].LinkNo = element.linkNo;
        pIdb->Data[i].Index = element.index;
        pIdb->Data[i].Data = element.pData;
        pIdb->Data[i].User = static_cast<uint64_t>(static_cast<uint32_t>(i)) |
                             (static_cast<uint64_t>(par_UpdateAlways ? 1u : 0u) << 32);
        pIdb->Data[i].Flags = 0;
    }
    (void)A2LGetDataFromLinkReq(m_dataFromLinkChannelNo, 0, pIdb);
}

void A2LCalSingleWindow::UpdateOneValueReq(int par_Row, bool par_UpdateAlways)
{
    const int count{static_cast<int>(m_elements.size())};
    if ((0 >= m_linkNo) || (0 > m_dataFromLinkChannelNo))
    {
        return;
    }
    if ((0 > par_Row) || (par_Row >= count))
    {
        return;
    }
    INDEX_DATA_BLOCK *pIdb{GetIndexDataBlock(1)};
    if (nullptr == pIdb)
    {
        return;
    }
    Element &element{m_elements[static_cast<size_t>(par_Row)]};
    pIdb->Data->LinkNo = element.linkNo;
    pIdb->Data->Index = element.index;
    pIdb->Data->Data = element.pData;
    pIdb->Data->User = static_cast<uint64_t>(static_cast<uint32_t>(par_Row)) |
                       (static_cast<uint64_t>(par_UpdateAlways ? 1u : 0u) << 32);
    pIdb->Data->Flags = 0;
    (void)A2LGetDataFromLinkReq(m_dataFromLinkChannelNo, 0, pIdb);
}

void A2LCalSingleWindow::WriteValueToLink(Element &par_Element, const char *par_Text)
{
    if (!IsWritable(par_Element) || (nullptr == par_Text))
    {
        return;
    }
    A2L_SINGLE_VALUE *pValue{GetLinkSingleValueData(par_Element.pData)};
    if (nullptr == pValue)
    {
        return;
    }

    if (3 == par_Element.displayType)
    {
        double rawValue{0.0};
        if (BB_CONV_TEXTREP == par_Element.info.m_ConvType)
        {
            // A textreplace "physical" input is an enum TEXT (e.g. "ON"); resolve it to its raw
            // value via the reverse lookup instead of strtod (which would silently yield 0).
            char textBuffer[256];
            std::snprintf(textBuffer, sizeof(textBuffer), "%s", par_Text);
            int64_t fromRaw{0};
            int64_t toRaw{0};
            if (0 != convert_textreplace2value(par_Element.info.m_Conversion, textBuffer, &fromRaw, &toRaw))
            {
                return; // Unknown enum text: do not write a bogus 0.
            }
            rawValue = static_cast<double>(fromRaw);
        }
        else
        {
            // Physical input: convert phys -> raw exactly like the Qt PhysValueInput editor.
            const double physValue{std::strtod(par_Text, nullptr)};
            rawValue = physValue;
            if (0 < par_Element.info.m_ConvType)
            {
                double realPhys{0.0};
                char *pErrString{nullptr};
                if (0 != Conv_PhysToRawFromString(
                             par_Element.info.m_Conversion,
                             static_cast<enum BB_CONV_TYPES>(par_Element.info.m_ConvType),
                             par_Element.name.c_str(), physValue, &rawValue, &realPhys, &pErrString))
                {
                    FreeErrStringBuffer(&pErrString);
                    return; // Do not write an unconvertible value.
                }
            }
        }
        // Round-to-nearest + range-check to the exact target type (mirrors the Qt
        // sc_convert_from_FloatOrInt64_to_BB_VARI path): e.g. raw 2.9999 -> 3 and out-of-range
        // doubles are clamped to the target's min/max instead of invoking a truncating cast.
        const uint32_t savedFlags{pValue->Flags};
        ConvertDoubleToRawValue(pValue->TargetType, rawValue, pValue);
        pValue->Flags = savedFlags;
        if (A2L_ELEM_TYPE_ERROR == pValue->Type)
        {
            return; // Unsupported target type.
        }
    }
    else
    {
        int base{10};
        if (1 == par_Element.displayType)
        {
            base = 16;
        }
        else if (2 == par_Element.displayType)
        {
            base = 2;
        }
        switch (pValue->Type)
        {
        case A2L_ELEM_TYPE_INT:
        {
            const char *p{StripBasePrefix(par_Text, base)};
            pValue->Value.Int = static_cast<int64_t>(std::strtoll(p, nullptr, base));
            break;
        }
        case A2L_ELEM_TYPE_UINT:
        {
            const char *p{StripBasePrefix(par_Text, base)};
            pValue->Value.Uint = static_cast<uint64_t>(std::strtoull(p, nullptr, base));
            break;
        }
        case A2L_ELEM_TYPE_DOUBLE:
        case A2L_ELEM_TYPE_PHYS_DOUBLE:
            pValue->Value.Double = std::strtod(par_Text, nullptr);
            break;
        default:
            return;
        }
    }
    pValue->Flags |= A2L_VALUE_FLAG_UPDATE;

    // Push the value back over the link (direction 1 = write).
    if ((0 < m_linkNo) && (0 <= m_dataFromLinkChannelNo))
    {
        INDEX_DATA_BLOCK *pIdb{GetIndexDataBlock(1)};
        if (nullptr != pIdb)
        {
            pIdb->Data->LinkNo = par_Element.linkNo;
            pIdb->Data->Index = par_Element.index;
            pIdb->Data->Data = par_Element.pData;
            pIdb->Data->Flags = 0;
            pIdb->Data->User = static_cast<uint64_t>(1u) << 32; // update always
            (void)A2LGetDataFromLinkReq(m_dataFromLinkChannelNo, 1, pIdb);
        }
    }
    par_Element.hasChanged = true;
    (void)ToString(par_Element, true);
}

std::string A2LCalSingleWindow::ConvertPhysToString(double par_RawValue,
                                                    const CHARACTERISTIC_AXIS_INFO &par_Info) const
{
    if (BB_CONV_TEXTREP == par_Info.m_ConvType)
    {
        char buffer[256];
        int color{0};
        if (0 == convert_value2textreplace(static_cast<int64_t>(par_RawValue), par_Info.m_Conversion, buffer,
                                           sizeof(buffer), &color))
        {
            return buffer;
        }
        return std::string();
    }

    double physValue{par_RawValue};
    if (0 < par_Info.m_ConvType)
    {
        char *pErrString{nullptr};
        if (0 != Conv_RawToPhysFromString(par_Info.m_Conversion,
                                          static_cast<enum BB_CONV_TYPES>(par_Info.m_ConvType), "",
                                          par_RawValue, &physValue, &pErrString))
        {
            FreeErrStringBuffer(&pErrString);
            physValue = par_RawValue; // fall back to the raw value on conversion error
        }
    }
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%f (p)", physValue);
    return buffer;
}

int A2LCalSingleWindow::ToString(Element &par_Element, bool par_UpdateAlways) const
{
    if (nullptr == par_Element.pData)
    {
        return 0;
    }
    A2L_SINGLE_VALUE *pValue{GetLinkSingleValueData(par_Element.pData)};
    if ((nullptr == pValue) || !par_Element.exists)
    {
        par_Element.valueStr = "doesn't exist";
        return 0;
    }
    if (!par_UpdateAlways && !par_Element.hasChanged)
    {
        return 0;
    }
    par_Element.hasChanged = false;

    char buffer[128];
    switch (par_Element.displayType)
    {
    case 0: // raw decimal
        switch (pValue->Type)
        {
        case A2L_ELEM_TYPE_DOUBLE:
            par_Element.valueStr = DoubleToString(pValue->Value.Double);
            break;
        case A2L_ELEM_TYPE_UINT:
            std::snprintf(buffer, sizeof(buffer), "%llu",
                          static_cast<unsigned long long>(pValue->Value.Uint));
            par_Element.valueStr = buffer;
            break;
        case A2L_ELEM_TYPE_INT:
            std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(pValue->Value.Int));
            par_Element.valueStr = buffer;
            break;
        default:
            par_Element.valueStr = "data type error";
            break;
        }
        break;
    case 1: // hex
        switch (pValue->Type)
        {
        case A2L_ELEM_TYPE_DOUBLE:
            par_Element.valueStr = DoubleToString(pValue->Value.Double);
            break;
        case A2L_ELEM_TYPE_UINT:
            std::snprintf(buffer, sizeof(buffer), "0x%llx",
                          static_cast<unsigned long long>(pValue->Value.Uint));
            par_Element.valueStr = buffer;
            break;
        case A2L_ELEM_TYPE_INT:
            if (0 <= pValue->Value.Int)
            {
                std::snprintf(buffer, sizeof(buffer), "0x%llx",
                              static_cast<unsigned long long>(pValue->Value.Int));
            }
            else
            {
                std::snprintf(buffer, sizeof(buffer), "-0x%llx",
                              static_cast<unsigned long long>(-pValue->Value.Int));
            }
            par_Element.valueStr = buffer;
            break;
        default:
            par_Element.valueStr = "data type error";
            break;
        }
        break;
    case 2: // binary
        switch (pValue->Type)
        {
        case A2L_ELEM_TYPE_DOUBLE:
            par_Element.valueStr = DoubleToString(pValue->Value.Double);
            break;
        case A2L_ELEM_TYPE_UINT:
        case A2L_ELEM_TYPE_INT:
        {
            const bool negative{(A2L_ELEM_TYPE_INT == pValue->Type) && (0 > pValue->Value.Int)};
            uint64_t magnitude{(A2L_ELEM_TYPE_UINT == pValue->Type)
                                   ? pValue->Value.Uint
                                   : static_cast<uint64_t>(negative ? -pValue->Value.Int : pValue->Value.Int)};
            char bits[72];
            int pos{static_cast<int>(sizeof(bits)) - 1};
            bits[pos] = '\0';
            if (0 == magnitude)
            {
                bits[--pos] = '0';
            }
            while ((0 != magnitude) && (0 < pos))
            {
                bits[--pos] = static_cast<char>('0' + (magnitude & 1u));
                magnitude >>= 1u;
            }
            par_Element.valueStr = negative ? "-0b" : "0b";
            par_Element.valueStr += &bits[pos];
            break;
        }
        default:
            par_Element.valueStr = "data type error";
            break;
        }
        break;
    case 3: // physical
        par_Element.valueStr = ConvertPhysToString(ConvertRawValueToDouble(pValue), par_Element.info);
        break;
    default:
        par_Element.valueStr = "unknown display type";
        break;
    }

    if (nullptr != par_Element.info.m_Unit)
    {
        par_Element.unit = par_Element.info.m_Unit;
    }
    else
    {
        par_Element.unit.clear();
    }
    return 1;
}

// -------------------------------------------------------------------------
// IA2LCalWidget (dispatched on the render thread by A2LCalBridge)
// -------------------------------------------------------------------------

void A2LCalSingleWindow::OnLinkDataChanged(int par_LinkNo, int par_Index, A2L_DATA *par_Data)
{
    for (int row{0}; row < static_cast<int>(m_elements.size()); ++row)
    {
        Element &element{m_elements[static_cast<size_t>(row)]};
        if ((par_LinkNo == element.linkNo) && (par_Index == element.index) && (par_Data != element.pData))
        {
            UpdateOneValueReq(row, true);
        }
    }
}

void A2LCalSingleWindow::OnProcessStartStop(const std::string &par_ProcessName, int par_Flags, int par_Action)
{
    (void)par_Flags;
    if (0 != Compare2ProcessNames(par_ProcessName.c_str(), m_processName.c_str()))
    {
        return;
    }
    if (0 == par_Action)
    {
        // Process terminated: invalidate every element (mirrors ProcessTerminated).
        for (Element &element : m_elements)
        {
            element.exists = false;
            element.index = -1;
            element.linkNo = -1;
            element.valueStr = "doesn't exist";
            FreeAxisInfo(element.info);
        }
    }
    else if (1 == par_Action)
    {
        SetupLinkTo();
        UpdateAllValuesReq(true);
    }
}

void A2LCalSingleWindow::OnGetDataFromLinkAck(void *par_IndexData, int par_FetchDataChannelNo)
{
    // Self-filter: ignore acknowledgements for channels this window did not register.
    if (par_FetchDataChannelNo != m_dataFromLinkChannelNo)
    {
        return;
    }
    INDEX_DATA_BLOCK *pIndexData{static_cast<INDEX_DATA_BLOCK *>(par_IndexData)};
    if (nullptr == pIndexData)
    {
        return;
    }
    const int elementCount{static_cast<int>(pIndexData->Size)};
    for (int i{0}; i < elementCount; ++i)
    {
        A2L_DATA *pNewData{static_cast<A2L_DATA *>(pIndexData->Data[i].Data)};
        if (nullptr == pNewData)
        {
            continue;
        }
        if (A2L_DATA_TYPE_VALUE != pNewData->Type)
        {
            continue; // Only single-value characteristics are shown here.
        }
        const bool updateAlways{0 != ((pIndexData->Data[i].User >> 32) & 0x1u)};
        for (Element &element : m_elements)
        {
            if ((pIndexData->Data[i].LinkNo != element.linkNo) ||
                (pIndexData->Data[i].Index != element.index))
            {
                continue;
            }
            // SetData: duplicate on first receipt, else replace only on change.
            if (nullptr == element.pData)
            {
                element.pData = DupA2lData(pNewData);
                element.hasChanged = true;
            }
            else
            {
                element.hasChanged = (0 == CompareIfA2lDataAreEqual(element.pData, pNewData));
                if (element.hasChanged)
                {
                    FreeA2lData(element.pData);
                    element.pData = DupA2lData(pNewData);
                }
            }
            if (0 <= element.index)
            {
                (void)ToString(element, updateAlways);
            }
        }
    }
}

// -------------------------------------------------------------------------
// Render / update
// -------------------------------------------------------------------------

void A2LCalSingleWindow::RefreshCharacteristicBrowser()
{
    m_browserCharacteristics.clear();
    m_browserSelected.clear();
    if (0 >= m_linkNo)
    {
        return;
    }
    char name[1024];
    int index{-1};
    while (0 <= (index = A2LGetNextSymbolFromLink(m_linkNo, index, A2L_LABEL_TYPE_SINGLE_VALUE_CALIBRATION,
                                                  "*", name, sizeof(name))))
    {
        m_browserCharacteristics.emplace_back(name);
    }
    m_browserSelected.assign(m_browserCharacteristics.size(), 0);
}

void A2LCalSingleWindow::OnUpdateTick(const WindowContext &par_Context)
{
    (void)par_Context;
    // Periodically refresh calibration values so external changes (model/XCP) show up.
    UpdateAllValuesReq(false);
}

void A2LCalSingleWindow::OnAcceptVariableDrop(const std::string &par_VariableName)
{
    // Dropped A2L labels default to the physical display type (Qt AddCharacteristicToModel default 3).
    AddCharacteristic(par_VariableName, -1, 3);
}

void A2LCalSingleWindow::RenderContent(const WindowContext &par_Context)
{
    (void)par_Context;

    int columnCount{2};
    if (m_showUnitColumn)
    {
        ++columnCount;
    }
    if (m_showDisplayTypeColumn)
    {
        ++columnCount;
    }

    const ImGuiTableFlags tableFlags{ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                     ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY};

    const ImVec2 avail{ImGui::GetContentRegionAvail()};
    ImGui::BeginChild("##a2lcalsinglecontent", avail, false);

    if (ImGui::BeginTable("##a2lcalsingletable", columnCount, tableFlags))
    {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        if (m_showUnitColumn)
        {
            ImGui::TableSetupColumn("Unit", ImGuiTableColumnFlags_WidthFixed);
        }
        if (m_showDisplayTypeColumn)
        {
            ImGui::TableSetupColumn("DisplayType", ImGuiTableColumnFlags_WidthFixed);
        }
        ImGui::TableHeadersRow();

        static const char *const DISPLAY_LABELS[]{"[dec]", "[hex]", "[bin]", "[phys]"};

        for (int row{0}; row < static_cast<int>(m_elements.size()); ++row)
        {
            Element &element{m_elements[static_cast<size_t>(row)]};
            ImGui::TableNextRow();
            ImGui::PushID(row);

            // Name column: draggable label (mirrors the Qt buildDragObject source).
            ImGui::TableSetColumnIndex(LABEL_NAME_COLUMN);
            ImGui::Selectable(element.name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
            {
                DragDropDisplayMode mode{DragDropDisplayMode::Physical};
                switch (element.displayType)
                {
                case 0:
                    mode = DragDropDisplayMode::Decimal;
                    break;
                case 1:
                    mode = DragDropDisplayMode::Hex;
                    break;
                case 2:
                    mode = DragDropDisplayMode::Binary;
                    break;
                case 3:
                default:
                    mode = DragDropDisplayMode::Physical;
                    break;
                }
                BlackboardVariablePayload payload;
                payload.name = element.name;
                payload.displayMode = mode;
                SetBlackboardVariableDragPayload(payload);
                ImGui::EndDragDropSource();
            }

            // Value column: editable when the label resolved to a writable single value.
            ImGui::TableSetColumnIndex(VALUE_COLUMN);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (IsWritable(element))
            {
                if (!element.editing)
                {
                    std::snprintf(element.editBuffer, sizeof(element.editBuffer), "%s",
                                  element.valueStr.c_str());
                }
                if (ImGui::InputText("##val", element.editBuffer, sizeof(element.editBuffer),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    WriteValueToLink(element, element.editBuffer);
                    element.editing = false;
                }
                else
                {
                    element.editing = ImGui::IsItemActive();
                }
            }
            else if (element.exists)
            {
                ImGui::TextUnformatted(element.valueStr.c_str());
            }
            else
            {
                ImGui::TextDisabled("doesn't exist");
            }

            int nextColumn{2};
            if (m_showUnitColumn)
            {
                ImGui::TableSetColumnIndex(nextColumn);
                ImGui::TextUnformatted(element.unit.c_str());
                ++nextColumn;
            }
            if (m_showDisplayTypeColumn)
            {
                ImGui::TableSetColumnIndex(nextColumn);
                int current{(0 <= element.displayType) && (3 >= element.displayType) ? element.displayType : 0};
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::Combo("##disp", &current, DISPLAY_LABELS, 4))
                {
                    SetDisplayType(element, current);
                    element.hasChanged = true;
                    (void)ToString(element, true);
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::EndChild();

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

void A2LCalSingleWindow::RenderSettingsPopup()
{
    // Seed the rename field and (re)build the characteristic browser when the popup first appears.
    if (ImGui::IsWindowAppearing())
    {
        std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", GetName().c_str());
        RefreshCharacteristicBrowser();
    }

    // --- Window rename (mirrors A2LCalSingleConfigDlg's WindowTitleLineEdit) ------------------
    ImGui::TextUnformatted("Window name:");
    ImGui::SetNextItemWidth(200.0f);
    const bool renameCommitted{ImGui::InputText("##rename", m_renameBuffer, sizeof(m_renameBuffer),
                                                ImGuiInputTextFlags_EnterReturnsTrue)};
    ImGui::SameLine();
    if (ImGui::Button("Rename") || renameCommitted)
    {
        const std::string newName{m_renameBuffer};
        if (!newName.empty() && (0 != newName.compare(GetName())))
        {
            // Route through the WindowManager so its name->instance map is re-keyed too (else
            // OpenWindowByName / dedup would lose the window). Ignores empty/duplicate names.
            (void)WmRenameWindow(GetName().c_str(), newName.c_str());
        }
    }

    ImGui::Separator();

    // --- Process selection (mirrors A2LSelectWidget's process list) ---------------------------
    ImGui::TextUnformatted("Process:");
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("##process", m_processBuffer, sizeof(m_processBuffer));
    ImGui::SameLine();
    if (ImGui::Button("Set process"))
    {
        const std::string newProcess{m_processBuffer};
        if (0 != Compare2ProcessNames(newProcess.c_str(), m_processName.c_str()))
        {
            // Changing the process removes all labels (one process per window, mirrors Qt).
            ClearElements();
            CloseLink();
            SetProcessName(newProcess);
            RefreshCharacteristicBrowser();
        }
    }
    // Pick from the processes that currently expose an A2L link (same 2|0x100 iterator as Qt).
    if (ImGui::BeginCombo("##processpick", "Select process..."))
    {
        READ_NEXT_PROCESS_NAME *pProcesses{init_read_next_process_name(2 | 0x100)};
        if (nullptr != pProcesses)
        {
            char *pProcessName{nullptr};
            while (nullptr != (pProcessName = read_next_process_name(pProcesses)))
            {
                if (0 < A2LGetLinkToExternProcess(pProcessName))
                {
                    if (ImGui::Selectable(pProcessName))
                    {
                        std::snprintf(m_processBuffer, sizeof(m_processBuffer), "%s", pProcessName);
                    }
                }
            }
            close_read_next_process_name(pProcesses);
        }
        ImGui::EndCombo();
    }

    ImGui::Separator();
    if (ImGui::Button("Update all"))
    {
        UpdateAllValuesReq(true);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Show unit column", &m_showUnitColumn);
    ImGui::SameLine();
    ImGui::Checkbox("Show display-type column", &m_showDisplayTypeColumn);

    ImGui::Separator();

    // --- A2L characteristic browser (multi-select) -------------------------------------------
    ImGui::TextUnformatted("A2L characteristics:");
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh"))
    {
        RefreshCharacteristicBrowser();
    }
    ImGui::SetNextItemWidth(300.0f);
    ImGui::InputTextWithHint("##browserfilter", "filter (substring)", m_browserFilter,
                             sizeof(m_browserFilter));
    ImGui::BeginChild("##browserlist", ImVec2(300.0f, 150.0f), true);
    if (m_browserCharacteristics.empty())
    {
        ImGui::TextDisabled("(no linked process / no characteristics)");
    }
    for (size_t i{0}; i < m_browserCharacteristics.size(); ++i)
    {
        const std::string &candidate{m_browserCharacteristics[i]};
        if (!ContainsCaseInsensitive(candidate.c_str(), m_browserFilter))
        {
            continue;
        }
        const bool selected{0 != m_browserSelected[i]};
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(candidate.c_str(), selected))
        {
            m_browserSelected[i] = selected ? 0 : 1; // toggle (multi-select)
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (ImGui::Button("Add selected"))
    {
        for (size_t i{0}; i < m_browserCharacteristics.size(); ++i)
        {
            if (0 != m_browserSelected[i])
            {
                AddCharacteristic(m_browserCharacteristics[i], -1, -1);
                m_browserSelected[i] = 0;
            }
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Add characteristic by name:");
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("##addlabel", m_addCharacteristicBuffer, sizeof(m_addCharacteristicBuffer));
    ImGui::SameLine();
    if (ImGui::Button("Add"))
    {
        AddCharacteristic(m_addCharacteristicBuffer, -1, -1);
        m_addCharacteristicBuffer[0] = '\0';
    }

    ImGui::Separator();
    int removeIndex{-1};
    for (int row{0}; row < static_cast<int>(m_elements.size()); ++row)
    {
        Element &element{m_elements[static_cast<size_t>(row)]};
        ImGui::PushID(2000 + row);
        ImGui::TextUnformatted(element.name.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
        {
            removeIndex = row;
        }
        ImGui::PopID();
    }
    if (0 <= removeIndex)
    {
        Element &element{m_elements[static_cast<size_t>(removeIndex)]};
        if (nullptr != element.pData)
        {
            FreeA2lData(element.pData);
        }
        FreeAxisInfo(element.info);
        m_elements.erase(m_elements.begin() + removeIndex);
    }
}

// -------------------------------------------------------------------------
// Config round-trip (Qt "GUI/Widgets/<name>" layout)
// -------------------------------------------------------------------------

void A2LCalSingleWindow::WriteConfig(int par_FileDescriptor, const char *par_Section)
{
    (void)IniFileDataBaseWriteString(par_Section, "type", m_pType->GetTypeName(), par_FileDescriptor);
    (void)IniFileDataBaseWriteString(par_Section, "process", m_processName.c_str(), par_FileDescriptor);

    if (!m_fontString.empty())
    {
        (void)IniFileDataBaseWriteString(par_Section, "Font", m_fontString.c_str(), par_FileDescriptor);
    }
    (void)IniFileDataBaseWriteInt(par_Section, "icon", m_icon, par_FileDescriptor);

    char colorString[32];
    std::snprintf(colorString, sizeof(colorString), "0x%08X", m_backgroundColor);
    (void)IniFileDataBaseWriteString(par_Section, "BgColor", colorString, par_FileDescriptor);

    // Characteristics are stored as E1..En = "<displayType>,<name>" (Qt is 1-based).
    int index{0};
    for (; index < static_cast<int>(m_elements.size()); ++index)
    {
        const Element &element{m_elements[static_cast<size_t>(index)]};
        char entry[32];
        std::snprintf(entry, sizeof(entry), "E%d", index + 1);
        char line[600];
        std::snprintf(line, sizeof(line), "%d,%s", element.displayType, element.name.c_str());
        (void)IniFileDataBaseWriteString(par_Section, entry, line, par_FileDescriptor);
    }
    // Drop stale entries left from a previously longer list.
    for (;; ++index)
    {
        char entry[32];
        std::snprintf(entry, sizeof(entry), "E%d", index + 1);
        char existing[600];
        if (0 == IniFileDataBaseReadString(par_Section, entry, "", existing, sizeof(existing),
                                           par_FileDescriptor))
        {
            break;
        }
        (void)IniFileDataBaseWriteString(par_Section, entry, nullptr, par_FileDescriptor);
    }

    (void)IniFileDataBaseWriteYesNo(par_Section, "ShowUnitColumn", m_showUnitColumn ? 1 : 0,
                                    par_FileDescriptor);
    (void)IniFileDataBaseWriteYesNo(par_Section, "ShowDisplayTypeColumn", m_showDisplayTypeColumn ? 1 : 0,
                                    par_FileDescriptor);

    (void)IniFileDataBaseWriteInt(par_Section, "ColumnAlignmentName", m_columnAlignmentName,
                                  par_FileDescriptor);
    (void)IniFileDataBaseWriteInt(par_Section, "ColumnAlignmentValue", m_columnAlignmentValue,
                                  par_FileDescriptor);
    (void)IniFileDataBaseWriteInt(par_Section, "ColumnAlignmentUnit", m_columnAlignmentUnit,
                                  par_FileDescriptor);
    (void)IniFileDataBaseWriteInt(par_Section, "ColumnAlignmentDisplayType", m_columnAlignmentDisplayType,
                                  par_FileDescriptor);
}

void A2LCalSingleWindow::ReadConfig(int par_FileDescriptor, const char *par_Section)
{
    ClearElements();
    CloseLink();

    char fontBuffer[256];
    if (0 < IniFileDataBaseReadString(par_Section, "Font", "", fontBuffer, sizeof(fontBuffer),
                                      par_FileDescriptor))
    {
        m_fontString = fontBuffer;
    }

    char processBuffer[256];
    (void)IniFileDataBaseReadString(par_Section, "process", "", processBuffer, sizeof(processBuffer),
                                    par_FileDescriptor);
    std::snprintf(m_processBuffer, sizeof(m_processBuffer), "%s", processBuffer);
    SetProcessName(processBuffer);

    m_icon = IniFileDataBaseReadInt(par_Section, "icon", 0, par_FileDescriptor);
    m_backgroundColor = static_cast<std::uint32_t>(
        IniFileDataBaseReadInt(par_Section, "BgColor", 0xFFFFFF, par_FileDescriptor));

    for (int i{1};; ++i)
    {
        char entry[32];
        std::snprintf(entry, sizeof(entry), "E%d", i);
        char line[600];
        if (0 == IniFileDataBaseReadString(par_Section, entry, "", line, sizeof(line), par_FileDescriptor))
        {
            break;
        }
        const std::string lineStr{line};
        const size_t comma{lineStr.find(',')};
        if (std::string::npos == comma)
        {
            continue;
        }
        const int displayType{std::atoi(lineStr.substr(0, comma).c_str())};
        const std::string name{lineStr.substr(comma + 1)};
        AddCharacteristic(name, -1, displayType);
    }

    m_columnAlignmentName =
        IniFileDataBaseReadInt(par_Section, "ColumnAlignmentName", DEFAULT_ALIGN_NAME, par_FileDescriptor);
    m_columnAlignmentValue =
        IniFileDataBaseReadInt(par_Section, "ColumnAlignmentValue", DEFAULT_ALIGN_VALUE, par_FileDescriptor);
    m_columnAlignmentUnit =
        IniFileDataBaseReadInt(par_Section, "ColumnAlignmentUnit", DEFAULT_ALIGN_UNIT, par_FileDescriptor);
    m_columnAlignmentDisplayType = IniFileDataBaseReadInt(par_Section, "ColumnAlignmentDisplayType",
                                                          DEFAULT_ALIGN_DISPLAYTYPE, par_FileDescriptor);

    m_showUnitColumn =
        (0 != IniFileDataBaseReadYesNo(par_Section, "ShowUnitColumn", m_showUnitColumn ? 1 : 0,
                                       par_FileDescriptor));
    m_showDisplayTypeColumn =
        (0 != IniFileDataBaseReadYesNo(par_Section, "ShowDisplayTypeColumn", m_showDisplayTypeColumn ? 1 : 0,
                                       par_FileDescriptor));
}

} // namespace XilEnvImGui
