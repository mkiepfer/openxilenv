/**
 * @file Src/GUI/ImGui/Framework/A2LCalBridge.h
 * @brief Thread-marshalling bridge between the A2L link/scheduler thread and the ImGui render thread.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_FRAMEWORK_A2LCALBRIDGE_H
#define SRC_GUI_IMGUI_FRAMEWORK_A2LCALBRIDGE_H

#include <deque>
#include <mutex>
#include <string>
#include <vector>

extern "C"
{
#include "A2LAccessData.h"
}

namespace XilEnvImGui
{

/**
 * @class IA2LCalWidget
 * @brief Abstract sink for A2L calibration notifications delivered on the render thread.
 * @details Implemented by the A2LCalSingle / A2LCalMap window instances. All three callbacks
 *          are invoked on the ImGui render thread (either synchronously via
 *          A2LCalBridge::NotifyDataChanged or after marshalling via A2LCalBridge::Drain).
 *          Implementations self-filter link/channel/index as needed (mirrors the Qt widgets).
 */
class IA2LCalWidget
{
public:
    /**
     * @brief Virtual destructor for safe polymorphic ownership.
     */
    virtual ~IA2LCalWidget() = default;

    /**
     * @brief Notifies the widget that link data for a characteristic changed.
     * @param[in] linkNo Link (process) number the data belongs to.
     * @param[in] index Characteristic index within the link.
     * @param[in] pData Descriptor of the changed value. Owned by the caller for the call duration.
     * @note Dispatched synchronously on the render thread (non-marshalled path).
     */
    virtual void OnLinkDataChanged(int linkNo, int index, A2L_DATA *pData) = 0;

    /**
     * @brief Notifies the widget that a process link was started or stopped.
     * @param[in] processName Name of the affected process.
     * @param[in] flags Process link flags forwarded from the scheduler.
     * @param[in] action Start/stop action code forwarded from the scheduler.
     */
    virtual void OnProcessStartStop(const std::string &processName, int flags, int action) = 0;

    /**
     * @brief Notifies the widget that an asynchronous link-data fetch completed.
     * @param[in] pIndexData Filled INDEX_DATA_BLOCK (as void*). Valid only for the call duration;
     *                       the bridge frees it after all widgets were notified.
     * @param[in] fetchDataChannelNo Channel number the fetch was requested on. Widgets ignore
     *                               acknowledgements for channels they did not register.
     */
    virtual void OnGetDataFromLinkAck(void *pIndexData, int fetchDataChannelNo) = 0;
};

/**
 * @class A2LCalBridge
 * @brief Marshals asynchronous A2L link callbacks onto the ImGui render thread.
 * @details The C link/scheduler thread enqueues process start/stop and fetch-acknowledge events
 *          through the extern-C entry points; the render thread drains them once per frame and
 *          dispatches to every registered widget. Direct value-change notifications bypass the
 *          queue and are dispatched synchronously (mirrors the Qt A2LCalMapWidgetSyncObject).
 * @warning RegisterWidget, UnregisterWidget, NotifyDataChanged and Drain MUST be called on the
 *          render thread only. EnqueueProcessStartStop and EnqueueGetDataFromLinkAck are the only
 *          methods safe to call from the link/scheduler thread.
 */
class A2LCalBridge
{
public:
    /**
     * @brief Returns the process-wide bridge instance.
     * @return Reference to the singleton.
     */
    static A2LCalBridge &GetInstance();

    /**
     * @brief Registers a widget to receive A2L notifications.
     * @param[in] pWidget Widget to register. Ignored if nullptr or already registered.
     * @warning Render-thread only (widget constructor).
     */
    void RegisterWidget(IA2LCalWidget *pWidget);

    /**
     * @brief Unregisters a previously registered widget.
     * @param[in] pWidget Widget to remove. Ignored if nullptr or not registered.
     * @warning Render-thread only (widget destructor).
     */
    void UnregisterWidget(IA2LCalWidget *pWidget);

    /**
     * @brief Dispatches a value-change notification synchronously to all registered widgets.
     * @param[in] linkNo Link (process) number.
     * @param[in] index Characteristic index.
     * @param[in] pData Descriptor of the changed value.
     * @warning Render-thread only (non-marshalled direct path).
     */
    void NotifyDataChanged(int linkNo, int index, A2L_DATA *pData);

    /**
     * @brief Enqueues a process start/stop event for later dispatch on the render thread.
     * @param[in] processName Process name; copied into the queued command at enqueue time.
     * @param[in] flags Process link flags.
     * @param[in] action Start/stop action code.
     * @note Thread-safe; typically called from the link/scheduler thread.
     */
    void EnqueueProcessStartStop(const char *processName, int flags, int action);

    /**
     * @brief Enqueues an asynchronous fetch acknowledgement for later dispatch.
     * @param[in] pIndexData Filled INDEX_DATA_BLOCK (as void*). Ownership transfers to the queue;
     *                       the block is freed after dispatch in Drain().
     * @param[in] fetchDataChannelNo Channel number the fetch was requested on.
     * @note Thread-safe; typically called from the link/scheduler thread.
     */
    void EnqueueGetDataFromLinkAck(void *pIndexData, int fetchDataChannelNo);

    /**
     * @brief Drains all queued commands and dispatches them to the registered widgets.
     * @details Swaps the queue out under the mutex, then dispatches every command outside the lock.
     *          Each fetch-acknowledge INDEX_DATA_BLOCK is freed after its dispatch.
     * @warning Render-thread only; call once per frame.
     */
    void Drain();

private:
    A2LCalBridge() = default;

    /**
     * @struct Command
     * @brief Tagged, self-contained marshalled event carried through the queue.
     */
    struct Command
    {
        /**
         * @enum class Kind
         * @brief Discriminates the marshalled command payload.
         */
        enum class Kind
        {
            ProcessStartStop, /*!< Process start/stop event. */
            GetDataAck        /*!< Asynchronous fetch acknowledgement. */
        };

        Kind kind{Kind::ProcessStartStop}; /*!< Active payload selector. */
        std::string processName;           /*!< Process name (ProcessStartStop). */
        int flags{0};                      /*!< Process link flags (ProcessStartStop). */
        int action{0};                     /*!< Start/stop action code (ProcessStartStop). */
        void *pIndexData{nullptr};         /*!< Owned INDEX_DATA_BLOCK (GetDataAck). */
        int fetchDataChannelNo{0};         /*!< Fetch channel number (GetDataAck). */
    };

    std::vector<IA2LCalWidget *> m_widgets; /*!< Registered widgets; render-thread only. */
    std::mutex m_queueMutex;                /*!< Guards m_queue across threads. */
    std::deque<Command> m_queue;            /*!< Pending marshalled commands. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_FRAMEWORK_A2LCALBRIDGE_H
