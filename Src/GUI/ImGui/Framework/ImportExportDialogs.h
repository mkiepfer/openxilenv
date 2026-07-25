/**
 * @file Src/GUI/ImGui/Framework/ImportExportDialogs.h
 * @brief Import/export dialogs (sheets, variable properties, reference list, SVL, values,
 *        control lamps, binary compare) ported from the Qt Dialog/ set.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */

#ifndef SRC_GUI_IMGUI_FRAMEWORK_IMPORTEXPORTDIALOGS_H
#define SRC_GUI_IMGUI_FRAMEWORK_IMPORTEXPORTDIALOGS_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace XilEnvImGui
{

/**
 * @class ImportExportDialogManager
 * @brief Owns and renders every File/Import-Export dialog for the ImGui front-end.
 * @details Process-wide singleton mirroring the Qt Dialog/ import-export set. Each dialog is a modal
 *          popup opened from the "File / Import/Export" menu and driven through a shared inline file
 *          picker (std::filesystem, no new dependency). The dialogs call the exact same C core
 *          im/export entry points as the Qt originals so the produced files round-trip byte-identically:
 *          sheet/widget ini-section copies through @ref IniFileDataBaseCopySection, variable properties
 *          through @ref ImportOneVariablePropertiesFlags, reference lists through
 *          @ref ExportVariableReferenceList / @ref ImportVariableReferenceList, SVL through
 *          @ref WriteSVLToProcess and control-lamp stencils through the
 *          "GUI/AllUserDefinedStancilForControlLamps" section. All methods run on the render thread.
 * @warning Render-thread-only; not thread-safe.
 */
class ImportExportDialogManager
{
public:
    /**
     * @brief Returns the process-wide singleton instance.
     * @return Reference to the single ImportExportDialogManager.
     */
    static ImportExportDialogManager &GetInstance();

    /** @brief Opens the "export sheets" dialog (writes selected sheets to a file). */
    void OpenExportSheets();
    /** @brief Opens the "import sheets" dialog (reads sheets from a file). */
    void OpenImportSheets();
    /** @brief Opens the "export variable properties" dialog. */
    void OpenExportVariableProperties();
    /** @brief Opens the "import variable properties" dialog. */
    void OpenImportVariableProperties();
    /** @brief Opens the "export reference list" dialog. */
    void OpenExportReferenceList();
    /** @brief Opens the "import reference list" dialog. */
    void OpenImportReferenceList();
    /** @brief Opens the "load SVL file into process" dialog. */
    void OpenLoadSvl();
    /** @brief Opens the "load values (symbol/binary)" dialog. */
    void OpenLoadValues();
    /** @brief Opens the "import/export control lamps" dialog. */
    void OpenControlLamps();
    /** @brief Opens the "binary compare" dialog. */
    void OpenBinaryCompare();

    /**
     * @brief Renders whichever import/export dialog (and the shared file picker) is active this frame.
     */
    void Render();

private:
    ImportExportDialogManager() = default;

    /**
     * @enum class Dialog
     * @brief Identifies which import/export modal is currently active.
     */
    enum class Dialog : std::uint8_t
    {
        None,                   /*!< No dialog active. */
        ExportSheets,           /*!< Export selected sheets to a file. */
        ImportSheets,           /*!< Import sheets from a file. */
        ExportVariableProps,    /*!< Export blackboard variable properties. */
        ImportVariableProps,    /*!< Import blackboard variable properties. */
        ExportReferenceList,    /*!< Export a process reference list. */
        ImportReferenceList,    /*!< Import a process reference list. */
        LoadSvl,                /*!< Load an SVL file into a process. */
        LoadValues,             /*!< Load values from symbol/binary (UI shell only). */
        ControlLamps,           /*!< Import/export control-lamp stencils. */
        BinaryCompare           /*!< Compare two binary files. */
    };

    /**
     * @struct LampStencil
     * @brief One control-lamp stencil (name plus its polygon definition string).
     */
    struct LampStencil
    {
        std::string name{};    /*!< Stencil name (unique per list). */
        std::string polygon{}; /*!< Polygon definition ("(x y x y ...)..." blob). */
    };

    // -- shared modal helpers ----------------------------------------------
    void OpenModal(Dialog par_Dialog);
    bool BeginModal(const char *par_TitleId, bool par_LargePopup);

    // -- shared inline file picker -----------------------------------------
    void OpenFilePicker(const char *par_Title, const char *par_Extension, bool par_SaveMode,
                        std::string *par_pTarget);
    void RenderFilePicker();

    // -- per-dialog helpers -------------------------------------------------
    void RefreshProcessList();
    void RefreshSheetList(std::vector<std::string> &out_Names, int par_FileDescriptor);
    void RefreshVariableList(std::vector<std::string> &out_Names, int par_FileDescriptor,
                             bool par_ExistingOnly, const char *par_Filter);
    void LoadLampStencils(std::vector<LampStencil> &out_Stencils, int par_FileDescriptor);
    void SaveLampStencils(const std::vector<LampStencil> &par_Stencils, int par_FileDescriptor);
    std::string MakeUniqueLampName(const std::vector<LampStencil> &par_Stencils,
                                   const std::string &par_Name) const;

    void RenderExportSheets();
    void RenderImportSheets();
    void RenderExportVariableProperties();
    void RenderImportVariableProperties();
    void RenderExportReferenceList();
    void RenderImportReferenceList();
    void RenderLoadSvl();
    void RenderLoadValues();
    void RenderControlLamps();
    void RenderBinaryCompare();

    void CopyOneSheet(int par_SrcFd, const char *par_SrcSection, int par_DstFd,
                      const char *par_DstSection, const char *par_DstSheetName);

    // -- active-dialog state ------------------------------------------------
    Dialog m_active{Dialog::None};       /*!< Currently open modal, if any. */
    bool m_openRequested{false};         /*!< OpenPopup pending for m_active. */

    // -- shared file picker state ------------------------------------------
    bool m_pickerOpen{false};            /*!< File picker modal is shown. */
    bool m_pickerOpenRequested{false};   /*!< OpenPopup pending for the picker. */
    bool m_pickerSaveMode{false};        /*!< true -> "Save" confirm button, false -> "Open". */
    std::string m_pickerTitle{};         /*!< File picker heading. */
    std::string m_pickerExt{};           /*!< Filter extension (".ini"), empty = all. */
    std::string m_pickerDir{};           /*!< Current picker directory. */
    std::array<char, 512> m_pickerName{}; /*!< Selected/typed file name. */
    std::string *m_pPickerTarget{nullptr}; /*!< Destination path buffer for the chosen file. */

    // -- process list (shared by ref-list/SVL/values) -----------------------
    std::vector<std::string> m_processes{}; /*!< External process names. */
    int m_selectedProcess{-1};              /*!< Index into m_processes. */

    // -- sheets -------------------------------------------------------------
    std::vector<std::string> m_sheetAvailable{}; /*!< Sheets not yet chosen. */
    std::vector<std::string> m_sheetSelected{};  /*!< Sheets chosen for im/export. */
    int m_sheetAvailableSel{-1};                 /*!< Selection in available list. */
    int m_sheetSelectedSel{-1};                  /*!< Selection in chosen list. */
    std::string m_sheetFile{};                   /*!< Source/destination file path. */

    // -- variable properties -----------------------------------------------
    std::vector<std::string> m_varAvailable{}; /*!< Variables from source. */
    std::vector<std::string> m_varSelected{};  /*!< Variables chosen. */
    int m_varAvailableSel{-1};                 /*!< Selection in available list. */
    int m_varSelectedSel{-1};                  /*!< Selection in chosen list. */
    std::array<char, 512> m_varFilter{};       /*!< Wildcard filter. */
    bool m_varExistingOnly{true};              /*!< Only variables attached to a process. */
    std::string m_varFile{};                   /*!< Import source / export destination file. */
    bool m_impUnit{true};                      /*!< Import unit property. */
    bool m_impValue{true};                     /*!< Import min/max property. */
    bool m_impConversion{true};                /*!< Import conversion property. */
    bool m_impDisplay{true};                   /*!< Import display property. */
    bool m_impStep{true};                      /*!< Import step-type property. */
    bool m_impColor{true};                     /*!< Import color property. */

    // -- reference list -----------------------------------------------------
    std::string m_refFile{};                   /*!< SCRefL file path. */
    int m_refFormat{1};                        /*!< 0 = old (indexed), 1 = new (not indexed). */

    // -- SVL ----------------------------------------------------------------
    std::string m_svlFile{};                   /*!< SVL file path. */

    // -- load values (UI shell) --------------------------------------------
    std::string m_lvSymbolFile{};              /*!< Symbol file path. */
    std::string m_lvBinaryFile{};              /*!< Binary file path. */
    std::string m_lvSvlFile{};                 /*!< SVL destination path. */
    std::array<char, 64> m_lvStartAddr{};      /*!< Start address (hex text). */
    std::array<char, 64> m_lvBlockSize{};      /*!< Block size (hex text). */
    std::array<char, 64> m_lvGpLabel{};        /*!< GP-label address (hex text). */
    int m_lvEndian{0};                         /*!< 0 = LSB first, 1 = MSB first. */
    int m_lvTarget{0};                         /*!< 0 = process, 1 = SVL file. */

    // -- control lamps ------------------------------------------------------
    std::vector<LampStencil> m_lampInternal{}; /*!< Stencils in the running config. */
    std::vector<LampStencil> m_lampExternal{}; /*!< Stencils in the opened file. */
    int m_lampInternalSel{-1};                 /*!< Selection in internal list. */
    int m_lampExternalSel{-1};                 /*!< Selection in external list. */
    std::string m_lampFile{};                  /*!< External file path. */
    int m_lampFd{-1};                          /*!< Open descriptor of the external file. */
    bool m_lampInternalDirty{false};           /*!< Internal list changed. */
    bool m_lampExternalDirty{false};           /*!< External list changed. */

    // -- binary compare -----------------------------------------------------
    std::string m_binFile1{};                  /*!< First file path. */
    std::string m_binFile2{};                  /*!< Second file path. */
    std::vector<std::uint8_t> m_binImage1{};   /*!< First file bytes. */
    std::vector<std::uint8_t> m_binImage2{};   /*!< Second file bytes. */
};

} // namespace XilEnvImGui

#endif // SRC_GUI_IMGUI_FRAMEWORK_IMPORTEXPORTDIALOGS_H
