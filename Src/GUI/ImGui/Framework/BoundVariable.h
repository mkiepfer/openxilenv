/**
 * @file Src/GUI/ImGui/Framework/BoundVariable.h
 * @brief One bound blackboard variable with Qt KnobOrTachoWidget-compatible config round-trip.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_FRAMEWORK_BOUNDVARIABLE_H
#define SRC_GUI_IMGUI_FRAMEWORK_BOUNDVARIABLE_H

#include <cstdint>
#include <string>

#include "Framework/BlackboardDragDrop.h"

namespace XilEnvImGui
{

/**
 * @class BoundVariable
 * @brief Encapsulates a single blackboard variable binding for gauge-style widgets.
 * @details Attaches with add_bbvari(BB_UNKNOWN_WAIT), caches min/max/color/precision,
 *          and reads/writes physical or raw values exactly like the Qt
 *          KnobOrTachoWidget. Serializes to the Qt "variable" entry
 *          ("name, lower, upper, (r,g,b), phys|dec") so files stay interchangeable.
 * @warning Not thread-safe; use on the render thread only.
 */
class BoundVariable
{
public:
    BoundVariable() = default;
    ~BoundVariable();

    BoundVariable(const BoundVariable &) = delete;
    BoundVariable &operator=(const BoundVariable &) = delete;

    /**
     * @brief Binds to a variable by name, fetching min/max/color from the blackboard.
     * @param[in] par_Name Variable name. Empty unbinds.
     */
    void Bind(const std::string &par_Name);

    /**
     * @brief Binds from a dropped payload, preferring its min/max/color when present.
     * @param[in] par_Payload Payload carrying at least a name.
     */
    void BindFromPayload(const BlackboardVariablePayload &par_Payload);

    /**
     * @brief Detaches the current variable (if any).
     */
    void Unbind();

    /**
     * @brief Reports whether a variable is currently bound and attached.
     * @return true if bound (VID > 0), false otherwise.
     */
    [[nodiscard]] bool IsBound() const
    {
        return (0 < m_vid);
    }

    /**
     * @brief Returns the bound variable name.
     * @return Const reference to the name (empty when unbound).
     */
    [[nodiscard]] const std::string &GetName() const
    {
        return m_name;
    }

    /**
     * @brief Returns the cached VID.
     * @return VID (>0 when bound), 0 otherwise.
     */
    [[nodiscard]] std::int32_t GetVid() const
    {
        return m_vid;
    }

    /**
     * @brief Reads the current value (physical or raw), clamped to [min,max].
     * @return Clamped value, or the lower bound when unbound.
     */
    [[nodiscard]] double ReadValue() const;

    /**
     * @brief Writes a value with min/max checking (physical or raw as configured).
     * @param[in] par_Value New value.
     */
    void WriteValue(double par_Value) const;

    [[nodiscard]] double GetMin() const
    {
        return m_min;
    }
    [[nodiscard]] double GetMax() const
    {
        return m_max;
    }
    [[nodiscard]] std::int32_t GetColor() const
    {
        return m_color;
    }
    [[nodiscard]] int GetPrecision() const
    {
        return m_precision;
    }
    [[nodiscard]] bool IsPhysical() const
    {
        return m_physical;
    }

    void SetPhysical(bool par_Physical)
    {
        m_physical = par_Physical;
    }

    /**
     * @brief Overrides the value range.
     * @param[in] par_Min Lower bound.
     * @param[in] par_Max Upper bound (forced above min if not strictly greater).
     */
    void SetMinMax(double par_Min, double par_Max);

    /**
     * @brief Reads the unit string into a caller buffer.
     * @param[out] par_Buffer Destination buffer. Must not be nullptr.
     * @param[in] par_Size Buffer size. Must be > 0.
     */
    void ReadUnit(char *par_Buffer, int par_Size) const;

    /**
     * @brief Writes the Qt "variable" entry into the section.
     * @param[in] par_FileDescriptor Ini descriptor.
     * @param[in] par_Section Section name. Must not be nullptr.
     */
    void WriteQtConfig(int par_FileDescriptor, const char *par_Section) const;

    /**
     * @brief Reads and binds from the Qt "variable" entry in the section.
     * @param[in] par_FileDescriptor Ini descriptor.
     * @param[in] par_Section Section name. Must not be nullptr.
     */
    void ReadQtConfig(int par_FileDescriptor, const char *par_Section);

private:
    void RefreshMetadata();

    std::string m_name{};       /*!< Bound variable name. */
    std::int32_t m_vid{0};      /*!< Cached VID (>0 when bound). */
    double m_min{0.0};          /*!< Lower bound. */
    double m_max{1.0};          /*!< Upper bound. */
    std::int32_t m_color{0};    /*!< 0x00bbggrr color. */
    int m_precision{0};         /*!< Display precision. */
    bool m_physical{false};     /*!< Physical (converted) vs. raw value handling. Defaults to
                                     raw/decimal like the Qt KnobOrTachoWidget, so a freshly
                                     dropped variable (payload display-mode Unknown) shows dec,
                                     not phys; config load and explicit drop modes override it. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_FRAMEWORK_BOUNDVARIABLE_H
