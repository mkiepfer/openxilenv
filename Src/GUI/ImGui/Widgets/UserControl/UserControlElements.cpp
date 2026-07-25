#include "Widgets/UserControl/UserControlElements.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <utility>

#include "imgui.h"

extern "C" {
#include "EquationParser.h"
#include "InterfaceToScript.h"
#include "ThrowError.h"
}

namespace XilEnvImGui
{

namespace
{

constexpr float GROUP_PADDING{4.0f};      /*!< Inner padding of a group box in pixels. */
constexpr float CHILD_GAP{2.0f};          /*!< Gap inset applied to every grid cell in pixels. */

/**
 * @brief Trims leading/trailing ASCII whitespace (mirrors QString::trimmed()).
 */
std::string Trim(const std::string &par_Value)
{
    std::size_t start{0u};
    while ((start < par_Value.size()) &&
           (0 != std::isspace(static_cast<unsigned char>(par_Value[start]))))
    {
        ++start;
    }
    std::size_t end{par_Value.size()};
    while ((end > start) && (0 != std::isspace(static_cast<unsigned char>(par_Value[end - 1u]))))
    {
        --end;
    }
    return par_Value.substr(start, end - start);
}

/**
 * @brief Case-insensitive prefix match returning the prefix length or 0 (Qt startsWith).
 */
int MatchKeyword(const std::string &par_Line, const char *par_Keyword)
{
    std::size_t index{0u};
    while ('\0' != par_Keyword[index])
    {
        if (index >= par_Line.size())
        {
            return 0;
        }
        char lineChar{par_Line[index]};
        char keyChar{par_Keyword[index]};
        if (('A' <= lineChar) && ('Z' >= lineChar))
        {
            lineChar = static_cast<char>(lineChar - 'A' + 'a');
        }
        if (('A' <= keyChar) && ('Z' >= keyChar))
        {
            keyChar = static_cast<char>(keyChar - 'A' + 'a');
        }
        if (lineChar != keyChar)
        {
            return 0;
        }
        ++index;
    }
    return static_cast<int>(index);
}

} // namespace

// ---------------------------------------------------------------------------
// UserControlContainer
// ---------------------------------------------------------------------------

void UserControlContainer::AddChild(std::unique_ptr<UserControlElement> par_Child)
{
    if (nullptr != par_Child)
    {
        m_children.push_back(std::move(par_Child));
    }
}

int UserControlContainer::GetChildCount() const
{
    return static_cast<int>(m_children.size());
}

UserControlElement *UserControlContainer::GetChild(int par_Index) const
{
    if ((0 <= par_Index) && (par_Index < static_cast<int>(m_children.size())))
    {
        return m_children[static_cast<std::size_t>(par_Index)].get();
    }
    return nullptr;
}

void UserControlContainer::RenderChildrenGrid(const ImVec2 &par_Origin, const ImVec2 &par_Size,
                                              bool par_ScriptRunning)
{
    if (m_children.empty() || (par_Size.x <= 0.0f) || (par_Size.y <= 0.0f))
    {
        return;
    }

    // Derive the uniform grid dimensions from the children's grid coordinates (QGridLayout maps
    // column = x, row = y, column span = width, row span = height).
    int columns{1};
    int rows{1};
    for (const std::unique_ptr<UserControlElement> &child : m_children)
    {
        columns = std::max(columns, child->GetX() + std::max(child->GetWidth(), 1));
        rows = std::max(rows, child->GetY() + std::max(child->GetHeight(), 1));
    }

    const float cellWidth{par_Size.x / static_cast<float>(columns)};
    const float cellHeight{par_Size.y / static_cast<float>(rows)};

    for (const std::unique_ptr<UserControlElement> &child : m_children)
    {
        const ImVec2 childOrigin{par_Origin.x + static_cast<float>(child->GetX()) * cellWidth +
                                     CHILD_GAP,
                                 par_Origin.y + static_cast<float>(child->GetY()) * cellHeight +
                                     CHILD_GAP};
        const ImVec2 childSize{
            std::max(static_cast<float>(std::max(child->GetWidth(), 1)) * cellWidth - 2.0f * CHILD_GAP,
                     1.0f),
            std::max(static_cast<float>(std::max(child->GetHeight(), 1)) * cellHeight -
                         2.0f * CHILD_GAP,
                     1.0f)};
        child->Render(childOrigin, childSize, par_ScriptRunning);
    }
}

// ---------------------------------------------------------------------------
// UserControlRoot
// ---------------------------------------------------------------------------

void UserControlRoot::Render(const ImVec2 &par_Origin, const ImVec2 &par_Size, bool par_ScriptRunning)
{
    RenderChildrenGrid(par_Origin, par_Size, par_ScriptRunning);
}

bool UserControlRoot::ParseOneParameter(const std::string &par_Value)
{
    (void)par_Value;
    return true;
}

int UserControlRoot::Is(const std::string &par_Line)
{
    return MatchKeyword(par_Line, "Root");
}

// ---------------------------------------------------------------------------
// UserControlGroup
// ---------------------------------------------------------------------------

void UserControlGroup::ResetToDefault()
{
    UserControlElement::ResetToDefault();
    m_text.clear();
}

void UserControlGroup::Render(const ImVec2 &par_Origin, const ImVec2 &par_Size, bool par_ScriptRunning)
{
    ImDrawList *drawList{ImGui::GetWindowDrawList()};
    const ImVec2 bottomRight{par_Origin.x + par_Size.x, par_Origin.y + par_Size.y};

    const ImU32 borderColor{ImGui::GetColorU32(ImGuiCol_Border)};
    const ImU32 textColor{ImGui::GetColorU32(ImGuiCol_Text)};
    drawList->AddRect(par_Origin, bottomRight, borderColor, 3.0f);

    const float titleHeight{ImGui::GetTextLineHeightWithSpacing()};
    if (!m_text.empty())
    {
        drawList->AddText(ImVec2(par_Origin.x + GROUP_PADDING, par_Origin.y + GROUP_PADDING),
                          textColor, m_text.c_str());
    }

    const ImVec2 innerOrigin{par_Origin.x + GROUP_PADDING, par_Origin.y + titleHeight};
    const ImVec2 innerSize{std::max(par_Size.x - 2.0f * GROUP_PADDING, 1.0f),
                           std::max(par_Size.y - titleHeight - GROUP_PADDING, 1.0f)};
    RenderChildrenGrid(innerOrigin, innerSize, par_ScriptRunning);
}

bool UserControlGroup::ParseOneParameter(const std::string &par_Value)
{
    // Qt splits on '=' and requires exactly two parts, so a value with more than one '=' is rejected.
    const std::size_t firstSep{par_Value.find('=')};
    if (std::string::npos == firstSep)
    {
        return false;
    }
    if (std::string::npos != par_Value.find('=', firstSep + 1u))
    {
        return false;
    }
    const std::string key{Trim(par_Value.substr(0u, firstSep))};
    const std::string value{Trim(par_Value.substr(firstSep + 1u))};
    if (UserControlIEqualsAscii(key, "text"))
    {
        m_text = value;
        return true;
    }
    return ParseOneBaseParameter(key, value);
}

int UserControlGroup::Is(const std::string &par_Line)
{
    return MatchKeyword(par_Line, "Group");
}

// ---------------------------------------------------------------------------
// UserControlButton
// ---------------------------------------------------------------------------

void UserControlButton::ResetToDefault()
{
    UserControlElement::ResetToDefault();
    m_text.clear();
    m_script.clear();
    m_equation.clear();
}

void UserControlButton::Render(const ImVec2 &par_Origin, const ImVec2 &par_Size, bool par_ScriptRunning)
{
    ImGui::SetCursorScreenPos(par_Origin);
    ImGui::PushID(this);
    if (par_ScriptRunning)
    {
        ImGui::BeginDisabled();
    }
    const char *label{m_text.empty() ? " " : m_text.c_str()};
    if (ImGui::Button(label, par_Size))
    {
        OnClick();
    }
    if (par_ScriptRunning)
    {
        ImGui::EndDisabled();
    }
    ImGui::PopID();
}

bool UserControlButton::ParseOneParameter(const std::string &par_Value)
{
    // Split on the first '=' only; the right-hand side (script/equation) may contain '='.
    const std::size_t separatorPos{par_Value.find('=')};
    if ((std::string::npos == separatorPos) || (0u == separatorPos))
    {
        return false;
    }
    const std::string key{Trim(par_Value.substr(0u, separatorPos))};
    const std::string value{Trim(par_Value.substr(separatorPos + 1u))};
    if (UserControlIEqualsAscii(key, "text"))
    {
        m_text = value;
        return true;
    }
    if (UserControlIEqualsAscii(key, "script"))
    {
        m_script = value;
        return true;
    }
    if (UserControlIEqualsAscii(key, "equation"))
    {
        m_equation = value;
        return true;
    }
    return ParseOneBaseParameter(key, value);
}

int UserControlButton::Is(const std::string &par_Line)
{
    return MatchKeyword(par_Line, "Button");
}

void UserControlButton::OnClick()
{
    if (!m_script.empty())
    {
        if (0 != StartScript(m_script.c_str()))
        {
            ThrowError(1, "cannot start script \"%s\". an other script is running", m_script.c_str());
        }
    }
    if (!m_equation.empty())
    {
        // Exact core call the Qt ButtonPressed() slot performs: evaluate the equation, which writes
        // the assigned blackboard variable(s).
        (void)direct_solve_equation(m_equation.c_str());
    }
}

} // namespace XilEnvImGui
