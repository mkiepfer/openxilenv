#include "Framework/BoundVariable.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
#include "Blackboard.h"
#include "BlackboardAccess.h"
#include "IniDataBase.h"
}

namespace XilEnvImGui
{

namespace
{

constexpr double MIN_RANGE_EPSILON{0.0001};

/**
 * @brief Trims leading and trailing ASCII whitespace.
 */
std::string Trim(const std::string &par_Text)
{
    std::size_t begin{0};
    std::size_t end{par_Text.size()};
    while ((begin < end) && (0 != std::isspace(static_cast<unsigned char>(par_Text[begin]))))
    {
        ++begin;
    }
    while ((end > begin) && (0 != std::isspace(static_cast<unsigned char>(par_Text[end - 1]))))
    {
        --end;
    }
    return par_Text.substr(begin, end - begin);
}

} // namespace

BoundVariable::~BoundVariable()
{
    Unbind();
}

void BoundVariable::RefreshMetadata()
{
    if (0 < m_vid)
    {
        m_min = get_bbvari_min(m_vid);
        m_max = get_bbvari_max(m_vid);
        if (m_max <= (m_min + MIN_RANGE_EPSILON))
        {
            m_max = m_min + 1.0;
        }
        m_color = get_bbvari_color(m_vid);
        m_precision = get_bbvari_format_prec(m_vid);
        if (0 > m_precision)
        {
            m_precision = 0;
        }
    }
}

void BoundVariable::Bind(const std::string &par_Name)
{
    Unbind();
    m_name = Trim(par_Name);
    if (m_name.empty())
    {
        return;
    }
    const VID vid{add_bbvari(m_name.c_str(), BB_UNKNOWN_WAIT, nullptr)};
    m_vid = (0 < vid) ? vid : 0;
    RefreshMetadata();
}

void BoundVariable::BindFromPayload(const BlackboardVariablePayload &par_Payload)
{
    Bind(par_Payload.name);
    if (par_Payload.hasMinMax)
    {
        SetMinMax(par_Payload.minValue, par_Payload.maxValue);
    }
    if (par_Payload.hasColor)
    {
        m_color = par_Payload.color;
    }
    if (DragDropDisplayMode::Physical == par_Payload.displayMode)
    {
        m_physical = true;
    }
    else if ((DragDropDisplayMode::Decimal == par_Payload.displayMode) ||
             (DragDropDisplayMode::Hex == par_Payload.displayMode) ||
             (DragDropDisplayMode::Binary == par_Payload.displayMode))
    {
        m_physical = false;
    }
}

void BoundVariable::Unbind()
{
    if (0 < m_vid)
    {
        (void)remove_bbvari(m_vid);
        m_vid = 0;
    }
    m_name.clear();
}

double BoundVariable::ReadValue() const
{
    if (0 >= m_vid)
    {
        return m_min;
    }
    if (BB_UNKNOWN_WAIT == static_cast<enum BB_DATA_TYPES>(get_bbvaritype(m_vid)))
    {
        return m_min;
    }
    double value{0.0};
    if (m_physical && (BB_CONV_FORMULA == get_bbvari_conversiontype(m_vid)))
    {
        value = read_bbvari_equ(m_vid);
    }
    else
    {
        value = read_bbvari_convert_double(m_vid);
    }
    if (value > m_max)
    {
        value = m_max;
    }
    if (value < m_min)
    {
        value = m_min;
    }
    return value;
}

void BoundVariable::WriteValue(double par_Value) const
{
    if (0 >= m_vid)
    {
        return;
    }
    if (m_physical && (BB_CONV_FORMULA == get_bbvari_conversiontype(m_vid)))
    {
        (void)write_bbvari_phys_minmax_check(m_vid, par_Value);
    }
    else
    {
        write_bbvari_minmax_check(m_vid, par_Value);
    }
}

void BoundVariable::SetMinMax(double par_Min, double par_Max)
{
    m_min = par_Min;
    m_max = par_Max;
    if (m_max <= (m_min + MIN_RANGE_EPSILON))
    {
        m_max = m_min + 1.0;
    }
}

void BoundVariable::ReadUnit(char *par_Buffer, int par_Size) const
{
    if ((nullptr == par_Buffer) || (0 >= par_Size))
    {
        return;
    }
    par_Buffer[0] = '\0';
    if (0 < m_vid)
    {
        (void)get_bbvari_unit(m_vid, par_Buffer, par_Size);
    }
}

void BoundVariable::WriteQtConfig(int par_FileDescriptor, const char *par_Section) const
{
    if (0 >= m_vid)
    {
        (void)IniFileDataBaseWriteString(par_Section, "variable", nullptr, par_FileDescriptor);
        return;
    }
    const std::int32_t red{m_color & 0x000000FF};
    const std::int32_t green{(m_color & 0x0000FF00) >> 8};
    const std::int32_t blue{(m_color & 0x00FF0000) >> 16};
    char line[BBVARI_NAME_SIZE + 128];
    std::snprintf(line, sizeof(line), "%s, %f, %f, (%d,%d,%d), %s", m_name.c_str(), m_min, m_max, red,
                  green, blue, m_physical ? "phys" : "dec");
    (void)IniFileDataBaseWriteString(par_Section, "variable", line, par_FileDescriptor);
}

void BoundVariable::ReadQtConfig(int par_FileDescriptor, const char *par_Section)
{
    Unbind();

    char line[BBVARI_NAME_SIZE + 1024];
    if (0 == IniFileDataBaseReadString(par_Section, "variable", "", line, sizeof(line), par_FileDescriptor))
    {
        return;
    }

    // Format: "name, lower, upper, (r,g,b), phys|dec"
    std::string text{line};
    auto nextField{[&text](std::size_t &pos) -> std::string {
        const std::size_t comma{text.find(',', pos)};
        const std::size_t end{(std::string::npos == comma) ? text.size() : comma};
        std::string field{text.substr(pos, end - pos)};
        pos = (std::string::npos == comma) ? text.size() : comma + 1;
        return Trim(field);
    }};

    std::size_t pos{0};
    const std::string name{nextField(pos)};
    Bind(name);
    if (!IsBound())
    {
        return;
    }

    if (pos < text.size())
    {
        const std::string lowerStr{nextField(pos)};
        const std::string upperStr{nextField(pos)};
        if (!lowerStr.empty() && !upperStr.empty())
        {
            SetMinMax(std::atof(lowerStr.c_str()), std::atof(upperStr.c_str()));
        }
    }

    // Colour "(r,g,b)" spans three comma-separated fields; parse the raw digits.
    std::int32_t red{0};
    std::int32_t green{0};
    std::int32_t blue{0};
    const std::size_t open{text.find('(', pos)};
    if (std::string::npos != open)
    {
        const std::size_t close{text.find(')', open)};
        if (std::string::npos != close)
        {
            std::string rgb{text.substr(open + 1, close - open - 1)};
            (void)std::sscanf(rgb.c_str(), "%d , %d , %d", &red, &green, &blue);
            m_color = (blue << 16) | (green << 8) | red;
            pos = close + 1;
        }
    }

    const std::size_t modePos{text.find("phys", pos)};
    m_physical = (std::string::npos != modePos);
}

} // namespace XilEnvImGui
