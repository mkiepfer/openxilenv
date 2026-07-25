/**
 * @file Src/GUI/ImGui/Widgets/A2LCalMap/A2LCalMapData.h
 * @brief Data model for the ImGui A2L MAP/CURVE calibration editor (axes + value grid over the link).
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_WIDGETS_A2LCALMAP_A2LCALMAPDATA_H
#define SRC_GUI_IMGUI_WIDGETS_A2LCALMAP_A2LCALMAPDATA_H

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include "A2LAccessData.h"
#include "A2LLink.h"
}

namespace XilEnvImGui
{

/**
 * @class A2LCalMapData
 * @brief Owns a single MAP/CURVE characteristic fetched over the A2L link.
 * @details Immediate-mode port of the Qt A2LCalMapData. Holds the link/channel, the
 *          fetched A2L_DATA grid, the three axis descriptors (X/Y/Map) and the axis-input
 *          blackboard VIDs used for the operating-point marker. All raw/phys conversion,
 *          value read/write, min/max scaling and pick bookkeeping mirror the Qt widget.
 * @warning All methods run on the ImGui render thread only; the A2L link C-API is
 *          internally synchronised so no additional locking is required.
 */
class A2LCalMapData
{
public:
    /**
     * @enum class AxisType
     * @brief Selects which of the three value arrays an accessor addresses.
     */
    enum class AxisType : int
    {
        XAxis = 0, /*!< X axis vector. */
        YAxis = 1, /*!< Y axis vector (map only). */
        Map = 2    /*!< Value grid. */
    };

    A2LCalMapData();
    ~A2LCalMapData();

    A2LCalMapData(const A2LCalMapData &) = delete;
    A2LCalMapData &operator=(const A2LCalMapData &) = delete;

    /**
     * @brief Releases axis descriptors and detaches the axis-input blackboard variables.
     * @param[in] par_Free When true the my_malloc-owned axis strings are freed as well.
     */
    void Reset(bool par_Free = false);

    /**
     * @brief Binds the data model to a process and opens an async link fetch channel.
     * @param[in] par_ProcessName Owning process name.
     * @param[in] par_CallbackParam Opaque callback param forwarded to the link channel.
     */
    void SetProcess(const std::string &par_ProcessName, void *par_CallbackParam);
    [[nodiscard]] const std::string &GetProcess() const
    {
        return m_processName;
    }
    void ProcessTerminated();

    [[nodiscard]] const std::string &GetCharacteristicName() const
    {
        return m_characteristicName;
    }

    /**
     * @brief Resolves a characteristic label on the current link and reads its axis infos.
     * @param[in] par_Characteristic Characteristic (label) name.
     */
    void SetCharacteristicName(const std::string &par_Characteristic);

    [[nodiscard]] int GetLinkNo() const
    {
        return m_linkNo;
    }
    [[nodiscard]] int GetIndex() const
    {
        return m_index;
    }
    [[nodiscard]] int GetDataChannelNo() const
    {
        return m_getDataChannelNo;
    }

    /**
     * @brief Returns the characteristic kind derived from the fetched array count.
     * @return 0 = none/scalar, 1 = curve, 2 = map.
     */
    [[nodiscard]] int GetType() const;

    [[nodiscard]] bool IsPhysical() const
    {
        return m_physicalFlag;
    }
    void SetPhysical(bool par_Physical)
    {
        m_physicalFlag = par_Physical;
    }

    [[nodiscard]] int GetXDim() const;
    [[nodiscard]] int GetYDim() const;

    [[nodiscard]] const char *GetXInput() const;
    [[nodiscard]] const char *GetXUnit() const;
    [[nodiscard]] const char *GetYInput() const;
    [[nodiscard]] const char *GetYUnit() const;
    [[nodiscard]] const char *GetMapUnit() const;

    [[nodiscard]] int GetXVid() const
    {
        return m_xVid;
    }
    [[nodiscard]] int GetYVid() const
    {
        return m_yVid;
    }

    /**
     * @brief Returns the mutable single-value descriptor at a grid coordinate.
     * @param[in] par_Axis Axis to address.
     * @param[in] par_X X index.
     * @param[in] par_Y Y index (ignored for axis vectors).
     * @return Pointer into the owned A2L_DATA grid, or nullptr if out of range.
     */
    [[nodiscard]] A2L_SINGLE_VALUE *GetValue(AxisType par_Axis, int par_X, int par_Y) const;

    [[nodiscard]] double GetRawValue(AxisType par_Axis, int par_X, int par_Y) const;
    [[nodiscard]] double GetDoubleValue(AxisType par_Axis, int par_X, int par_Y);
    [[nodiscard]] double GetValueNorm(AxisType par_Axis, int par_X, int par_Y);
    [[nodiscard]] std::string GetValueString(AxisType par_Axis, int par_X, int par_Y);

    /**
     * @brief Applies an arithmetic operation to one grid value (mirrors Qt ChangeValueOP).
     * @param[in] par_Op Operation code ('I' set, '+', '-', '*', '/', 1 add-min-one).
     * @param[in] par_Value Operand.
     * @param[in] par_Axis Axis to address.
     * @param[in] par_X X index.
     * @param[in] par_Y Y index.
     * @return 0 on success.
     */
    int ChangeValueOP(int par_Op, double par_Value, AxisType par_Axis, int par_X, int par_Y);

    /**
     * @brief Pushes the whole grid back over the link (direction 1 = write).
     * @param[in] par_Readback When true a fresh read is requested afterwards.
     * @return 0 on success.
     */
    int WriteDataToTargetLink(bool par_Readback);

    /**
     * @brief Requests a fresh async read of the whole grid (direction 0 = read).
     * @return 0 on success, -1 when not linked.
     */
    int UpdateReq();

    /**
     * @brief Installs a freshly fetched grid (called from the link ack path).
     * @param[in] par_Data Source A2L_DATA (duplicated internally).
     * @return 0 on success, -1 on duplication failure.
     */
    int UpdateAck(void *par_Data);

    [[nodiscard]] bool IsValid() const
    {
        return nullptr != m_pData;
    }

    void GetLastPick(int *ret_XPick, int *ret_YPick) const;
    void SetPick(int par_X, int par_Y, int par_Op);
    [[nodiscard]] bool IsPicked(int par_X, int par_Y) const;
    void ChangeSelectionOfAll(bool par_Select);

    [[nodiscard]] double GetMaxMinusMin(int par_Axis) const;
    void AdjustMinMax();
    void CalcRawMinMaxValues();

    /**
     * @brief Interpolates the current operating point from the axis-input VIDs.
     * @param[out] ret_PointX Normalised X of the operating point.
     * @param[out] ret_PointY Normalised Y of the operating point.
     * @param[out] ret_PointZ Normalised Z of the operating point.
     * @return Interpolated raw map value at the operating point.
     */
    double MapAccess(double *ret_PointX, double *ret_PointY, double *ret_PointZ);

    [[nodiscard]] double GetDecIncOffset() const
    {
        return m_decIncOffset;
    }
    void SetDecIncOffset(double par_DecIncOffset)
    {
        m_decIncOffset = par_DecIncOffset;
    }

    void SetNoRecursiveSetPickMapFlag(bool par_Flag)
    {
        m_noRecursiveSetPickMapFlag = par_Flag;
    }
    [[nodiscard]] bool GetNoRecursiveSetPickMapFlag() const
    {
        return m_noRecursiveSetPickMapFlag;
    }

private:
    void Clear();
    int TranslateAxisToArrayNo(AxisType par_Axis) const;

    double Conv(double par_Value, CHARACTERISTIC_AXIS_INFO &par_AxisInfo);
    double ConvNorm(double par_Value, CHARACTERISTIC_AXIS_INFO &par_AxisInfo);
    std::string ConvertToString(double par_Value, CHARACTERISTIC_AXIS_INFO &par_AxisInfo, bool par_PhysFlag);
    std::string ConvertValueToString(A2L_SINGLE_VALUE *par_Value, int par_Precision) const;

    void ResetAxisInfo(CHARACTERISTIC_AXIS_INFO *par_AxisInfo, bool par_Free);
    void AttachAxisInput(CHARACTERISTIC_AXIS_INFO &par_AxisInfo, int &ret_Vid);

    bool m_noRecursiveSetPickMapFlag{false}; /*!< Suppresses recursive pick clearing. */

    std::string m_processName{};        /*!< Owning process name. */
    std::string m_characteristicName{}; /*!< Bound characteristic label. */
    int m_linkNo{-1};                   /*!< Resolved A2L link number. */
    int m_getDataChannelNo{-1};         /*!< Async fetch channel number. */
    int m_index{-1};                    /*!< Characteristic index within the link. */

    std::vector<char> m_pickMap{}; /*!< Per-cell pick flags (row-major XDim*YDim). */
    int m_xDimPickMap{-1};         /*!< Pick-map X dimension. */
    int m_yDimPickMap{-1};         /*!< Pick-map Y dimension. */
    int m_xPickLast{0};            /*!< Last picked X index. */
    int m_yPickLast{0};            /*!< Last picked Y index. */

    int m_xVid{-1};             /*!< X-axis input blackboard VID. */
    int m_yVid{-1};             /*!< Y-axis input blackboard VID. */
    double m_decIncOffset{1.0}; /*!< Step size for keyboard inc/dec. */

    bool m_physicalFlag{false}; /*!< Physical vs. raw display/edit mode. */

    A2L_DATA *m_pData{nullptr}; /*!< Owned fetched value grid (FreeA2lData). */

    CHARACTERISTIC_AXIS_INFO m_xAxisInfo{}; /*!< X-axis descriptor. */
    CHARACTERISTIC_AXIS_INFO m_yAxisInfo{}; /*!< Y-axis descriptor. */
    CHARACTERISTIC_AXIS_INFO m_zAxisInfo{}; /*!< Map/value descriptor. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_WIDGETS_A2LCALMAP_A2LCALMAPDATA_H
