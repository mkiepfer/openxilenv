#include "A2LCalBridge.h"

#include <algorithm>
#include <utility>

#include "A2LCalWidgetSync.h"

extern "C"
{
#include "A2LLinkThread.h"
}

namespace XilEnvImGui
{

A2LCalBridge &A2LCalBridge::GetInstance()
{
    static A2LCalBridge s_instance;
    return s_instance;
}

void A2LCalBridge::RegisterWidget(IA2LCalWidget *pWidget)
{
    if (nullptr == pWidget)
    {
        return;
    }
    if (m_widgets.end() == std::find(m_widgets.begin(), m_widgets.end(), pWidget))
    {
        m_widgets.push_back(pWidget);
    }
}

void A2LCalBridge::UnregisterWidget(IA2LCalWidget *pWidget)
{
    if (nullptr == pWidget)
    {
        return;
    }
    m_widgets.erase(std::remove(m_widgets.begin(), m_widgets.end(), pWidget), m_widgets.end());
}

void A2LCalBridge::NotifyDataChanged(int linkNo, int index, A2L_DATA *pData)
{
    for (IA2LCalWidget *pWidget : m_widgets)
    {
        pWidget->OnLinkDataChanged(linkNo, index, pData);
    }
}

void A2LCalBridge::EnqueueProcessStartStop(const char *processName, int flags, int action)
{
    Command command;
    command.kind = Command::Kind::ProcessStartStop;
    // Copy the name now: the caller's buffer is not owned and lives on the link thread.
    command.processName = (nullptr != processName) ? processName : "";
    command.flags = flags;
    command.action = action;

    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_queue.push_back(std::move(command));
}

void A2LCalBridge::EnqueueGetDataFromLinkAck(void *pIndexData, int fetchDataChannelNo)
{
    Command command;
    command.kind = Command::Kind::GetDataAck;
    command.pIndexData = pIndexData; // ownership transfers to the queue
    command.fetchDataChannelNo = fetchDataChannelNo;

    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_queue.push_back(std::move(command));
}

void A2LCalBridge::Drain()
{
    std::deque<Command> pending;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        pending.swap(m_queue);
    }

    // Dispatch outside the queue mutex so widget callbacks never block the link thread.
    for (const Command &command : pending)
    {
        switch (command.kind)
        {
        case Command::Kind::ProcessStartStop:
            for (IA2LCalWidget *pWidget : m_widgets)
            {
                pWidget->OnProcessStartStop(command.processName, command.flags, command.action);
            }
            break;

        case Command::Kind::GetDataAck:
            for (IA2LCalWidget *pWidget : m_widgets)
            {
                pWidget->OnGetDataFromLinkAck(command.pIndexData, command.fetchDataChannelNo);
            }
            // Free on the render thread after all widgets consumed the block (mirrors Qt).
            FreeIndexDataBlock(static_cast<INDEX_DATA_BLOCK *>(command.pIndexData));
            break;

        default:
            break;
        }
    }
}

} // namespace XilEnvImGui

// C-ABI entry points called from the link/scheduler thread. Signatures are fixed by the C core.
extern "C"
{

int GlobalNotifiyA2LCalMapWidgetFuncSchedThread(const char *par_ProcessName, int par_Flags, int par_Action)
{
    XilEnvImGui::A2LCalBridge::GetInstance().EnqueueProcessStartStop(par_ProcessName, par_Flags, par_Action);
    return 0;
}

int GlobalNotifiyGetDataFromLinkAckCallback(void *par_IndexData, int par_FetchDataChannelNo, void *par_Param)
{
    // par_Param carries the registering widget in the Qt model; the ImGui bridge dispatches via
    // the registry instead, so it is unused here (kept for ABI compatibility).
    (void)par_Param;
    XilEnvImGui::A2LCalBridge::GetInstance().EnqueueGetDataFromLinkAck(par_IndexData, par_FetchDataChannelNo);
    return 0;
}

} // extern "C"
