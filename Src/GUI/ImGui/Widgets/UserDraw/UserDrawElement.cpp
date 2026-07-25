#include "Widgets/UserDraw/UserDrawElement.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

extern "C" {
#include "Blackboard.h"
#include "EquationParser.h"
#include "ExecutionStack.h"
#include "IniDataBase.h"
}

namespace XilEnvImGui
{

namespace
{

/**
 * @brief Clamps a channel value to [0,255] and returns it as an int (Qt SetColor semantics).
 */
int ClampChannel(double par_Value)
{
    if (par_Value < 0.0)
    {
        return 0;
    }
    if (255.0 < par_Value)
    {
        return 255;
    }
    return static_cast<int>(par_Value);
}

} // namespace

bool IEqualsAscii(const std::string &par_Value, const char *par_Literal)
{
    std::size_t index{0u};
    for (; index < par_Value.size(); ++index)
    {
        const char literalChar{par_Literal[index]};
        if ('\0' == literalChar)
        {
            return false;
        }
        char valueChar{par_Value[index]};
        char cmpChar{literalChar};
        if (('A' <= valueChar) && ('Z' >= valueChar))
        {
            valueChar = static_cast<char>(valueChar - 'A' + 'a');
        }
        if (('A' <= cmpChar) && ('Z' >= cmpChar))
        {
            cmpChar = static_cast<char>(cmpChar - 'A' + 'a');
        }
        if (valueChar != cmpChar)
        {
            return false;
        }
    }
    return '\0' == par_Literal[index];
}

// ---------------------------------------------------------------------------
// BaseValue
// ---------------------------------------------------------------------------

BaseValue::~BaseValue()
{
    Release();
}

BaseValue::BaseValue(BaseValue &&par_Other) noexcept
    : m_isFixedValue{par_Other.m_isFixedValue}, m_fixed{par_Other.m_fixed},
      m_pExecStack{par_Other.m_pExecStack}
{
    par_Other.m_pExecStack = nullptr;
    par_Other.m_isFixedValue = true;
    par_Other.m_fixed = 0.0;
}

BaseValue &BaseValue::operator=(BaseValue &&par_Other) noexcept
{
    if (this != &par_Other)
    {
        Release();
        m_isFixedValue = par_Other.m_isFixedValue;
        m_fixed = par_Other.m_fixed;
        m_pExecStack = par_Other.m_pExecStack;
        par_Other.m_pExecStack = nullptr;
        par_Other.m_isFixedValue = true;
        par_Other.m_fixed = 0.0;
    }
    return *this;
}

void BaseValue::Release()
{
    if ((!m_isFixedValue) && (nullptr != m_pExecStack))
    {
        remove_exec_stack(m_pExecStack);
    }
    m_pExecStack = nullptr;
    m_isFixedValue = true;
}

int BaseValue::Parse(const std::string &par_Value, bool par_OneTimeCalc)
{
    Release();
    char *errString{nullptr};
    if (par_OneTimeCalc)
    {
        m_isFixedValue = true;
        if (0 != direct_solve_equation_err_string(par_Value.c_str(), &m_fixed, &errString))
        {
            FreeErrStringBuffer(&errString);
            m_fixed = 0.0;
            return -1;
        }
        return 0;
    }

    union BB_VARI bbValue;
    if (0 == string_to_bbvari(BB_DOUBLE, &bbValue, par_Value.c_str()))
    {
        m_fixed = bbValue.d;
        m_isFixedValue = true;
        return 0;
    }

    m_pExecStack = solve_equation_err_string(par_Value.c_str(), &errString);
    if (nullptr == m_pExecStack)
    {
        FreeErrStringBuffer(&errString);
        m_isFixedValue = true;
        m_fixed = 0.0;
        return -1;
    }
    m_isFixedValue = false;
    return 0;
}

void BaseValue::SetFixed(double par_Value)
{
    Release();
    m_isFixedValue = true;
    m_fixed = par_Value;
}

double BaseValue::Get() const
{
    if (m_isFixedValue)
    {
        return m_fixed;
    }
    return execute_stack(m_pExecStack);
}

// ---------------------------------------------------------------------------
// ColorValue
// ---------------------------------------------------------------------------

int ColorValue::Parse(const std::string &par_Value)
{
    std::vector<std::string> parts;
    std::size_t start{0u};
    while (true)
    {
        const std::size_t pos{par_Value.find(':', start)};
        if (std::string::npos == pos)
        {
            parts.push_back(par_Value.substr(start));
            break;
        }
        parts.push_back(par_Value.substr(start, pos - start));
        start = pos + 1u;
    }
    if (3u == parts.size())
    {
        std::string red{parts[0]};
        std::string green{parts[1]};
        std::string blue{parts[2]};
        if ((0 == m_red.Parse(red)) && (0 == m_green.Parse(green)) && (0 == m_blue.Parse(blue)))
        {
            return 0;
        }
    }
    return -1;
}

void ColorValue::SetFixed(double par_Red, double par_Green, double par_Blue)
{
    m_red.SetFixed(par_Red);
    m_green.SetFixed(par_Green);
    m_blue.SetFixed(par_Blue);
}

ImU32 ColorValue::GetColor(std::uint8_t par_Alpha) const
{
    const int red{ClampChannel(m_red.Get())};
    const int green{ClampChannel(m_green.Get())};
    const int blue{ClampChannel(m_blue.Get())};
    return IM_COL32(red, green, blue, par_Alpha);
}

// ---------------------------------------------------------------------------
// DrawParameter / TranslateParameter / Translate
// ---------------------------------------------------------------------------

DrawParameter::DrawParameter(double par_ParentX, double par_ParentY, double par_ParentScale,
                             double par_ParentRot, double par_XScale, double par_YScale)
    : m_parentX{par_ParentX}, m_parentY{par_ParentY}, m_parentScale{par_ParentScale},
      m_parentRot{par_ParentRot}, m_parentRotSin{std::sin(par_ParentRot)},
      m_parentRotCos{std::cos(par_ParentRot)}, m_xScale{par_XScale}, m_yScale{par_YScale}
{
}

TranslateParameter::TranslateParameter(double par_PosX, double par_PosY, double par_Scale,
                                       double par_Rot, const DrawParameter &par_Draw)
    : m_posX{par_PosX}, m_posY{par_PosY}, m_scale{par_Scale}, m_sin{std::sin(par_Rot)},
      m_cos{std::cos(par_Rot)}, m_draw{par_Draw}
{
}

void Translate(double *par_pX, double *par_pY, const TranslateParameter &par_Parameter,
               TranslateType par_Type)
{
    const std::uint8_t type{static_cast<std::uint8_t>(par_Type)};
    double x{*par_pX};
    double y{*par_pY};

    if (static_cast<std::uint8_t>(TranslateType::OnlyItself) ==
        (type & static_cast<std::uint8_t>(TranslateType::OnlyItself)))
    {
        const double xr{par_Parameter.m_cos * x + par_Parameter.m_sin * y};
        const double xs{xr * par_Parameter.m_scale + par_Parameter.m_posX};
        const double yr{par_Parameter.m_sin * -x + par_Parameter.m_cos * y};
        const double ys{yr * par_Parameter.m_scale + par_Parameter.m_posY};
        x = xs;
        y = ys;
    }
    if (static_cast<std::uint8_t>(TranslateType::OnlyParent) ==
        (type & static_cast<std::uint8_t>(TranslateType::OnlyParent)))
    {
        const double xr{par_Parameter.m_draw.m_parentRotCos * x +
                        par_Parameter.m_draw.m_parentRotSin * y};
        const double xs{xr * par_Parameter.m_draw.m_parentScale + par_Parameter.m_draw.m_parentX};
        const double yr{par_Parameter.m_draw.m_parentRotSin * -x +
                        par_Parameter.m_draw.m_parentRotCos * y};
        const double ys{yr * par_Parameter.m_draw.m_parentScale + par_Parameter.m_draw.m_parentY};
        x = xs;
        y = ys;
    }
    if (static_cast<std::uint8_t>(TranslateType::OnlyToScreen) ==
        (type & static_cast<std::uint8_t>(TranslateType::OnlyToScreen)))
    {
        x = x * par_Parameter.m_draw.m_xScale;
        y = par_Parameter.m_draw.m_yScale - y * par_Parameter.m_draw.m_yScale;
    }
    *par_pX = x;
    *par_pY = y;
}

// ---------------------------------------------------------------------------
// UserDrawElement
// ---------------------------------------------------------------------------

void UserDrawElement::ResetToDefault()
{
    m_visible.SetFixed(1.0);
    m_lineColor.SetFixed(0.0, 0.0, 0.0);
    m_lineThickness.SetFixed(1.0);
    m_fillColorIsSet = false;
    m_fillColor.SetFixed(0.0, 0.0, 0.0);
    m_x.SetFixed(0.0);
    m_y.SetFixed(0.0);
    m_scale.SetFixed(1.0);
    m_rot.SetFixed(0.0);
}

bool UserDrawElement::ParseOneBaseParameter(const std::string &par_Key, const std::string &par_Value)
{
    std::string value{par_Value};
    if (IEqualsAscii(par_Key, "x"))
    {
        m_x.Parse(value);
    }
    else if (IEqualsAscii(par_Key, "y"))
    {
        m_y.Parse(value);
    }
    else if (IEqualsAscii(par_Key, "scale") || IEqualsAscii(par_Key, "s"))
    {
        m_scale.Parse(value);
    }
    else if (IEqualsAscii(par_Key, "rot") || IEqualsAscii(par_Key, "r"))
    {
        m_rot.Parse(value);
    }
    else if (IEqualsAscii(par_Key, "visible") || IEqualsAscii(par_Key, "v"))
    {
        m_visible.Parse(value);
    }
    else if (IEqualsAscii(par_Key, "thickness") || IEqualsAscii(par_Key, "t"))
    {
        m_lineThickness.Parse(value);
    }
    else if (IEqualsAscii(par_Key, "color") || IEqualsAscii(par_Key, "c"))
    {
        m_lineColor.Parse(value);
    }
    else if (IEqualsAscii(par_Key, "fill") || IEqualsAscii(par_Key, "f"))
    {
        m_fillColor.Parse(value);
        m_fillColorIsSet = true;
    }
    else
    {
        return false;
    }
    return true;
}

ImU32 UserDrawElement::GetLineColor() const
{
    return m_lineColor.GetColor();
}

float UserDrawElement::GetLineThickness() const
{
    const double thickness{m_lineThickness.Get()};
    return (thickness < 1.0) ? 1.0f : static_cast<float>(thickness);
}

ImU32 UserDrawElement::GetFillColor() const
{
    return m_fillColor.GetColor();
}

void UserDrawElement::WriteToIni(const char *par_Section, const std::string &par_Entry,
                                 int par_FileDescriptor)
{
    std::string line{GetTypeString()};
    line.append(m_parameterLine);
    (void)IniFileDataBaseWriteString(par_Section, par_Entry.c_str(), line.c_str(), par_FileDescriptor);
    for (int index{0}; index < GetChildCount(); ++index)
    {
        UserDrawElement *child{GetChild(index)};
        if (nullptr != child)
        {
            std::string childEntry{par_Entry};
            childEntry.append("_");
            childEntry.append(std::to_string(index));
            child->WriteToIni(par_Section, childEntry, par_FileDescriptor);
        }
    }
}

} // namespace XilEnvImGui
