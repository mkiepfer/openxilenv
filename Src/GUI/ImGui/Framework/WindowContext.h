/**
 * @file Src/GUI/ImGui/Framework/WindowContext.h
 * @brief Shared per-frame context and update-rate classification for ImGui window instances.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_FRAMEWORK_WINDOWCONTEXT_H
#define SRC_GUI_IMGUI_FRAMEWORK_WINDOWCONTEXT_H

#include <cstdint>

namespace XilEnvImGui
{

/**
 * @enum class UpdateRate
 * @brief Classifies how often a window's cached values are refreshed.
 * @details Replaces the three Qt QTimers (WindowUpdateTimers): Fast is driven at
 *          display frame rate, Medium/Slow on wall-clock accumulators. None means
 *          the window never needs a periodic value refresh.
 */
enum class UpdateRate : std::uint8_t
{
    Fast,   /*!< Refresh every rendered frame. */
    Medium, /*!< Refresh on the medium accumulator (matches the Qt 500 ms timer). */
    Slow,   /*!< Refresh on the slow accumulator (matches the Qt 1000 ms timer). */
    None    /*!< Never refresh periodically. */
};

/**
 * @struct WindowContext
 * @brief Immutable per-frame data handed to every window during render/update.
 * @details Passed by const reference into IWindowInstance::RenderContent and
 *          IWindowInstance::OnUpdateTick each frame.
 */
struct WindowContext
{
    float dpiScale{1.0f};      /*!< Current viewport DPI scale factor. */
    bool forceRefresh{false};  /*!< When true OnUpdateTick must refresh all cached values. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_FRAMEWORK_WINDOWCONTEXT_H
