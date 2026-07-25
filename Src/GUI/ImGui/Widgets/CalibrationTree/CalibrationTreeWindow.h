/**
 * @file Src/GUI/ImGui/Widgets/CalibrationTree/CalibrationTreeWindow.h
 * @brief ImGui port of the Qt calibration-tree widget: a browsable A2L/debug-info label tree.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_WIDGETS_CALIBRATIONTREE_CALIBRATIONTREEWINDOW_H
#define SRC_GUI_IMGUI_WIDGETS_CALIBRATIONTREE_CALIBRATIONTREEWINDOW_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Framework/IWindowInstance.h"
#include "Framework/IWindowType.h"
#include "Framework/WindowContext.h"

extern "C" {
#include "DebugInfos.h"
#include "SectionFilter.h"
#include "SharedDataTypes.h"
#include "Wildcards.h"
}

namespace XilEnvImGui
{

/**
 * @class CalibrationTreeWindowType
 * @brief Window type/factory for calibration-tree windows (mirrors the Qt CalibrationTreeType).
 * @details Keeps the exact Qt metadata so stored configs dispatch: type name
 *          "CalibrationTree", ini list "GUI/AllCalibrationTreeWindows". The Qt type is a
 *          FastUpdateWindow (its cyclic update executes the delayed value reads promptly),
 *          so the ImGui rate is Fast.
 */
class CalibrationTreeWindowType : public IWindowType
{
public:
    [[nodiscard]] UpdateRate GetUpdateRate() const override
    {
        return UpdateRate::Fast;
    }
    [[nodiscard]] const char *GetTypeName() const override
    {
        return "CalibrationTree";
    }
    [[nodiscard]] const char *GetIniListName() const override
    {
        return "GUI/AllCalibrationTreeWindows";
    }
    [[nodiscard]] const char *GetMenuEntryName() const override
    {
        return "Calibration tree";
    }
    [[nodiscard]] bool HasStorage() const override
    {
        return false;
    }
    [[nodiscard]] std::unique_ptr<IWindowInstance> CreateNew(const std::string &par_Title) override;
    [[nodiscard]] std::unique_ptr<IWindowInstance> CreateFromConfig(int par_FileDescriptor,
                                                                    const char *par_Section,
                                                                    const std::string &par_Name) override;
};

/**
 * @class CalibrationTreeWindow
 * @brief One calibration-tree window bound to exactly one external process.
 * @details Immediate-mode counterpart of the Qt CalibrationTreeWidget + CalibrationTreeModel +
 *          CalibrationTreeItem trio. Connects to a process' debug/A2L information, enumerates its
 *          calibration labels and lazily expands structures, arrays and pointers into a tree.
 *          Values of base-type leaves are read synchronously through the debug-info label read
 *          C-API under the scheduler process lock (batched delayed reads, exactly like the Qt
 *          model) and can be edited in place. Config round-trips to the Qt "GUI/Widgets/<name>"
 *          section.
 * @warning All methods run on the ImGui render thread only. Label value reads acquire the
 *          scheduler process lock (WaitUntilProcessIsNotActiveAndThanLockIt / UnLockProcess) so
 *          they never race the scheduler mid-cycle.
 */
class CalibrationTreeWindow : public IWindowInstance
{
public:
    /**
     * @brief Constructs a calibration-tree window bound to its type and name.
     * @param[in] par_Type Owning window type. Must not be nullptr.
     * @param[in] par_Name Unique window name.
     */
    CalibrationTreeWindow(IWindowType *par_Type, std::string par_Name);
    ~CalibrationTreeWindow() override;

    [[nodiscard]] IWindowType *GetType() const override
    {
        return m_pType;
    }
    void RenderContent(const WindowContext &par_Context) override;
    void OnUpdateTick(const WindowContext &par_Context) override;
    void RenderSettingsPopup() override;
    void WriteConfig(int par_FileDescriptor, const char *par_Section) override;
    void ReadConfig(int par_FileDescriptor, const char *par_Section) override;
    void OnAcceptVariableDrop(const std::string &par_VariableName) override;

private:
    /**
     * @class TreeNode
     * @brief One node of the calibration tree (counterpart of the Qt CalibrationTreeItem).
     * @details Nodes are expanded lazily: a struct/array/pointer node computes its child count
     *          without materialising children until it is first opened. Base-type leaves hold a
     *          cached value that is (re)read under the process lock.
     */
    class TreeNode
    {
    public:
        TreeNode(CalibrationTreeWindow *par_Owner, int par_What, std::int32_t par_TypeNr,
                 bool par_IsBasisClass, std::uint64_t par_Address, bool par_AddressIsResolved,
                 bool par_AddressIsInvalid, std::string par_Label, std::string par_DataType,
                 TreeNode *par_Parent);

        [[nodiscard]] const std::string &GetLabel() const
        {
            return m_label;
        }
        [[nodiscard]] const std::string &GetDataType() const
        {
            return m_dataType;
        }
        [[nodiscard]] int GetWhat() const
        {
            return m_what;
        }
        [[nodiscard]] std::int32_t GetTypeNr() const
        {
            return m_typeNr;
        }
        [[nodiscard]] std::uint64_t GetAddress() const
        {
            return m_address;
        }

        [[nodiscard]] int ChildCount();
        [[nodiscard]] std::vector<std::unique_ptr<TreeNode>> &GetChildren();
        [[nodiscard]] bool IsValidBaseElement() const;

        /**
         * @brief Returns the rendered value string; queues a delayed read when not yet cached.
         * @param[out] ret_NeedsRead Set true when the value must be read under the lock.
         * @return Value string, "invalid" for unreadable base types, or "" for non-base nodes.
         */
        [[nodiscard]] std::string GetValueString(bool &ret_NeedsRead);
        [[nodiscard]] std::string GetAddressString() const;
        [[nodiscard]] std::string GetCompleteName() const;

        /**
         * @brief Reads the value under an already-held process lock (mirrors DelayedGetLabelValueString).
         * @retval 0 Read succeeded, cache updated.
         * @retval -1 This element failed; caller marks its address invalid.
         */
        int DelayedRead();

        void WriteValue(const char *par_Text);
        void InvalidateCache();
        void SetAddressInvalid()
        {
            m_addressIsInvalid = true;
        }

        /**
         * @brief Returns the persistent in-place value-editor buffer.
         * @details The buffer survives across frames so a focus change can still commit the
         *          typed text (mirrors the Qt delegate committing on editor close). While the
         *          editor holds focus the cached value must not overwrite it.
         * @return Mutable pointer to the editor buffer.
         */
        [[nodiscard]] char *GetEditBuffer()
        {
            return m_editBuffer;
        }
        [[nodiscard]] int GetEditBufferSize() const
        {
            return static_cast<int>(sizeof(m_editBuffer));
        }
        [[nodiscard]] bool IsEditActive() const
        {
            return m_editActive;
        }
        void SetEditActive(bool par_Active)
        {
            m_editActive = par_Active;
        }

    private:
        void ExpandChilds();
        void ExpandRoot();
        void ExpandStructure();
        void ExpandArray();
        void ExpandPointer();
        [[nodiscard]] std::string GetCompleteNameWithSeparator() const;
        [[nodiscard]] std::uint64_t ResolveAddress(bool &ret_Ok);

        CalibrationTreeWindow *m_pOwner{nullptr}; /*!< Owning window (pid/debug-infos/filters). */
        int m_what{0};                            /*!< -1 root, 1 base, 2 struct, 3 array, 4 pointer. */
        std::int32_t m_typeNr{0};                 /*!< Debug-info type number. */
        std::uint64_t m_address{0};               /*!< Absolute address (or offset when unresolved). */
        bool m_addressIsResolved{true};           /*!< False while m_address is only an offset. */
        bool m_addressIsInvalid{false};           /*!< True when the address could not be read. */
        std::uint64_t m_pointsToAddress{0};       /*!< Resolved pointer target address. */
        bool m_pointsToAddressIsResolved{false};  /*!< True once the pointer target was read. */
        bool m_isBasisClass{false};               /*!< True for C++ base-class struct nodes. */
        union BB_VARI m_valueCache{};             /*!< Cached raw value. */
        enum BB_DATA_TYPES m_typeCache{BB_INVALID}; /*!< Cached value blackboard type. */
        bool m_valueCachedFlag{false};            /*!< True when m_valueCache is valid. */
        std::string m_label{};                    /*!< Node label (name). */
        std::string m_dataType{};                 /*!< Node data-type string. */
        std::vector<std::unique_ptr<TreeNode>> m_children{}; /*!< Lazily expanded children. */
        bool m_childsExpanded{false};             /*!< True once children were materialised. */
        TreeNode *m_pParent{nullptr};             /*!< Parent node (nullptr for the root). */
        char m_editBuffer[128]{};                 /*!< In-place value-editor buffer (kept across frames). */
        bool m_editActive{false};                 /*!< True while the value editor holds keyboard focus. */
    };

    // Accessors used by TreeNode (mirror the Qt CalibrationTreeModel getters).
    [[nodiscard]] int GetPid() const
    {
        return m_pid;
    }
    [[nodiscard]] PROCESS_APPL_DATA *GetDebugInfos() const
    {
        return m_pDebugInfos;
    }
    [[nodiscard]] SECTION_ADDR_RANGES_FILTER *GetGlobalSectionFilter() const
    {
        return m_pGlobalSectionFilter;
    }
    [[nodiscard]] INCLUDE_EXCLUDE_FILTER *GetIncludeExcludeFilter() const
    {
        return m_pIncExcFilter;
    }
    [[nodiscard]] int GetHexOrDec() const
    {
        return m_hexOrDec;
    }
    void QueueDelayedRead(TreeNode *par_Node);

    void SetProcessName(const std::string &par_ProcessName);
    void ConnectDebugInfos();
    void DisconnectDebugInfos();
    void ResetTree();
    void ExecuteDelayedReads();
    void RenderNode(TreeNode &par_Node, int par_ValueColumn, int par_AddressColumn, int par_TypeColumn,
                    int &io_RowId);

    /**
     * @brief Captures the live table column widths into the persisted width members.
     * @details Reads ImGuiTableColumn::WidthGiven keyed by the Qt field index carried in the
     *          column UserID, so WriteConfig round-trips the exact on-screen layout. Must be
     *          called while the calibration-tree table is the current ImGui table.
     */
    void CaptureColumnWidths();

    /**
     * @brief Rebuilds the in-memory include/exclude filter from the edited pattern lists.
     * @details Writes the current pattern vectors (and the main filter) into a scratch in-memory
     *          ini and feeds them through BuildIncludeExcludeFilterFromIni, so the label-only
     *          filter arrays are produced by the core exactly as on load. Swaps in the new filter
     *          and resets the tree. A no-op if the scratch ini cannot be created.
     */
    void RebuildIncludeExcludeFilter();

    /**
     * @brief Refreshes the editable pattern lists and buffers from the active filter model.
     */
    void SyncFilterListsFromModel();

    IWindowType *m_pType{nullptr};       /*!< Owning window type. */

    std::string m_processName{};         /*!< Bound external process name. */
    int m_uniqueNumber{-1};              /*!< Debug-info connection id (AquireUniqueNumber). */
    int m_pid{-1};                       /*!< Current process id (-1 when not running). */
    PROCESS_APPL_DATA *m_pDebugInfos{nullptr}; /*!< Debug-info connection handle. */
    SECTION_ADDR_RANGES_FILTER *m_pGlobalSectionFilter{nullptr}; /*!< Section address filter. */
    INCLUDE_EXCLUDE_FILTER *m_pIncExcFilter{nullptr}; /*!< Include/exclude label filter. */

    std::unique_ptr<TreeNode> m_root{}; /*!< Synthetic root node holding the label list. */
    std::vector<TreeNode *> m_delayedReads{}; /*!< Nodes queued for a batched locked read. */

    std::string m_filter{"*"};   /*!< Main filter string (round-trip; kept for config parity). */
    int m_constOnly{0};          /*!< "const only" flag (round-trip; kept for config parity). */
    int m_showValues{1};         /*!< Show the value column. */
    int m_showAddress{0};        /*!< Show the address column. */
    int m_showType{0};           /*!< Show the type column. */
    int m_hexOrDec{0};           /*!< 0 -> decimal, 1 -> hexadecimal value formatting. */

    int m_columnWidthLabel{-1};   /*!< Persisted label column width (round-trip only). */
    int m_columnWidthValue{-1};   /*!< Persisted value column width (round-trip only). */
    int m_columnWidthAddress{-1}; /*!< Persisted address column width (round-trip only). */
    int m_columnWidthType{-1};    /*!< Persisted type column width (round-trip only). */

    char m_processBuffer[256]{};  /*!< Settings text buffer for the process name. */

    std::vector<std::string> m_includePatterns{}; /*!< Editable include-filter patterns. */
    std::vector<std::string> m_excludePatterns{}; /*!< Editable exclude-filter patterns. */
    char m_filterBuffer[256]{};        /*!< Main-filter editor buffer. */
    char m_includeInputBuffer[256]{};  /*!< New include-pattern entry buffer. */
    char m_excludeInputBuffer[256]{};  /*!< New exclude-pattern entry buffer. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_WIDGETS_CALIBRATIONTREE_CALIBRATIONTREEWINDOW_H
