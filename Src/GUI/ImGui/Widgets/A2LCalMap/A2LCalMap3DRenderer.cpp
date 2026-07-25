#include "Widgets/A2LCalMap/A2LCalMap3DRenderer.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
#include "PrintFormatToString.h"
}

namespace XilEnvImGui
{

namespace
{

constexpr int X_AXIS_NO{0};
constexpr int Y_AXIS_NO{1};
constexpr int MAP_NO{2};

constexpr double POINTER_WIDTH{0.03};
constexpr double POINTER_HEIGHT{0.1};

constexpr ImU32 COL_BACKGROUND{IM_COL32(255, 255, 255, 255)};
constexpr ImU32 COL_LINE{IM_COL32(0, 0, 0, 255)};
constexpr ImU32 COL_TEXT{IM_COL32(0, 0, 0, 255)};
constexpr ImU32 COL_SURFACE{IM_COL32(111, 210, 143, 255)};
constexpr ImU32 COL_AXIS_FILL{IM_COL32(210, 210, 210, 255)};
constexpr ImU32 COL_PICK{IM_COL32(255, 0, 0, 255)};
constexpr ImU32 COL_ACCESS{IM_COL32(0, 0, 255, 255)};

using AxisType = A2LCalMapData::AxisType;

} // namespace

void A2LCalMap3DRenderer::UpdateSinCos()
{
    m_trans.ax -= std::floor(m_trans.ax / (2.0 * M_PI)) * (2.0 * M_PI);
    m_trans.sinAx = std::sin(m_trans.ax);
    m_trans.cosAx = std::cos(m_trans.ax);
    m_trans.ay -= std::floor(m_trans.ay / (2.0 * M_PI)) * (2.0 * M_PI);
    m_trans.sinAy = std::sin(m_trans.ay);
    m_trans.cosAy = std::cos(m_trans.ay);
    m_trans.az -= std::floor(m_trans.az / (2.0 * M_PI)) * (2.0 * M_PI);
    m_trans.sinAz = std::sin(m_trans.az);
    m_trans.cosAz = std::cos(m_trans.az);
}

void A2LCalMap3DRenderer::InitTrans3D()
{
    m_trans.ax = 0.0;
    m_trans.ay = 0.0;
    m_trans.az = M_PI;
    UpdateSinCos();
    m_trans.kDepth = 0.5;
    m_trans.scale = 1.0;
    m_trans.xOff = 0.4;
    m_trans.yOff = 0.0;
    m_trans.zOff = -0.3;
}

void A2LCalMap3DRenderer::InitTrans2D()
{
    m_trans.ax = 0.0;
    m_trans.ay = 0.0;
    m_trans.az = M_PI;
    UpdateSinCos();
    m_trans.kDepth = 0.5;
    m_trans.scale = 1.6;
    m_trans.xOff = 0.8;
    m_trans.yOff = 0.0;
    m_trans.zOff = -0.8;
}

void A2LCalMap3DRenderer::Rotate3D(Point3D &par_Point) const
{
    Point3D help;
    // X rotation.
    help.x = par_Point.x;
    help.y = par_Point.y * m_trans.cosAx - par_Point.z * m_trans.sinAx;
    help.z = par_Point.y * m_trans.sinAx + par_Point.z * m_trans.cosAx;
    // Y rotation.
    par_Point.x = help.x * m_trans.cosAy + help.z * m_trans.sinAy;
    par_Point.y = help.y;
    par_Point.z = help.z * m_trans.cosAy - help.x * m_trans.sinAy;
    // Z rotation.
    help.x = par_Point.x * m_trans.cosAz - par_Point.y * m_trans.sinAz;
    help.y = par_Point.y * m_trans.cosAz + par_Point.x * m_trans.sinAz;
    help.z = par_Point.z;
    par_Point = help;
}

void A2LCalMap3DRenderer::Scale3D(Point3D &par_Point) const
{
    par_Point.x *= m_trans.scale;
    par_Point.y *= m_trans.scale;
    par_Point.z *= m_trans.scale;
}

void A2LCalMap3DRenderer::Move3D(Point3D &par_Point) const
{
    par_Point.x += m_trans.xOff;
    par_Point.y += m_trans.yOff;
    par_Point.z += m_trans.zOff;
}

ImVec2 A2LCalMap3DRenderer::Transform(const Point3D &par_Point, double par_HalfWidth, double par_HalfHeight) const
{
    double x2d{0.0};
    double y2d{0.0};
    const double help{1.0 + par_Point.y * m_trans.kDepth};
    if (0.0 != help)
    {
        x2d = par_Point.x / help;
        y2d = par_Point.z / help;
    }
    else
    {
        x2d = 1.0E11;
        y2d = 1.0E11;
    }
    return ImVec2(static_cast<float>(par_HalfWidth - x2d * par_HalfWidth),
                  static_cast<float>(par_HalfHeight - y2d * par_HalfHeight));
}

ImVec2 A2LCalMap3DRenderer::Project(Point3D par_Point, double par_HalfWidth, double par_HalfHeight) const
{
    Rotate3D(par_Point);
    Scale3D(par_Point);
    Move3D(par_Point);
    return Transform(par_Point, par_HalfWidth, par_HalfHeight);
}

void A2LCalMap3DRenderer::AxisPointer(ImDrawList *par_pDrawList, const ImVec2 &par_Origin, double par_HalfWidth,
                                      double par_HalfHeight, const Point3D &par_Base, int par_Dir)
{
    Point3D a{par_Base};
    Point3D b{par_Base};
    const Point3D c{par_Base};
    switch (par_Dir)
    {
    case 0: // x
        a.x -= POINTER_HEIGHT;
        b.x -= POINTER_HEIGHT;
        a.y -= POINTER_WIDTH;
        b.y += POINTER_WIDTH;
        break;
    case 1: // y
        a.y -= POINTER_HEIGHT;
        b.y -= POINTER_HEIGHT;
        a.x -= POINTER_WIDTH;
        b.x += POINTER_WIDTH;
        break;
    case 2: // z
        a.z -= POINTER_HEIGHT;
        b.z -= POINTER_HEIGHT;
        a.y -= POINTER_WIDTH;
        b.y += POINTER_WIDTH;
        break;
    default:
        break;
    }
    const ImVec2 pa{Project(a, par_HalfWidth, par_HalfHeight)};
    const ImVec2 pb{Project(b, par_HalfWidth, par_HalfHeight)};
    const ImVec2 pc{Project(c, par_HalfWidth, par_HalfHeight)};
    const ImVec2 oa{par_Origin.x + pa.x, par_Origin.y + pa.y};
    const ImVec2 ob{par_Origin.x + pb.x, par_Origin.y + pb.y};
    const ImVec2 oc{par_Origin.x + pc.x, par_Origin.y + pc.y};
    par_pDrawList->AddTriangleFilled(oa, ob, oc, COL_AXIS_FILL);
    par_pDrawList->AddTriangle(oa, ob, oc, COL_LINE);
}

void A2LCalMap3DRenderer::PaintAxis(ImDrawList *par_pDrawList, const ImVec2 &par_Origin, double par_HalfWidth,
                                    double par_HalfHeight, A2LCalMapData &par_Data, int par_AxisFlag)
{
    const Point3D nullPoint{0.0, 0.0, 0.0};
    const Point3D xAxis{1.0, 0.0, 0.0};
    const Point3D yAxis{0.0, 1.0, 0.0};
    const Point3D zAxis{0.0, 0.0, 1.0};

    auto drawAxis = [&](const Point3D &par_End, int par_Dir, const char *par_Input, const char *par_Unit) {
        const ImVec2 origin2d{Project(nullPoint, par_HalfWidth, par_HalfHeight)};
        const ImVec2 end2d{Project(par_End, par_HalfWidth, par_HalfHeight)};
        const ImVec2 p0{par_Origin.x + origin2d.x, par_Origin.y + origin2d.y};
        const ImVec2 p1{par_Origin.x + end2d.x, par_Origin.y + end2d.y};
        par_pDrawList->AddLine(p0, p1, COL_LINE);
        AxisPointer(par_pDrawList, par_Origin, par_HalfWidth, par_HalfHeight, par_End, par_Dir);
        char label[256];
        std::snprintf(label, sizeof(label), "%s%s%s%s", (nullptr != par_Input) ? par_Input : "no input",
                      ((nullptr != par_Unit) && (0 != par_Unit[0])) ? " [" : "",
                      ((nullptr != par_Unit) && (0 != par_Unit[0])) ? par_Unit : "",
                      ((nullptr != par_Unit) && (0 != par_Unit[0])) ? "]" : "");
        par_pDrawList->AddText(p1, COL_TEXT, label);
    };

    if (0x01 == (par_AxisFlag & 0x01))
    {
        drawAxis(xAxis, 0, par_Data.GetXInput(), par_Data.GetXUnit());
    }
    if ((2 == par_Data.GetType()) && (0x02 == (par_AxisFlag & 0x02)))
    {
        drawAxis(yAxis, 1, par_Data.GetYInput(), par_Data.GetYUnit());
    }
    if (0x04 == (par_AxisFlag & 0x04))
    {
        drawAxis(zAxis, 2, "", "");
    }
}

void A2LCalMap3DRenderer::Render(ImDrawList *par_pDrawList, const ImVec2 &par_Origin, const ImVec2 &par_Size,
                                 A2LCalMapData &par_Data)
{
    par_pDrawList->AddRectFilled(par_Origin, ImVec2(par_Origin.x + par_Size.x, par_Origin.y + par_Size.y),
                                 COL_BACKGROUND);
    if (!par_Data.IsValid())
    {
        return;
    }

    par_pDrawList->PushClipRect(par_Origin, ImVec2(par_Origin.x + par_Size.x, par_Origin.y + par_Size.y), true);

    const double halfWidth{par_Size.x / 2.0};
    const double halfHeight{par_Size.y / 2.0};
    const int xDim{par_Data.GetXDim()};
    const int yDim{par_Data.GetYDim()};

    const Point3D nullPoint{0.0, 0.0, 0.0};
    const Point3D xAxis{1.0, 0.0, 0.0};
    const Point3D yAxis{0.0, 1.0, 0.0};
    const Point3D zAxis{0.0, 0.0, 1.0};

    auto depthOf = [&](Point3D p) {
        Rotate3D(p);
        Scale3D(p);
        Move3D(p);
        return p.y;
    };
    auto screenOfNorm = [&](int par_X, int par_Y) {
        Point3D p;
        p.x = par_Data.GetValueNorm(AxisType::XAxis, par_X, 0);
        p.y = (2 == par_Data.GetType()) ? par_Data.GetValueNorm(AxisType::YAxis, par_Y, 0) : 0.0;
        p.z = par_Data.GetValueNorm(AxisType::Map, par_X, par_Y);
        const ImVec2 rel{Project(p, halfWidth, halfHeight)};
        return ImVec2(par_Origin.x + rel.x, par_Origin.y + rel.y);
    };

    int xStart{0};
    int xEnd{xDim - 1};
    int dx{1};
    int yStart{0};
    int yEnd{yDim - 1};
    int dy{1};
    int axisMask{0xFF};

    if ((2 == par_Data.GetType()) && ((2 <= xDim) || (2 <= yDim)))
    {
        const double zxa{depthOf(xAxis)};
        const double zya{depthOf(yAxis)};
        const double zza{depthOf(zAxis)};
        const double z0p{depthOf(nullPoint)};

        Point3D c1;
        c1.x = par_Data.GetValueNorm(AxisType::XAxis, 0, 0);
        c1.y = par_Data.GetValueNorm(AxisType::YAxis, 0, 0);
        c1.z = par_Data.GetValueNorm(AxisType::Map, 0, 0);
        const double depth1{depthOf(c1)};
        Point3D c2;
        c2.x = par_Data.GetValueNorm(AxisType::XAxis, 0, 0);
        c2.y = par_Data.GetValueNorm(AxisType::YAxis, yDim - 1, 0);
        c2.z = par_Data.GetValueNorm(AxisType::Map, 0, yDim - 1);
        const double depth2{depthOf(c2)};
        Point3D c3;
        c3.x = par_Data.GetValueNorm(AxisType::XAxis, xDim - 1, 0);
        c3.y = par_Data.GetValueNorm(AxisType::YAxis, yDim - 1, 0);
        c3.z = par_Data.GetValueNorm(AxisType::Map, xDim - 1, yDim - 1);
        const double depth3{depthOf(c3)};
        Point3D c4;
        c4.x = par_Data.GetValueNorm(AxisType::XAxis, xDim - 1, 0);
        c4.y = par_Data.GetValueNorm(AxisType::YAxis, 0, 0);
        c4.z = par_Data.GetValueNorm(AxisType::Map, xDim - 1, 0);
        const double depth4{depthOf(c4)};

        axisMask = 0x00;
        auto setMask = [&](double par_Depth) {
            if ((par_Depth < zxa) || (par_Depth < z0p))
            {
                axisMask |= 0x01;
            }
            if ((par_Depth < zya) || (par_Depth < z0p))
            {
                axisMask |= 0x02;
            }
            if ((par_Depth < zza) || (par_Depth < z0p))
            {
                axisMask |= 0x04;
            }
        };
        if (depth1 > depth2)
        {
            if (depth1 > depth3)
            {
                if (depth1 > depth4)
                {
                    xStart = 0, xEnd = xDim - 1, dx = 1, yStart = 0, yEnd = yDim - 1, dy = 1;
                    setMask(depth1);
                }
                else
                {
                    xStart = xDim - 2, xEnd = -1, dx = -1, yStart = 0, yEnd = yDim - 1, dy = 1;
                    setMask(depth4);
                }
            }
            else
            {
                if (depth2 > depth4)
                {
                    xStart = 0, xEnd = xDim - 1, dx = 1, yStart = yDim - 2, yEnd = -1, dy = -1;
                    setMask(depth2);
                }
                else
                {
                    xStart = xDim - 2, xEnd = -1, dx = -1, yStart = 0, yEnd = yDim - 1, dy = 1;
                    setMask(depth4);
                }
            }
        }
        else
        {
            if (depth2 > depth3)
            {
                if (depth2 > depth4)
                {
                    xStart = 0, xEnd = xDim - 1, dx = 1, yStart = yDim - 2, yEnd = -1, dy = -1;
                    setMask(depth2);
                }
                else
                {
                    xStart = xDim - 2, xEnd = -1, dx = -1, yStart = 0, yEnd = yDim - 1, dy = 1;
                    setMask(depth4);
                }
            }
            else
            {
                if (depth3 > depth4)
                {
                    xStart = xDim - 2, xEnd = -1, dx = -1, yStart = yDim - 2, yEnd = -1, dy = -1;
                    setMask(depth3);
                }
                else
                {
                    xStart = xDim - 2, xEnd = -1, dx = -1, yStart = 0, yEnd = yDim - 1, dy = 1;
                    setMask(depth4);
                }
            }
        }
    }

    // Axes behind the surface.
    PaintAxis(par_pDrawList, par_Origin, halfWidth, halfHeight, par_Data, axisMask);

    double accessValue{0.0};
    if (2 == par_Data.GetType())
    {
        for (int x{xStart}; x != xEnd; x += dx)
        {
            for (int y{yStart}; y != yEnd; y += dy)
            {
                const ImVec2 p0{screenOfNorm(x, y)};
                const ImVec2 p1{screenOfNorm(x + 1, y)};
                const ImVec2 p2{screenOfNorm(x + 1, y + 1)};
                const ImVec2 p3{screenOfNorm(x, y + 1)};
                par_pDrawList->AddQuadFilled(p0, p1, p2, p3, COL_SURFACE);
                par_pDrawList->AddQuad(p0, p1, p2, p3, COL_LINE);
                par_pDrawList->AddRectFilled(ImVec2(p0.x - 2.0f, p0.y - 2.0f), ImVec2(p0.x + 2.0f, p0.y + 2.0f),
                                             COL_LINE);
                if ((xDim - 2) == x)
                {
                    par_pDrawList->AddRectFilled(ImVec2(p1.x - 2.0f, p1.y - 2.0f),
                                                 ImVec2(p1.x + 2.0f, p1.y + 2.0f), COL_LINE);
                }
                if (((xDim - 2) == x) && ((yDim - 2) == y))
                {
                    par_pDrawList->AddRectFilled(ImVec2(p2.x - 2.0f, p2.y - 2.0f),
                                                 ImVec2(p2.x + 2.0f, p2.y + 2.0f), COL_LINE);
                }
                if ((yDim - 2) == y)
                {
                    par_pDrawList->AddRectFilled(ImVec2(p3.x - 2.0f, p3.y - 2.0f),
                                                 ImVec2(p3.x + 2.0f, p3.y + 2.0f), COL_LINE);
                }
            }
        }
    }
    else if (1 == par_Data.GetType())
    {
        ImVec2 prev{};
        for (int x{0}; x != xDim; ++x)
        {
            Point3D p;
            p.x = par_Data.GetValueNorm(AxisType::XAxis, x, 0);
            p.y = 0.0;
            p.z = par_Data.GetValueNorm(AxisType::Map, x, 0);
            const ImVec2 rel{Project(p, halfWidth, halfHeight)};
            const ImVec2 screen{par_Origin.x + rel.x, par_Origin.y + rel.y};
            if (0 != x)
            {
                par_pDrawList->AddLine(prev, screen, COL_LINE);
            }
            par_pDrawList->AddRectFilled(ImVec2(screen.x - 2.0f, screen.y - 2.0f),
                                         ImVec2(screen.x + 2.0f, screen.y + 2.0f), COL_LINE);
            prev = screen;
        }
    }

    // Axes in front of the surface.
    PaintAxis(par_pDrawList, par_Origin, halfWidth, halfHeight, par_Data, ~axisMask);

    // Picking crosses.
    for (int y{0}; y < yDim; ++y)
    {
        for (int x{0}; x < xDim; ++x)
        {
            if (par_Data.IsPicked(x, y))
            {
                const ImVec2 p{screenOfNorm(x, y)};
                par_pDrawList->AddLine(ImVec2(p.x - 6.0f, p.y), ImVec2(p.x + 6.0f, p.y), COL_PICK);
                par_pDrawList->AddLine(ImVec2(p.x, p.y - 6.0f), ImVec2(p.x, p.y + 6.0f), COL_PICK);
            }
        }
    }

    // Operating-point marker from the axis-input variables.
    const bool hasAccess{((2 == par_Data.GetType()) && (0 < par_Data.GetYVid()) && (0 < par_Data.GetXVid())) ||
                         ((1 == par_Data.GetType()) && (0 < par_Data.GetXVid()))};
    if ((0 < xDim) && (0 < yDim) && hasAccess)
    {
        Point3D ap;
        accessValue = par_Data.MapAccess(&ap.x, &ap.y, &ap.z);
        const ImVec2 rel{Project(ap, halfWidth, halfHeight)};
        const ImVec2 p{par_Origin.x + rel.x, par_Origin.y + rel.y};
        par_pDrawList->AddLine(p, ImVec2(p.x - 3.0f, p.y - 8.0f), COL_ACCESS);
        par_pDrawList->AddLine(ImVec2(p.x - 3.0f, p.y - 8.0f), ImVec2(p.x + 3.0f, p.y - 8.0f), COL_ACCESS);
        par_pDrawList->AddLine(ImVec2(p.x + 3.0f, p.y - 8.0f), p, COL_ACCESS);
    }

    // Title (top) and value read-out (bottom).
    const char *pTitle{par_Data.GetCharacteristicName().c_str()};
    const ImVec2 titleSize{ImGui::CalcTextSize(pTitle)};
    par_pDrawList->AddText(ImVec2(par_Origin.x + (par_Size.x - titleSize.x) / 2.0f, par_Origin.y + 2.0f),
                           COL_TEXT, pTitle);

    int iPick{0};
    int jPick{0};
    par_Data.GetLastPick(&iPick, &jPick);
    const double vx{par_Data.GetRawValue(AxisType::XAxis, iPick, 0)};
    const double vy{par_Data.GetRawValue(AxisType::YAxis, jPick, 0)};
    const double vz{par_Data.GetRawValue(AxisType::Map, iPick, jPick)};
    char bottomLine[1024];
    PrintFormatToString(bottomLine, sizeof(bottomLine), "%s  X = %g %s  Y = %g %s  Z = %g %s",
                        par_Data.IsPhysical() ? "(phys)" : "(raw)", vx, par_Data.GetXUnit(), vy,
                        par_Data.GetYUnit(), vz, par_Data.GetMapUnit());
    if (hasAccess)
    {
        PrintFormatToString(bottomLine + std::strlen(bottomLine), sizeof(bottomLine) - std::strlen(bottomLine),
                            "  AP = %g %s", accessValue, par_Data.GetMapUnit());
    }
    const ImVec2 bottomSize{ImGui::CalcTextSize(bottomLine)};
    par_pDrawList->AddText(ImVec2(par_Origin.x + (par_Size.x - bottomSize.x) / 2.0f,
                                  par_Origin.y + par_Size.y - bottomSize.y - 2.0f),
                           COL_TEXT, bottomLine);

    par_pDrawList->PopClipRect();
}

bool A2LCalMap3DRenderer::PickingMap(int par_HalfWidth, int par_HalfHeight, int par_XPos, int par_YPos,
                                     bool par_Add, A2LCalMapData &par_Data)
{
    const int xDim{par_Data.GetXDim()};
    const int yDim{par_Data.GetYDim()};
    int nearest{0x7FFFFFFF};
    int xPick{0};
    int yPick{0};

    for (int x{0}; x < xDim; ++x)
    {
        for (int y{0}; y < yDim; ++y)
        {
            if (!par_Add)
            {
                par_Data.SetPick(x, y, 2); // no Ctrl -> clear all first
            }
            Point3D p;
            p.x = par_Data.GetValueNorm(AxisType::XAxis, x, 0);
            p.y = par_Data.GetValueNorm(AxisType::YAxis, y, 0);
            p.z = par_Data.GetValueNorm(AxisType::Map, x, y);
            const ImVec2 rel{Project(p, par_HalfWidth, par_HalfHeight)};
            const int dpx{static_cast<int>(rel.x) - par_XPos};
            const int dpy{static_cast<int>(rel.y) - par_YPos};
            const int distSquared{dpx * dpx + dpy * dpy};
            const int dist{(distSquared < 0) ? 0x7FFFFFFF
                                             : static_cast<int>(std::sqrt(static_cast<double>(distSquared)))};
            if (dist < nearest)
            {
                nearest = dist;
                xPick = x;
                yPick = y;
            }
        }
    }
    if (0x7FFFFFFF == nearest)
    {
        return false;
    }
    if (par_Add)
    {
        par_Data.SetPick(xPick, yPick, par_Data.IsPicked(xPick, yPick) ? 2 : 1);
    }
    else
    {
        par_Data.SetPick(xPick, yPick, 3);
    }
    return true;
}

} // namespace XilEnvImGui
