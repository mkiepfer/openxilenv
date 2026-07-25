/**
 * @file Src/GUI/ImGui/Widgets/A2LCalMap/A2LCalMap3DRenderer.h
 * @brief Software 3D surface renderer for the A2L MAP/CURVE editor, drawn via ImDrawList.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_WIDGETS_A2LCALMAP_A2LCALMAP3DRENDERER_H
#define SRC_GUI_IMGUI_WIDGETS_A2LCALMAP_A2LCALMAP3DRENDERER_H

#include "imgui.h"

#include "Widgets/A2LCalMap/A2LCalMapData.h"

namespace XilEnvImGui
{

/**
 * @class A2LCalMap3DRenderer
 * @brief Renders a MAP as a filled/wireframe surface (or a CURVE as a polyline) into a rect.
 * @details Immediate-mode port of the Qt A2LCalMap3DView software painter. Keeps the same
 *          rotation/scale/offset/perspective transform (TRANS3D) and the same painter's-order
 *          corner sorting; only the QPainter primitives are retargeted onto ImDrawList
 *          (AddTriangleFilled / AddLine / AddText). Height is the normalised map value, the
 *          base plane is spanned by the two axes. Mouse-drag rotates, the wheel zooms and a
 *          plain left-click picks the nearest grid point.
 */
class A2LCalMap3DRenderer
{
public:
    A2LCalMap3DRenderer() = default;

    void InitTrans3D();
    void InitTrans2D();
    void UpdateSinCos();

    /**
     * @brief Draws the surface/curve, axes, picks and operating-point marker into a rect.
     * @param[in] par_pDrawList Target draw list. Must not be nullptr.
     * @param[in] par_Origin Top-left corner of the drawing rect in screen space.
     * @param[in] par_Size Size of the drawing rect in pixels.
     * @param[in,out] par_Data Data model queried for values, picks and the operating point.
     */
    void Render(ImDrawList *par_pDrawList, const ImVec2 &par_Origin, const ImVec2 &par_Size,
                A2LCalMapData &par_Data);

    /**
     * @brief Picks the grid point nearest a click position (mirrors Qt PickingMap).
     * @param[in] par_HalfWidth Half of the drawing-rect width (projection centre).
     * @param[in] par_HalfHeight Half of the drawing-rect height (projection centre).
     * @param[in] par_XPos Click X relative to the rect origin.
     * @param[in] par_YPos Click Y relative to the rect origin.
     * @param[in] par_Add When true toggle a single pick, else replace the whole selection.
     * @param[in,out] par_Data Data model whose pick map is updated.
     * @return True when the selection changed.
     */
    bool PickingMap(int par_HalfWidth, int par_HalfHeight, int par_XPos, int par_YPos, bool par_Add,
                    A2LCalMapData &par_Data);

    void RotateXAngle(double par_Angle)
    {
        m_trans.ax += par_Angle;
    }
    void RotateZAngle(double par_Angle)
    {
        m_trans.az += par_Angle;
    }
    void ScaleFactor(double par_Factor)
    {
        m_trans.scale *= par_Factor;
    }

    [[nodiscard]] double GetXAngle() const
    {
        return m_trans.ax;
    }
    [[nodiscard]] double GetYAngle() const
    {
        return m_trans.ay;
    }
    [[nodiscard]] double GetZAngle() const
    {
        return m_trans.az;
    }
    [[nodiscard]] double GetXOffset() const
    {
        return m_trans.xOff;
    }
    [[nodiscard]] double GetYOffset() const
    {
        return m_trans.yOff;
    }
    [[nodiscard]] double GetZOffset() const
    {
        return m_trans.zOff;
    }
    [[nodiscard]] double GetDepth() const
    {
        return m_trans.kDepth;
    }
    [[nodiscard]] double GetScale() const
    {
        return m_trans.scale;
    }

    void SetXAngle(double par_Angle)
    {
        m_trans.ax = par_Angle;
    }
    void SetYAngle(double par_Angle)
    {
        m_trans.ay = par_Angle;
    }
    void SetZAngle(double par_Angle)
    {
        m_trans.az = par_Angle;
    }
    void SetXOffset(double par_Offset)
    {
        m_trans.xOff = par_Offset;
    }
    void SetYOffset(double par_Offset)
    {
        m_trans.yOff = par_Offset;
    }
    void SetZOffset(double par_Offset)
    {
        m_trans.zOff = par_Offset;
    }
    void SetDepth(double par_Depth)
    {
        m_trans.kDepth = par_Depth;
    }
    void SetScale(double par_Scale)
    {
        m_trans.scale = par_Scale;
    }

private:
    /**
     * @struct Trans3D
     * @brief View transform: Euler angles (+ cached sin/cos), perspective depth, scale, offset.
     */
    struct Trans3D
    {
        double ax{0.0};      /*!< X rotation angle. */
        double sinAx{0.0};   /*!< Cached sin(ax). */
        double cosAx{1.0};   /*!< Cached cos(ax). */
        double ay{0.0};      /*!< Y rotation angle. */
        double sinAy{0.0};   /*!< Cached sin(ay). */
        double cosAy{1.0};   /*!< Cached cos(ay). */
        double az{0.0};      /*!< Z rotation angle. */
        double sinAz{0.0};   /*!< Cached sin(az). */
        double cosAz{1.0};   /*!< Cached cos(az). */
        double kDepth{0.5};  /*!< Perspective depth factor. */
        double scale{1.0};   /*!< Uniform scale. */
        double xOff{0.4};    /*!< X translation. */
        double yOff{0.0};    /*!< Y (depth) translation. */
        double zOff{-0.3};   /*!< Z translation. */
    };

    /**
     * @struct Point3D
     * @brief A point in the pre-projection normalised model space.
     */
    struct Point3D
    {
        double x{0.0}; /*!< Model X. */
        double y{0.0}; /*!< Model Y (depth). */
        double z{0.0}; /*!< Model Z (height). */
    };

    void Rotate3D(Point3D &par_Point) const;
    void Scale3D(Point3D &par_Point) const;
    void Move3D(Point3D &par_Point) const;
    ImVec2 Transform(const Point3D &par_Point, double par_HalfWidth, double par_HalfHeight) const;
    ImVec2 Project(Point3D par_Point, double par_HalfWidth, double par_HalfHeight) const;

    void PaintAxis(ImDrawList *par_pDrawList, const ImVec2 &par_Origin, double par_HalfWidth,
                   double par_HalfHeight, A2LCalMapData &par_Data, int par_AxisFlag);
    void AxisPointer(ImDrawList *par_pDrawList, const ImVec2 &par_Origin, double par_HalfWidth,
                     double par_HalfHeight, const Point3D &par_Base, int par_Dir);

    Trans3D m_trans{}; /*!< Current view transform. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_WIDGETS_A2LCALMAP_A2LCALMAP3DRENDERER_H
