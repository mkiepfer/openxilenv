/**
 * @file Src/GUI/ImGui/Framework/BlackboardDialogs.h
 * @brief Blackboard/variable dialogs (info, per-process access, internal view, filters, value ops).
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_FRAMEWORK_BLACKBOARDDIALOGS_H
#define SRC_GUI_IMGUI_FRAMEWORK_BLACKBOARDDIALOGS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace XilEnvImGui
{

/**
 * @brief Callback invoked when the batch value-operation dialog is confirmed.
 * @param par_Op ChangeValueOP operation character ('I','+','-','*','/','O', ...).
 * @param par_Value Entered value (raw or physical depending on the dialog set-up).
 */
using ChangeValueCallback = std::function<void(int par_Op, double par_Value)>;

/**
 * @brief Callback invoked when the add-filter prompt is confirmed with a non-empty name.
 * @param par_FilterName The entered filter wildcard string.
 */
using AddFilterCallback = std::function<void(const std::string &par_FilterName)>;

/**
 * @class BlackboardDialogManager
 * @brief Owns and renders the blackboard-variable dialogs reachable from the menu and from widgets.
 * @details Process-wide singleton mirroring the Qt Dialog/ classes:
 *          - BlackboardInfoDialog: variable inspector/editor (value, unit, min/max, format, color,
 *            conversion type/string). Conversion editing is fully delegated to
 *            @ref ConversionDialogManager (the "Edit conversion..." button), and the displayed
 *            conversion is refreshed from the C core each frame so delegated edits round-trip.
 *          - BlackboardAccessDialog: per-process read/write access mask (enable/disable lists) via
 *            @ref ReadNextVariableProcessAccess / @ref enable_bbvari_access / @ref disable_bbvari_access.
 *          - BlackboardInternalDialog: read-only internal blackboard state inspector.
 *          - GlobalSectionFilterDialog: the global ini-section include/exclude filter editor,
 *            persisted through @ref SaveIncludeExcludeListsToIni.
 *          - AddFilterDialog: a small include/exclude filter-name prompt (helper).
 *          - ChangeValueTextDialog: a batch value operation prompt reproducing the ChangeValueOP set;
 *            the caller passes an op and receives the entered value through a @ref ChangeValueCallback.
 *          All methods run on the render thread.
 * @warning Render-thread-only; not thread-safe.
 */
class BlackboardDialogManager
{
public:
    /**
     * @brief Returns the process-wide singleton instance.
     * @return Reference to the single BlackboardDialogManager.
     */
    static BlackboardDialogManager &GetInstance();

    /**
     * @brief Opens the variable inspector/editor, pre-selecting an optional variable.
     * @param[in] par_VariableName Variable to pre-select, or nullptr/"" for the first match.
     */
    void OpenBlackboardInfo(const char *par_VariableName);

    /**
     * @brief Opens the per-process blackboard access-mask editor.
     */
    void OpenBlackboardAccess();

    /**
     * @brief Opens the read-only internal blackboard state inspector.
     */
    void OpenBlackboardInternal();

    /**
     * @brief Opens the global ini-section include/exclude filter editor.
     */
    void OpenGlobalSectionFilter();

    /**
     * @brief Opens the small add-filter prompt (helper used by other dialogs/widgets).
     * @param[in] par_IncludeExclude false = include filter, true = exclude filter (title only).
     * @param[in] par_Callback Invoked with the entered name on confirm. Must be callable.
     */
    void OpenAddFilter(bool par_IncludeExclude, AddFilterCallback par_Callback);

    /**
     * @brief Opens the batch value-operation prompt for a ChangeValueOP operation.
     * @details Reproduces the ChangeValueTextDialog used by the A2LCal/CalibrationTree "change all
     *          selected values" actions. The dialog shows @p par_Title, lets the user enter one value
     *          and, on confirm, invokes @p par_Callback with (@p par_Op, value) so the caller can run
     *          its own ChangeValueOP over the current selection.
     * @param[in] par_Op ChangeValueOP operation character (e.g. 'I','+','-','*','/','O').
     * @param[in] par_Title Header shown above the value field. May be nullptr.
     * @param[in] par_DataType BB_DATA_TYPES of the target (integer types force raw-integer entry).
     * @param[in] par_Precision Display precision for the value field.
     * @param[in] par_InitialValue Value pre-filled into the field.
     * @param[in] par_Callback Invoked with (op, value) on confirm. Must be callable.
     */
    void OpenChangeValue(int par_Op, const char *par_Title, int par_DataType, int par_Precision,
                         double par_InitialValue, ChangeValueCallback par_Callback);

    /**
     * @brief Renders every managed dialog for this frame.
     */
    void Render();

private:
    BlackboardDialogManager() = default;

    /**
     * @struct InfoState
     * @brief Edit buffers backing the BlackboardInfo variable editor.
     */
    struct InfoState
    {
        std::string name{};                     /*!< Selected variable name. */
        std::int32_t vid{0};                    /*!< Variable id (read-only display). */
        int dataType{0};                        /*!< BB_DATA_TYPES (read-only display). */
        int attachCount{0};                     /*!< Attach counter (read-only display). */
        double value{0.0};                      /*!< Physical value. */
        std::array<char, 64> unit{};            /*!< Unit text. */
        double minValue{0.0};                   /*!< Display minimum. */
        double maxValue{0.0};                   /*!< Display maximum. */
        int width{0};                           /*!< Display total width. */
        int precision{0};                       /*!< Display decimal places. */
        bool colorValid{false};                 /*!< Color is set. */
        std::array<float, 3> color{};           /*!< RGB color (0..1) mirror for the picker. */
        int conversionType{0};                  /*!< BB_CONV_* type (read-only display). */
        std::string conversionString{};         /*!< Current conversion string (read-only display). */
    };

    /**
     * @struct AccessProcess
     * @brief Enable/disable variable lists for one process in the access-mask editor.
     */
    struct AccessProcess
    {
        std::int32_t pid{0};                    /*!< Process id. */
        std::vector<std::string> enable{};      /*!< Currently enabled (write-access) variables. */
        std::vector<std::string> disable{};     /*!< Currently disabled variables. */
        std::vector<std::string> enableSave{};  /*!< Enable snapshot at load (for diffing on apply). */
        std::vector<std::string> disableSave{}; /*!< Disable snapshot at load. */
        bool loaded{false};                     /*!< Lists have been read from the core. */
    };

    // -- BlackboardInfo -----------------------------------------------------
    void LoadInfoVariable(const std::string &par_Name);
    void ApplyInfoVariable();
    void RefreshInfoConversion();
    void RenderBlackboardInfo();

    // -- BlackboardAccess ---------------------------------------------------
    void LoadAccessProcess(const std::string &par_ProcessName);
    void ApplyAccess();
    void RenderBlackboardAccess();

    // -- BlackboardInternal -------------------------------------------------
    void RenderBlackboardInternal();

    // -- GlobalSectionFilter ------------------------------------------------
    void LoadGlobalSectionFilter();
    void AddSectionsForProcess(const std::string &par_ProcessName, int par_UniqueId);
    void ApplyGlobalSectionFilter();
    void RenderGlobalSectionFilter();

    // -- AddFilter / ChangeValue -------------------------------------------
    void RenderAddFilter();
    void RenderChangeValue();

    // -- BlackboardInfo state ----------------------------------------------
    InfoState m_info{};                          /*!< Live edit state for the selected variable. */
    InfoState m_infoSave{};                      /*!< Snapshot to diff against on apply. */
    std::array<char, 256> m_infoFilter{};        /*!< Variable-list filter text. */
    std::string m_pendingInfoSelect{};           /*!< Variable to select once the list is shown. */
    bool m_infoOpen{false};                      /*!< BlackboardInfo modal is shown. */
    bool m_infoOpenRequested{false};             /*!< OpenPopup pending for BlackboardInfo. */

    // -- BlackboardAccess state --------------------------------------------
    std::vector<std::string> m_accessProcessNames{}; /*!< Process names shown in the list. */
    std::string m_accessSelected{};              /*!< Currently selected process. */
    std::vector<AccessProcess> m_accessData{};   /*!< Per-process enable/disable state. */
    std::array<char, 256> m_accessEnableFilter{};/*!< Enable-list wildcard filter. */
    std::array<char, 256> m_accessDisableFilter{};/*!< Disable-list wildcard filter. */
    std::vector<int> m_accessEnableSel{};        /*!< Selected rows in the enable list. */
    std::vector<int> m_accessDisableSel{};       /*!< Selected rows in the disable list. */
    bool m_accessOpen{false};                    /*!< BlackboardAccess modal is shown. */
    bool m_accessOpenRequested{false};           /*!< OpenPopup pending for BlackboardAccess. */

    // -- BlackboardInternal state ------------------------------------------
    std::array<char, 256> m_internalFilter{};    /*!< Name wildcard filter. */
    bool m_internalOnlyUsed{false};              /*!< Hide BB_UNKNOWN_WAIT (unused) variables. */
    std::string m_internalSelected{};            /*!< Selected variable name. */
    bool m_internalOpen{false};                  /*!< BlackboardInternal window is shown. */

    // -- GlobalSectionFilter state -----------------------------------------
    std::vector<std::string> m_gsfProcesses{};   /*!< Process names. */
    std::vector<std::string> m_gsfAvailable{};   /*!< Available section names. */
    std::vector<std::string> m_gsfInclude{};     /*!< Include list. */
    std::vector<std::string> m_gsfExclude{};     /*!< Exclude list. */
    int m_gsfAvailableSel{-1};                   /*!< Selected available section. */
    int m_gsfIncludeSel{-1};                     /*!< Selected include entry. */
    int m_gsfExcludeSel{-1};                     /*!< Selected exclude entry. */
    bool m_gsfOpen{false};                       /*!< GlobalSectionFilter modal is shown. */
    bool m_gsfOpenRequested{false};              /*!< OpenPopup pending for GlobalSectionFilter. */

    // -- AddFilter state ----------------------------------------------------
    std::array<char, 256> m_addFilterName{};     /*!< Entered filter name. */
    bool m_addFilterExclude{false};              /*!< Title flavour (include/exclude). */
    AddFilterCallback m_addFilterCallback{};     /*!< Confirm callback. */
    bool m_addFilterOpen{false};                 /*!< AddFilter modal is shown. */
    bool m_addFilterOpenRequested{false};        /*!< OpenPopup pending for AddFilter. */

    // -- ChangeValue state --------------------------------------------------
    int m_changeValueOp{'I'};                    /*!< ChangeValueOP operation character. */
    std::string m_changeValueTitle{};            /*!< Header text. */
    double m_changeValue{0.0};                   /*!< Entered value. */
    int m_changeValuePrecision{6};               /*!< Field precision. */
    bool m_changeValueRawInt{false};             /*!< Integer-only entry for integer target types. */
    ChangeValueCallback m_changeValueCallback{}; /*!< Confirm callback. */
    bool m_changeValueOpen{false};               /*!< ChangeValue modal is shown. */
    bool m_changeValueOpenRequested{false};      /*!< OpenPopup pending for ChangeValue. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_FRAMEWORK_BLACKBOARDDIALOGS_H
