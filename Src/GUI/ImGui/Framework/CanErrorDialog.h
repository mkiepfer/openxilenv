/**
 * @file Src/GUI/ImGui/Framework/CanErrorDialog.h
 * @brief Runtime "Insert CAN error" dialog (ports the Qt InsertCANErrorDialog).
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_FRAMEWORK_CANERRORDIALOG_H
#define SRC_GUI_IMGUI_FRAMEWORK_CANERRORDIALOG_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "Framework/CanConfigTree.h"

namespace XilEnvImGui
{

/**
 * @enum class CanErrorMode
 * @brief Which CAN-error injection tab is active (mirrors the Qt tab widget order).
 */
enum class CanErrorMode : int
{
    ChangeBits = 0, /*!< Overwrite a bit field for a number of cycles. */
    ChangeSize = 1, /*!< Change the transmitted DLC/size. */
    Interrupt = 2   /*!< Suppress transmission of the object. */
};

/**
 * @class CanErrorDialogManager
 * @brief Owns and renders the runtime CAN-error injection dialog.
 * @details Process-wide singleton, render-thread only. Opening snapshots the CAN tree (read-only)
 *          so an object/signal can be picked to fill the channel/id/bit fields, then Start/Stop
 *          call the core @c ScriptSetCanErr / @c ScriptClearCanErr injection functions exactly like
 *          the Qt dialog. The last configuration is persisted to @c CAN/Global/can_biterr_history0.
 * @warning Not thread-safe; all methods must be called from the render thread.
 */
class CanErrorDialogManager
{
public:
    /**
     * @brief Returns the process-wide singleton instance.
     * @return Reference to the single CanErrorDialogManager.
     */
    static CanErrorDialogManager &GetInstance();

    /** @brief Snapshots the CAN tree, registers the counter variable and opens the modal. */
    void OpenDialog();

    /** @brief Renders the dialog for the current frame. */
    void Render();

private:
    CanErrorDialogManager() = default;

    void RenderTree();
    void RenderTab(CanErrorMode par_Mode);
    void FillFromObject(const CanTreeVariant &par_Variant, const CanTreeObject &par_Object,
                        const CanTreeSignal *par_Signal);
    bool SearchNextSignal();

    void CollectForStart(CanErrorMode par_Mode);
    void LoadHistory();
    void SaveHistory();
    void RegisterCounter();
    void ReleaseCounter();

    std::vector<CanTreeVariant> m_variants{}; /*!< Read-only CAN tree snapshot. */
    int m_numberOfChannels{1};                /*!< Configured CAN controller count. */

    bool m_open{false};          /*!< Modal visible. */
    bool m_openRequested{false}; /*!< OpenPopup pending. */

    int m_counterVid{-1};       /*!< ".CANBitError.Counter" blackboard variable id. */
    uint64_t m_counterValue{0}; /*!< Last read counter value. */

    int m_channel{0};        /*!< Selected CAN channel. */
    uint32_t m_id{0};        /*!< Object identifier. */
    int m_startBit{0};       /*!< Start bit (encoded per mode on Start). */
    int m_bitSize{1};        /*!< Bit size / new size (per mode on Start). */
    int m_byteOrder{0};      /*!< 0: lsb_first, 1: msb_first. */
    uint64_t m_bitValue{0};  /*!< Bit value to inject. */
    uint32_t m_cycles{0x7FFFFFFF}; /*!< Cycle count the error stays active. */
    int m_size{8};           /*!< New size (change-size tab). */

    // Editable field buffers (kept as their exact hex/dec text like the Qt line edits).
    std::string m_idText{"0x0"};         /*!< Object id (hex). */
    std::string m_startBitText{"0"};     /*!< Start bit (dec). */
    std::string m_bitSizeText{"1"};      /*!< Bit size (dec). */
    std::string m_bitValueText{"0"};     /*!< Bit value (dec). */
    std::string m_cyclesText{"0x7FFFFFFF"}; /*!< Cycles (hex). */
    std::string m_sizeText{"8"};         /*!< New size (dec). */

    std::string m_searchFilter{}; /*!< Tree search filter (regex). */
    int m_searchVariant{-1};      /*!< Search cursor variant index. */
    int m_searchObject{-1};       /*!< Search cursor object index. */
    int m_searchSignal{-1};       /*!< Search cursor signal index. */

    int m_selVariant{-1}; /*!< Selected tree variant. */
    int m_selObject{-1};  /*!< Selected tree object. */
    int m_selSignal{-1};  /*!< Selected tree signal (-1 == object row). */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_FRAMEWORK_CANERRORDIALOG_H
