#include "Widgets/UserDraw/UserDrawElements.h"

#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

extern "C" {
#include "EnvironmentVariables.h"
}

namespace XilEnvImGui
{

namespace
{

/**
 * @brief Trims leading/trailing ASCII whitespace.
 */
std::string Trim(const std::string &par_Value)
{
    const std::size_t first{par_Value.find_first_not_of(" \t\r\n")};
    if (std::string::npos == first)
    {
        return std::string{};
    }
    const std::size_t last{par_Value.find_last_not_of(" \t\r\n")};
    return par_Value.substr(first, (last - first) + 1u);
}

/**
 * @brief Splits a "key=value" token (Qt QString::split('=') semantics: exactly two parts).
 * @return true if exactly one '=' produced a key and a value.
 */
bool SplitKeyValue(const std::string &par_Token, std::string &out_Key, std::string &out_Value)
{
    const std::size_t first{par_Token.find('=')};
    if (std::string::npos == first)
    {
        return false;
    }
    if (std::string::npos != par_Token.find('=', first + 1u))
    {
        return false; // more than one '=' -> Qt split size != 2
    }
    out_Key = Trim(par_Token.substr(0u, first));
    out_Value = Trim(par_Token.substr(first + 1u));
    return true;
}

/**
 * @brief Parses one "(x,y)" point token into two BaseValues appended to the list.
 */
void AppendPoint(const std::string &par_Point, std::vector<BaseValue> &out_Xy)
{
    int braceCounter{0};
    std::size_t start{0u};
    for (std::size_t index{0u}; index < par_Point.size(); ++index)
    {
        const char character{par_Point[index]};
        if ('(' == character)
        {
            ++braceCounter;
            if (1 == braceCounter)
            {
                start = index;
            }
        }
        else if (')' == character)
        {
            if (1 == braceCounter)
            {
                BaseValue value;
                value.Parse(par_Point.substr(start + 1u, index - start - 1u));
                out_Xy.push_back(std::move(value));
                start = index;
            }
            --braceCounter;
        }
        else if (',' == character)
        {
            if (1 == braceCounter)
            {
                BaseValue value;
                value.Parse(par_Point.substr(start + 1u, index - start - 1u));
                out_Xy.push_back(std::move(value));
                start = index;
            }
        }
    }
}

/**
 * @brief Parses a "((x,y),(x,y),...)" point-list value into a flat coordinate vector.
 */
void ParsePoints(const std::string &par_Value, std::vector<BaseValue> &out_Xy)
{
    int braceCounter{0};
    std::size_t start{0u};
    for (std::size_t index{0u}; index < par_Value.size(); ++index)
    {
        const char character{par_Value[index]};
        if ('(' == character)
        {
            ++braceCounter;
            if (1 == braceCounter)
            {
                start = index;
            }
        }
        else if (')' == character)
        {
            if (1 == braceCounter)
            {
                AppendPoint(par_Value.substr(start + 1u, index - start - 1u), out_Xy);
                start = index;
            }
            --braceCounter;
        }
        else if (',' == character)
        {
            if (1 == braceCounter)
            {
                AppendPoint(par_Value.substr(start + 1u, index - start - 1u), out_Xy);
                start = index;
            }
        }
    }
}

/**
 * @brief Case-insensitive check whether par_Line starts with par_Prefix.
 * @return Prefix length on match, else 0.
 */
int MatchPrefix(const std::string &par_Line, const char *par_Prefix)
{
    std::size_t index{0u};
    for (; '\0' != par_Prefix[index]; ++index)
    {
        if (index >= par_Line.size())
        {
            return 0;
        }
        char lineChar{par_Line[index]};
        char prefixChar{par_Prefix[index]};
        if (('A' <= lineChar) && ('Z' >= lineChar))
        {
            lineChar = static_cast<char>(lineChar - 'A' + 'a');
        }
        if (('A' <= prefixChar) && ('Z' >= prefixChar))
        {
            prefixChar = static_cast<char>(prefixChar - 'A' + 'a');
        }
        if (lineChar != prefixChar)
        {
            return 0;
        }
    }
    return static_cast<int>(index);
}

} // namespace

// ---------------------------------------------------------------------------
// UserDrawContainer
// ---------------------------------------------------------------------------

void UserDrawContainer::AddChild(std::unique_ptr<UserDrawElement> par_Child)
{
    if (nullptr != par_Child)
    {
        m_children.push_back(std::move(par_Child));
    }
}

int UserDrawContainer::GetChildCount() const
{
    return static_cast<int>(m_children.size());
}

UserDrawElement *UserDrawContainer::GetChild(int par_Index) const
{
    if ((0 <= par_Index) && (par_Index < static_cast<int>(m_children.size())))
    {
        return m_children[static_cast<std::size_t>(par_Index)].get();
    }
    return nullptr;
}

void UserDrawContainer::RenderChildren(ImDrawList *par_pDrawList, const DrawParameter &par_Draw,
                                       const ImVec2 &par_Origin)
{
    for (const std::unique_ptr<UserDrawElement> &child : m_children)
    {
        child->Render(par_pDrawList, par_Draw, par_Origin);
    }
}

// ---------------------------------------------------------------------------
// UserDrawRoot
// ---------------------------------------------------------------------------

void UserDrawRoot::Render(ImDrawList *par_pDrawList, const DrawParameter &par_Draw,
                          const ImVec2 &par_Origin)
{
    DrawParameter draw{par_Draw};
    if (m_fixedRatio)
    {
        if (draw.m_xScale < draw.m_yScale)
        {
            draw.m_yScale = draw.m_xScale;
        }
        else if (draw.m_yScale < draw.m_xScale)
        {
            draw.m_xScale = draw.m_yScale;
        }
    }
    RenderChildren(par_pDrawList, draw, par_Origin);
}

bool UserDrawRoot::ParseOneParameter(const std::string &par_Value)
{
    (void)par_Value;
    return true;
}

int UserDrawRoot::Is(const std::string &par_Line)
{
    return (0 != MatchPrefix(par_Line, "Root")) ? 4 : 0;
}

// ---------------------------------------------------------------------------
// UserDrawLayer
// ---------------------------------------------------------------------------

void UserDrawLayer::Render(ImDrawList *par_pDrawList, const DrawParameter &par_Draw,
                           const ImVec2 &par_Origin)
{
    const double rot{m_rot.Get()};
    const double scale{m_scale.Get()};
    TranslateParameter parameter{m_x.Get(), m_y.Get(), scale, rot, par_Draw};
    double x{0.0};
    double y{0.0};
    Translate(&x, &y, parameter, TranslateType::ParentAndItself);
    DrawParameter draw{x, y, scale, rot + par_Draw.m_parentRot, par_Draw.m_xScale, par_Draw.m_yScale};
    RenderChildren(par_pDrawList, draw, par_Origin);
}

bool UserDrawLayer::ParseOneParameter(const std::string &par_Value)
{
    std::string key;
    std::string value;
    if (!SplitKeyValue(par_Value, key, value))
    {
        return false;
    }
    if (IEqualsAscii(key, "update") || IEqualsAscii(key, "transparent"))
    {
        return true; // metadata: not reproduced semantically, round-trips via parameter line
    }
    return ParseOneBaseParameter(key, value);
}

int UserDrawLayer::Is(const std::string &par_Line)
{
    return (0 != MatchPrefix(par_Line, "Layer")) ? 5 : 0;
}

// ---------------------------------------------------------------------------
// UserDrawGroup
// ---------------------------------------------------------------------------

void UserDrawGroup::Render(ImDrawList *par_pDrawList, const DrawParameter &par_Draw,
                           const ImVec2 &par_Origin)
{
    if (m_visible.Get() >= 0.5)
    {
        const double rot{m_rot.Get()};
        const double scale{m_scale.Get()};
        TranslateParameter parameter{m_x.Get(), m_y.Get(), scale, rot, par_Draw};
        double x{0.0};
        double y{0.0};
        Translate(&x, &y, parameter, TranslateType::ParentAndItself);
        DrawParameter draw{x, y, scale, rot + par_Draw.m_parentRot, par_Draw.m_xScale, par_Draw.m_yScale};
        RenderChildren(par_pDrawList, draw, par_Origin);
    }
}

bool UserDrawGroup::ParseOneParameter(const std::string &par_Value)
{
    std::string key;
    std::string value;
    if (!SplitKeyValue(par_Value, key, value))
    {
        return false;
    }
    return ParseOneBaseParameter(key, value);
}

int UserDrawGroup::Is(const std::string &par_Line)
{
    return (0 != MatchPrefix(par_Line, "Group")) ? 5 : 0;
}

// ---------------------------------------------------------------------------
// UserDrawLine
// ---------------------------------------------------------------------------

void UserDrawLine::ResetToDefault()
{
    UserDrawElement::ResetToDefault();
    m_width.SetFixed(1.0);
    m_xy.clear();
}

void UserDrawLine::ParsePointList(const std::string &par_Value)
{
    m_xy.clear();
    ParsePoints(par_Value, m_xy);
}

bool UserDrawLine::ParseOneParameter(const std::string &par_Value)
{
    std::string key;
    std::string value;
    if (!SplitKeyValue(par_Value, key, value))
    {
        return false;
    }
    if (IEqualsAscii(key, "width") || IEqualsAscii(key, "w"))
    {
        m_width.Parse(value);
    }
    else if (IEqualsAscii(key, "points") || IEqualsAscii(key, "p"))
    {
        ParsePointList(value);
    }
    else
    {
        return ParseOneBaseParameter(key, value);
    }
    return true;
}

void UserDrawLine::Render(ImDrawList *par_pDrawList, const DrawParameter &par_Draw,
                          const ImVec2 &par_Origin)
{
    if (m_visible.Get() >= 0.5)
    {
        const std::size_t count{m_xy.size() / 2u};
        if (2u > count)
        {
            return;
        }
        TranslateParameter parameter{m_x.Get(), m_y.Get(), m_scale.Get(), m_rot.Get(), par_Draw};
        std::vector<ImVec2> points;
        points.reserve(count);
        for (std::size_t i{0u}; i < count; ++i)
        {
            double x{m_xy[i * 2u].Get()};
            double y{m_xy[(i * 2u) + 1u].Get()};
            Translate(&x, &y, parameter);
            points.push_back(ImVec2(par_Origin.x + static_cast<float>(x), par_Origin.y + static_cast<float>(y)));
        }
        par_pDrawList->AddPolyline(points.data(), static_cast<int>(points.size()), GetLineColor(),
                                   ImDrawFlags_None, GetLineThickness());
    }
}

int UserDrawLine::Is(const std::string &par_Line)
{
    return (0 != MatchPrefix(par_Line, "Line")) ? 4 : 0;
}

// ---------------------------------------------------------------------------
// UserDrawPolygon
// ---------------------------------------------------------------------------

void UserDrawPolygon::ResetToDefault()
{
    UserDrawElement::ResetToDefault();
    m_width.SetFixed(1.0);
    m_xy.clear();
}

void UserDrawPolygon::ParsePointList(const std::string &par_Value)
{
    m_xy.clear();
    ParsePoints(par_Value, m_xy);
}

bool UserDrawPolygon::ParseOneParameter(const std::string &par_Value)
{
    std::string key;
    std::string value;
    if (!SplitKeyValue(par_Value, key, value))
    {
        return false;
    }
    if (IEqualsAscii(key, "width") || IEqualsAscii(key, "w"))
    {
        m_width.Parse(value);
    }
    else if (IEqualsAscii(key, "points") || IEqualsAscii(key, "p"))
    {
        ParsePointList(value);
    }
    else
    {
        return ParseOneBaseParameter(key, value);
    }
    return true;
}

void UserDrawPolygon::Render(ImDrawList *par_pDrawList, const DrawParameter &par_Draw,
                             const ImVec2 &par_Origin)
{
    if (m_visible.Get() >= 0.5)
    {
        const std::size_t count{m_xy.size() / 2u};
        if (3u > count)
        {
            return;
        }
        TranslateParameter parameter{m_x.Get(), m_y.Get(), m_scale.Get(), m_rot.Get(), par_Draw};
        std::vector<ImVec2> points;
        points.reserve(count);
        for (std::size_t i{0u}; i < count; ++i)
        {
            double x{m_xy[i * 2u].Get()};
            double y{m_xy[(i * 2u) + 1u].Get()};
            Translate(&x, &y, parameter);
            points.push_back(ImVec2(par_Origin.x + static_cast<float>(x), par_Origin.y + static_cast<float>(y)));
        }
        if (m_fillColorIsSet)
        {
            par_pDrawList->AddConvexPolyFilled(points.data(), static_cast<int>(points.size()),
                                               GetFillColor());
        }
        par_pDrawList->AddPolyline(points.data(), static_cast<int>(points.size()), GetLineColor(),
                                   ImDrawFlags_Closed, GetLineThickness());
    }
}

int UserDrawPolygon::Is(const std::string &par_Line)
{
    return (0 != MatchPrefix(par_Line, "Polygon")) ? 7 : 0;
}

// ---------------------------------------------------------------------------
// UserDrawRect
// ---------------------------------------------------------------------------

void UserDrawRect::ResetToDefault()
{
    UserDrawElement::ResetToDefault();
    m_rectWidth.SetFixed(0.5);
    m_rectHeight.SetFixed(0.5);
}

bool UserDrawRect::ParseOneParameter(const std::string &par_Value)
{
    std::string key;
    std::string value;
    if (!SplitKeyValue(par_Value, key, value))
    {
        return false;
    }
    if (IEqualsAscii(key, "width") || IEqualsAscii(key, "w"))
    {
        m_rectWidth.Parse(value);
    }
    else if (IEqualsAscii(key, "height") || IEqualsAscii(key, "h"))
    {
        m_rectHeight.Parse(value);
    }
    else
    {
        return ParseOneBaseParameter(key, value);
    }
    return true;
}

void UserDrawRect::Render(ImDrawList *par_pDrawList, const DrawParameter &par_Draw,
                          const ImVec2 &par_Origin)
{
    if (m_visible.Get() >= 0.5)
    {
        TranslateParameter parameter{m_x.Get(), m_y.Get(), m_scale.Get(), m_rot.Get(), par_Draw};
        const double width{m_rectWidth.Get()};
        const double height{m_rectHeight.Get()};
        const double localX[4]{0.0, width, width, 0.0};
        const double localY[4]{0.0, 0.0, height, height};
        ImVec2 points[4];
        for (int i{0}; i < 4; ++i)
        {
            double x{localX[i]};
            double y{localY[i]};
            Translate(&x, &y, parameter);
            points[i] = ImVec2(par_Origin.x + static_cast<float>(x), par_Origin.y + static_cast<float>(y));
        }
        if (m_fillColorIsSet)
        {
            par_pDrawList->AddConvexPolyFilled(points, 4, GetFillColor());
        }
        par_pDrawList->AddPolyline(points, 4, GetLineColor(), ImDrawFlags_Closed, GetLineThickness());
    }
}

int UserDrawRect::Is(const std::string &par_Line)
{
    return (0 != MatchPrefix(par_Line, "Rect")) ? 4 : 0;
}

// ---------------------------------------------------------------------------
// UserDrawCircle
// ---------------------------------------------------------------------------

void UserDrawCircle::ResetToDefault()
{
    UserDrawElement::ResetToDefault();
    m_radius.SetFixed(0.5);
}

bool UserDrawCircle::ParseOneParameter(const std::string &par_Value)
{
    std::string key;
    std::string value;
    if (!SplitKeyValue(par_Value, key, value))
    {
        return false;
    }
    if (IEqualsAscii(key, "radius") || IEqualsAscii(key, "r"))
    {
        m_radius.Parse(value);
    }
    else if (IEqualsAscii(key, "RotPoint"))
    {
        return true; // rotation-point metadata, round-trips via parameter line
    }
    else
    {
        return ParseOneBaseParameter(key, value);
    }
    return true;
}

void UserDrawCircle::Render(ImDrawList *par_pDrawList, const DrawParameter &par_Draw,
                            const ImVec2 &par_Origin)
{
    if (m_visible.Get() >= 0.5)
    {
        const double scale{m_scale.Get()};
        const double rot{m_rot.Get()};
        const double radius{m_radius.Get()};
        TranslateParameter parameter{m_x.Get(), m_y.Get(), scale, rot, par_Draw};

        double x1{-radius};
        double y1{-radius};
        Translate(&x1, &y1, parameter, TranslateType::AllToScreen);
        double x2{radius};
        double y2{radius};
        Translate(&x2, &y2, parameter, TranslateType::AllToScreen);

        if (x1 > x2)
        {
            std::swap(x1, x2);
        }
        if (y1 > y2)
        {
            std::swap(y1, y2);
        }
        const ImVec2 center{par_Origin.x + static_cast<float>((x1 + x2) * 0.5),
                            par_Origin.y + static_cast<float>((y1 + y2) * 0.5)};
        const ImVec2 radii{static_cast<float>((x2 - x1) * 0.5), static_cast<float>((y2 - y1) * 0.5)};
        if (m_fillColorIsSet)
        {
            par_pDrawList->AddEllipseFilled(center, radii, GetFillColor());
        }
        par_pDrawList->AddEllipse(center, radii, GetLineColor(), 0.0f, 0, GetLineThickness());
    }
}

int UserDrawCircle::Is(const std::string &par_Line)
{
    return (0 != MatchPrefix(par_Line, "Circle")) ? 6 : 0;
}

// ---------------------------------------------------------------------------
// UserDrawPoint
// ---------------------------------------------------------------------------

bool UserDrawPoint::ParseOneParameter(const std::string &par_Value)
{
    std::string key;
    std::string value;
    if (!SplitKeyValue(par_Value, key, value))
    {
        return false;
    }
    return ParseOneBaseParameter(key, value);
}

void UserDrawPoint::Render(ImDrawList *par_pDrawList, const DrawParameter &par_Draw,
                           const ImVec2 &par_Origin)
{
    if (m_visible.Get() >= 0.5)
    {
        const double scale{m_scale.Get()};
        TranslateParameter parameter{m_x.Get(), m_y.Get(), scale, 0.0, par_Draw};
        double x{0.0};
        double y{0.0};
        Translate(&x, &y, parameter, TranslateType::AllToScreen);
        const ImVec2 point{par_Origin.x + static_cast<float>(x), par_Origin.y + static_cast<float>(y)};
        const float radius{(GetLineThickness() * 0.5f < 1.0f) ? 1.0f : GetLineThickness() * 0.5f};
        par_pDrawList->AddCircleFilled(point, radius, GetLineColor());
    }
}

int UserDrawPoint::Is(const std::string &par_Line)
{
    return (0 != MatchPrefix(par_Line, "Point")) ? 5 : 0;
}

// ---------------------------------------------------------------------------
// UserDrawText
// ---------------------------------------------------------------------------

void UserDrawText::ResetToDefault()
{
    UserDrawElement::ResetToDefault();
    m_text.clear();
}

bool UserDrawText::ParseOneParameter(const std::string &par_Value)
{
    std::string key;
    std::string value;
    if (!SplitKeyValue(par_Value, key, value))
    {
        return false;
    }
    if (IEqualsAscii(key, "text"))
    {
        m_text = value;
    }
    else if (IEqualsAscii(key, "RotPoint"))
    {
        return true; // rotation-point metadata, round-trips via parameter line
    }
    else
    {
        return ParseOneBaseParameter(key, value);
    }
    return true;
}

void UserDrawText::Render(ImDrawList *par_pDrawList, const DrawParameter &par_Draw,
                          const ImVec2 &par_Origin)
{
    if (m_visible.Get() >= 0.5)
    {
        char buffer[1024]{};
        const char *text{buffer};
        if (0 >= SearchAndReplaceEnvironmentStrings(m_text.c_str(), buffer, static_cast<int>(sizeof(buffer))))
        {
            text = "cannot evaluate string";
        }

        const double scale{m_scale.Get()};
        TranslateParameter parameter{m_x.Get(), m_y.Get(), scale, m_rot.Get(), par_Draw};
        double x{0.0};
        double y{0.0};
        Translate(&x, &y, parameter, TranslateType::AllToScreen);

        float fontSize{static_cast<float>(par_Draw.m_yScale * scale * par_Draw.m_parentScale)};
        if (fontSize < 1.0f)
        {
            fontSize = 1.0f;
        }
        ImFont *font{ImGui::GetFont()};
        const ImVec2 textSize{font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text)};
        const ImVec2 pos{par_Origin.x + static_cast<float>(x) - textSize.x * 0.5f,
                         par_Origin.y + static_cast<float>(y) - textSize.y * 0.5f};
        par_pDrawList->AddText(font, fontSize, pos, GetLineColor(), text);
    }
}

int UserDrawText::Is(const std::string &par_Line)
{
    return (0 != MatchPrefix(par_Line, "Text")) ? 4 : 0;
}

} // namespace XilEnvImGui
