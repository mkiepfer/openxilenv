/**
 * @file Src/GUI/ImGui/Framework/UpdateScheduler.h
 * @brief Wall-clock accumulator driving per-rate value refreshes (replaces the 3 QTimers).
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_FRAMEWORK_UPDATESCHEDULER_H
#define SRC_GUI_IMGUI_FRAMEWORK_UPDATESCHEDULER_H

#include "WindowContext.h"

namespace XilEnvImGui
{

/**
 * @class UpdateScheduler
 * @brief Decides, once per frame, which update rates are due.
 * @details Replaces WindowUpdateTimers: Advance() is called with the frame delta,
 *          then ShouldUpdate() reports whether a given rate fired this frame. Fast
 *          fires every frame; Medium/Slow fire on their periods (500 ms / 1000 ms,
 *          matching the Qt intervals). The first frame after construction reports
 *          all rates as due so windows populate immediately.
 */
class UpdateScheduler
{
public:
    /**
     * @brief Advances the accumulators by one frame and latches the due flags.
     * @param[in] par_DeltaMilliSeconds Elapsed wall-clock time since the last call, in ms.
     */
    void Advance(double par_DeltaMilliSeconds);

    /**
     * @brief Reports whether the given update rate fired in the current frame.
     * @param[in] par_Rate Update rate to query.
     * @return true if a refresh is due this frame, false otherwise.
     */
    [[nodiscard]] bool ShouldUpdate(UpdateRate par_Rate) const;

private:
    static constexpr double MEDIUM_PERIOD_MS{500.0};  /*!< Medium refresh period. */
    static constexpr double SLOW_PERIOD_MS{1000.0};   /*!< Slow refresh period. */

    double m_mediumAccumulator{MEDIUM_PERIOD_MS}; /*!< Ms since last medium tick (due on first frame). */
    double m_slowAccumulator{SLOW_PERIOD_MS};     /*!< Ms since last slow tick (due on first frame). */
    bool m_mediumDue{true};                       /*!< Latched: medium fired this frame. */
    bool m_slowDue{true};                         /*!< Latched: slow fired this frame. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_FRAMEWORK_UPDATESCHEDULER_H
