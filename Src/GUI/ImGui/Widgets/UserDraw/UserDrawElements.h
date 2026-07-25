/**
 * @file Src/GUI/ImGui/Widgets/UserDraw/UserDrawElements.h
 * @brief Concrete UserDraw scene-graph elements (Root/Layer/Group + Line/Rect/Circle/
 *        Polygon/Point/Text) for the ImGui UserDraw port.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_WIDGETS_USERDRAW_USERDRAWELEMENTS_H
#define SRC_GUI_IMGUI_WIDGETS_USERDRAW_USERDRAWELEMENTS_H

#include <memory>
#include <string>
#include <vector>

#include "imgui.h"

#include "Widgets/UserDraw/UserDrawElement.h"

namespace XilEnvImGui
{

/**
 * @class UserDrawContainer
 * @brief Shared base for the group-like elements (Root, Layer, Group).
 * @details Owns an ordered list of child elements and forwards config/tree queries.
 */
class UserDrawContainer : public UserDrawElement
{
public:
    [[nodiscard]] bool IsGroup() const override
    {
        return true;
    }
    void AddChild(std::unique_ptr<UserDrawElement> par_Child) override;
    [[nodiscard]] int GetChildCount() const override;
    [[nodiscard]] UserDrawElement *GetChild(int par_Index) const override;

protected:
    /**
     * @brief Renders all children with the given accumulated transform.
     */
    void RenderChildren(ImDrawList *par_pDrawList, const DrawParameter &par_Draw,
                        const ImVec2 &par_Origin);

    std::vector<std::unique_ptr<UserDrawElement>> m_children{}; /*!< Owned child elements. */
};

/**
 * @class UserDrawRoot
 * @brief Invisible top-level container; applies fixed-ratio scaling to its children.
 */
class UserDrawRoot : public UserDrawContainer
{
public:
    void Render(ImDrawList *par_pDrawList, const DrawParameter &par_Draw,
                const ImVec2 &par_Origin) override;
    bool ParseOneParameter(const std::string &par_Value) override;
    [[nodiscard]] const char *GetTypeString() const override
    {
        return "Root";
    }
    /**
     * @brief Detects a "Root" line prefix.
     * @param[in] par_Line Config line.
     * @return Length of the matched keyword or 0.
     */
    [[nodiscard]] static int Is(const std::string &par_Line);

    bool m_fixedRatio{false};   /*!< Keep a 1:1 aspect ratio if set. */
    bool m_antialiasing{false}; /*!< Antialiasing hint (ImGui is always antialiased). */
};

/**
 * @class UserDrawLayer
 * @brief Container with update-rate/transparency metadata (rendered inline in the ImGui port).
 */
class UserDrawLayer : public UserDrawContainer
{
public:
    void Render(ImDrawList *par_pDrawList, const DrawParameter &par_Draw,
                const ImVec2 &par_Origin) override;
    bool ParseOneParameter(const std::string &par_Value) override;
    [[nodiscard]] const char *GetTypeString() const override
    {
        return "Layer";
    }
    [[nodiscard]] static int Is(const std::string &par_Line);
};

/**
 * @class UserDrawGroup
 * @brief Container applying its own position/scale/rotation to the child subtree.
 */
class UserDrawGroup : public UserDrawContainer
{
public:
    void Render(ImDrawList *par_pDrawList, const DrawParameter &par_Draw,
                const ImVec2 &par_Origin) override;
    bool ParseOneParameter(const std::string &par_Value) override;
    [[nodiscard]] const char *GetTypeString() const override
    {
        return "Group";
    }
    [[nodiscard]] static int Is(const std::string &par_Line);
};

/**
 * @class UserDrawLine
 * @brief Polyline through a list of equation-backed points.
 */
class UserDrawLine : public UserDrawElement
{
public:
    void ResetToDefault() override;
    void Render(ImDrawList *par_pDrawList, const DrawParameter &par_Draw,
                const ImVec2 &par_Origin) override;
    bool ParseOneParameter(const std::string &par_Value) override;
    [[nodiscard]] const char *GetTypeString() const override
    {
        return "Line";
    }
    [[nodiscard]] static int Is(const std::string &par_Line);

private:
    void ParsePointList(const std::string &par_Value);

    BaseValue m_width{};            /*!< Parsed for round-trip (Qt uses thickness to draw). */
    std::vector<BaseValue> m_xy{};  /*!< Flat x0,y0,x1,y1,... point coordinates. */
};

/**
 * @class UserDrawPolygon
 * @brief Closed, optionally filled polygon through equation-backed points.
 */
class UserDrawPolygon : public UserDrawElement
{
public:
    void ResetToDefault() override;
    void Render(ImDrawList *par_pDrawList, const DrawParameter &par_Draw,
                const ImVec2 &par_Origin) override;
    bool ParseOneParameter(const std::string &par_Value) override;
    [[nodiscard]] const char *GetTypeString() const override
    {
        return "Polygon";
    }
    [[nodiscard]] static int Is(const std::string &par_Line);

private:
    void ParsePointList(const std::string &par_Value);

    BaseValue m_width{};           /*!< Parsed for round-trip (Qt uses thickness to draw). */
    std::vector<BaseValue> m_xy{}; /*!< Flat x0,y0,x1,y1,... point coordinates. */
};

/**
 * @class UserDrawRect
 * @brief Axis-aligned (in local frame) rectangle, optionally filled.
 */
class UserDrawRect : public UserDrawElement
{
public:
    void ResetToDefault() override;
    void Render(ImDrawList *par_pDrawList, const DrawParameter &par_Draw,
                const ImVec2 &par_Origin) override;
    bool ParseOneParameter(const std::string &par_Value) override;
    [[nodiscard]] const char *GetTypeString() const override
    {
        return "Rect";
    }
    [[nodiscard]] static int Is(const std::string &par_Line);

private:
    BaseValue m_rectWidth{};  /*!< Rectangle width (logical). */
    BaseValue m_rectHeight{}; /*!< Rectangle height (logical). */
};

/**
 * @class UserDrawCircle
 * @brief Circle/ellipse with equation-backed radius, optionally filled.
 */
class UserDrawCircle : public UserDrawElement
{
public:
    void ResetToDefault() override;
    void Render(ImDrawList *par_pDrawList, const DrawParameter &par_Draw,
                const ImVec2 &par_Origin) override;
    bool ParseOneParameter(const std::string &par_Value) override;
    [[nodiscard]] const char *GetTypeString() const override
    {
        return "Circle";
    }
    [[nodiscard]] static int Is(const std::string &par_Line);

private:
    BaseValue m_radius{}; /*!< Circle radius (logical). */
};

/**
 * @class UserDrawPoint
 * @brief A single point drawn at the element position.
 */
class UserDrawPoint : public UserDrawElement
{
public:
    void Render(ImDrawList *par_pDrawList, const DrawParameter &par_Draw,
                const ImVec2 &par_Origin) override;
    bool ParseOneParameter(const std::string &par_Value) override;
    [[nodiscard]] const char *GetTypeString() const override
    {
        return "Point";
    }
    [[nodiscard]] static int Is(const std::string &par_Line);
};

/**
 * @class UserDrawText
 * @brief Text (with environment-variable substitution) centered on the element position.
 * @note Text rotation is not reproduced; text is drawn axis-aligned (see port notes).
 */
class UserDrawText : public UserDrawElement
{
public:
    void ResetToDefault() override;
    void Render(ImDrawList *par_pDrawList, const DrawParameter &par_Draw,
                const ImVec2 &par_Origin) override;
    bool ParseOneParameter(const std::string &par_Value) override;
    [[nodiscard]] const char *GetTypeString() const override
    {
        return "Text";
    }
    [[nodiscard]] static int Is(const std::string &par_Line);

private:
    std::string m_text{}; /*!< Raw text (may contain %ENV% references). */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_WIDGETS_USERDRAW_USERDRAWELEMENTS_H
