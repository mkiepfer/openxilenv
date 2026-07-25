#include "UpdateScheduler.h"

namespace XilEnvImGui
{

void UpdateScheduler::Advance(double par_DeltaMilliSeconds)
{
    m_mediumAccumulator += par_DeltaMilliSeconds;
    m_slowAccumulator += par_DeltaMilliSeconds;

    m_mediumDue = false;
    m_slowDue = false;

    if (MEDIUM_PERIOD_MS <= m_mediumAccumulator)
    {
        m_mediumDue = true;
        // Preserve fractional overshoot to avoid long-term drift.
        m_mediumAccumulator -= MEDIUM_PERIOD_MS;
        if (MEDIUM_PERIOD_MS <= m_mediumAccumulator)
        {
            m_mediumAccumulator = 0.0;
        }
    }
    if (SLOW_PERIOD_MS <= m_slowAccumulator)
    {
        m_slowDue = true;
        m_slowAccumulator -= SLOW_PERIOD_MS;
        if (SLOW_PERIOD_MS <= m_slowAccumulator)
        {
            m_slowAccumulator = 0.0;
        }
    }
}

bool UpdateScheduler::ShouldUpdate(UpdateRate par_Rate) const
{
    switch (par_Rate)
    {
    case UpdateRate::Fast:
        return true;
    case UpdateRate::Medium:
        return m_mediumDue;
    case UpdateRate::Slow:
        return m_slowDue;
    case UpdateRate::None:
    default:
        return false;
    }
}

} // namespace XilEnvImGui
