#include "Framework/ImportExportDialogs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "imgui.h"

#include "Framework/DialogManager.h"
#include "Framework/WindowManager.h"

extern "C" {
#include "Blackboard.h"
#include "ExtProcessReferences.h"
#include "ImExportVarProperties.h"
#include "IniDataBase.h"
#include "LoadSaveToFile.h"
#include "MainValues.h"
#include "Scheduler.h"
#include "Wildcards.h"
#include "WindowIniHelper.h"
}

namespace XilEnvImGui
{

namespace
{

constexpr const char *LAMP_STENCIL_SECTION{"GUI/AllUserDefinedStancilForControlLamps"};
constexpr float LIST_HEIGHT{200.0f};       /*!< Default list-box height. */
constexpr int MAX_SHEET_SCAN{10000};       /*!< Upper bound for the sheet-index scan. */

/**
 * @brief Reads an ini string into a std::string (empty on miss).
 */
std::string ReadIniString(const char *par_Section, const char *par_Entry, const char *par_Default,
                          int par_Fd)
{
    char buffer[INI_MAX_LINE_LENGTH];
    if (0 >= IniFileDataBaseReadString(par_Section, par_Entry, par_Default, buffer, sizeof(buffer),
                                       par_Fd))
    {
        return std::string{(nullptr != par_Default) ? par_Default : ""};
    }
    return std::string{buffer};
}

/**
 * @brief Draws a single-selection list box; returns true when an item is double-clicked.
 */
bool DrawSelectableList(const char *par_Id, const std::vector<std::string> &par_Items, int &par_Sel,
                        float par_Height)
{
    bool doubleClicked{false};
    ImGui::BeginChild(par_Id, ImVec2(0.0f, par_Height), true);
    for (int i{0}; i < static_cast<int>(par_Items.size()); ++i)
    {
        const bool selected{i == par_Sel};
        if (ImGui::Selectable(par_Items[static_cast<size_t>(i)].c_str(), selected,
                              ImGuiSelectableFlags_AllowDoubleClick))
        {
            par_Sel = i;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                doubleClicked = true;
            }
        }
    }
    ImGui::EndChild();
    return doubleClicked;
}

/**
 * @brief Moves the currently selected entry from one list to another (deduplicated).
 */
void MoveSelected(std::vector<std::string> &par_From, int &par_FromSel,
                  std::vector<std::string> &par_To)
{
    if ((0 > par_FromSel) || (par_FromSel >= static_cast<int>(par_From.size())))
    {
        return;
    }
    const std::string value{par_From[static_cast<size_t>(par_FromSel)]};
    if (par_To.end() == std::find(par_To.begin(), par_To.end(), value))
    {
        par_To.push_back(value);
    }
    par_From.erase(par_From.begin() + par_FromSel);
    par_FromSel = -1;
}

} // namespace

ImportExportDialogManager &ImportExportDialogManager::GetInstance()
{
    static ImportExportDialogManager instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Shared modal + file-picker plumbing
// ---------------------------------------------------------------------------

void ImportExportDialogManager::OpenModal(Dialog par_Dialog)
{
    m_active = par_Dialog;
    m_openRequested = true;
}

bool ImportExportDialogManager::BeginModal(const char *par_TitleId, bool par_LargePopup)
{
    if (m_openRequested)
    {
        ImGui::OpenPopup(par_TitleId);
        m_openRequested = false;
    }
    const ImGuiViewport *viewport{ImGui::GetMainViewport()};
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    const ImVec2 size{par_LargePopup ? ImVec2(900.0f, 640.0f) : ImVec2(620.0f, 460.0f)};
    ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
    bool open{true};
    if (ImGui::BeginPopupModal(par_TitleId, &open, ImGuiWindowFlags_NoSavedSettings))
    {
        return true;
    }
    if (!open)
    {
        m_active = Dialog::None;
    }
    return false;
}

void ImportExportDialogManager::OpenFilePicker(const char *par_Title, const char *par_Extension,
                                               bool par_SaveMode, std::string *par_pTarget)
{
    m_pickerTitle = par_Title;
    m_pickerExt = (nullptr != par_Extension) ? par_Extension : "";
    m_pickerSaveMode = par_SaveMode;
    m_pPickerTarget = par_pTarget;
    m_pickerName = {};
    if ((nullptr != par_pTarget) && !par_pTarget->empty())
    {
        namespace fs = std::filesystem;
        const fs::path current{*par_pTarget};
        std::error_code ec;
        if (current.has_parent_path() && fs::exists(current.parent_path(), ec))
        {
            m_pickerDir = current.parent_path().string();
        }
        std::snprintf(m_pickerName.data(), m_pickerName.size(), "%s",
                      current.filename().string().c_str());
    }
    if (m_pickerDir.empty())
    {
        std::error_code ec;
        m_pickerDir = std::filesystem::current_path(ec).string();
    }
    m_pickerOpenRequested = true;
}

void ImportExportDialogManager::RenderFilePicker()
{
    namespace fs = std::filesystem;

    if (m_pickerOpenRequested)
    {
        ImGui::OpenPopup("Select file###ImExFilePicker");
        m_pickerOpenRequested = false;
    }

    const ImGuiViewport *viewport{ImGui::GetMainViewport()};
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560.0f, 480.0f), ImGuiCond_Appearing);

    bool open{true};
    if (!ImGui::BeginPopupModal("Select file###ImExFilePicker", &open,
                                ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }

    ImGui::TextUnformatted(m_pickerTitle.c_str());
    ImGui::TextDisabled("%s", m_pickerDir.c_str());
    if (ImGui::Button(".. (up)"))
    {
        const fs::path parent{fs::path{m_pickerDir}.parent_path()};
        if (!parent.empty())
        {
            m_pickerDir = parent.string();
        }
    }

    std::error_code ec;
    std::vector<std::string> dirs;
    std::vector<std::string> files;
    for (fs::directory_iterator it{m_pickerDir, fs::directory_options::skip_permission_denied, ec};
         !ec && (it != fs::directory_iterator{}); it.increment(ec))
    {
        std::error_code typeEc;
        if (it->is_directory(typeEc))
        {
            dirs.push_back(it->path().filename().string());
        }
        else if (it->is_regular_file(typeEc))
        {
            const std::string name{it->path().filename().string()};
            const std::string ext{it->path().extension().string()};
            if (m_pickerExt.empty() || (0 == ext.compare(m_pickerExt)))
            {
                files.push_back(name);
            }
        }
    }
    std::sort(dirs.begin(), dirs.end());
    std::sort(files.begin(), files.end());

    ImGui::BeginChild("##imexfilelist", ImVec2(0.0f, 300.0f), true);
    for (const std::string &name : dirs)
    {
        const std::string label{"[DIR] " + name};
        if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            m_pickerDir = (fs::path{m_pickerDir} / name).string();
        }
    }
    for (const std::string &name : files)
    {
        if (ImGui::Selectable(name.c_str()))
        {
            std::snprintf(m_pickerName.data(), m_pickerName.size(), "%s", name.c_str());
        }
    }
    ImGui::EndChild();

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##imexfilename", m_pickerName.data(), m_pickerName.size());

    const char *confirmLabel{m_pickerSaveMode ? "Save" : "Open"};
    if (ImGui::Button(confirmLabel, ImVec2(100.0f, 0.0f)) && ('\0' != m_pickerName[0]))
    {
        const fs::path chosen{fs::path{m_pickerDir} / m_pickerName.data()};
        if (nullptr != m_pPickerTarget)
        {
            *m_pPickerTarget = chosen.string();
        }
        m_pPickerTarget = nullptr;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
    {
        m_pPickerTarget = nullptr;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Data refresh helpers
// ---------------------------------------------------------------------------

void ImportExportDialogManager::RefreshProcessList()
{
    m_processes.clear();
    m_selectedProcess = -1;
    READ_NEXT_PROCESS_NAME *buffer{init_read_next_process_name(2 | 0x100)};
    char *name{nullptr};
    while (nullptr != (name = read_next_process_name(buffer)))
    {
        if (!IsInternalProcess(name))
        {
            m_processes.emplace_back(name);
        }
    }
    close_read_next_process_name(buffer);
    if (1 == m_processes.size())
    {
        m_selectedProcess = 0;
    }
}

void ImportExportDialogManager::RefreshSheetList(std::vector<std::string> &out_Names,
                                                 int par_FileDescriptor)
{
    out_Names.clear();
    for (int i{0}; i < MAX_SHEET_SCAN; ++i)
    {
        char section[INI_MAX_SECTION_LENGTH];
        std::snprintf(section, sizeof(section), "GUI/OpenWindowsForSheet%d", i);
        const std::string name{ReadIniString(section, "SheetName", "", par_FileDescriptor)};
        if (name.empty())
        {
            if (0 == i)
            {
                // Index 0 defaults to "Default" like the Qt originals.
                if (0 != IniFileDataBaseLookIfSectionExist(section, par_FileDescriptor))
                {
                    out_Names.emplace_back("Default");
                }
                continue;
            }
            break;
        }
        out_Names.push_back(name);
    }
}

void ImportExportDialogManager::RefreshVariableList(std::vector<std::string> &out_Names,
                                                    int par_FileDescriptor, bool par_ExistingOnly,
                                                    const char *par_Filter)
{
    out_Names.clear();
    char variName[INI_MAX_ENTRYNAME_LENGTH];
    int entryIdx{0};
    while (0 <= (entryIdx = IniFileDataBaseGetNextEntryName(entryIdx, "Variables", variName,
                                                            sizeof(variName), par_FileDescriptor)))
    {
        if (0 != Compare2StringsWithWildcards(variName, par_Filter))
        {
            continue;
        }
        if (par_ExistingOnly && (0 >= get_bbvari_attachcount(get_bbvarivid_by_name(variName))))
        {
            continue;
        }
        out_Names.emplace_back(variName);
    }
}

// ---------------------------------------------------------------------------
// Sheet copy (mirrors Qt CopyOneSheet / AddWindowToCorrespondAllList)
// ---------------------------------------------------------------------------

namespace
{

/**
 * @brief Registers a window in the "GUI/All<Type>Windows" list of the destination ini.
 */
void AddWindowToCorrespondAllList(const char *par_WidgetName, const char *par_SectionName,
                                  int par_DstFd)
{
    char typeName[INI_MAX_LINE_LENGTH];
    if (0 >= IniFileDataBaseReadString(par_SectionName, "type", "", typeName, sizeof(typeName),
                                       par_DstFd))
    {
        return;
    }
    char *allListSection{nullptr};
    if (0 != GetSection_AllWindowsOfType(typeName, &allListSection))
    {
        return;
    }
    for (int x{0}; x < MAX_SHEET_SCAN; ++x)
    {
        char entry[INI_MAX_ENTRYNAME_LENGTH];
        std::snprintf(entry, sizeof(entry), "W%d", x);
        char line[INI_MAX_LINE_LENGTH];
        if (0 == IniFileDataBaseReadString(allListSection, entry, "", line, sizeof(line), par_DstFd))
        {
            (void)IniFileDataBaseWriteString(allListSection, entry, par_WidgetName, par_DstFd);
            break;
        }
    }
}

} // namespace

void ImportExportDialogManager::CopyOneSheet(int par_SrcFd, const char *par_SrcSection, int par_DstFd,
                                             const char *par_DstSection, const char *par_DstSheetName)
{
    (void)IniFileDataBaseCopySection(par_DstFd, par_SrcFd, par_DstSection, par_SrcSection);
    (void)IniFileDataBaseWriteString(par_DstSection, "SheetName", par_DstSheetName, par_DstFd);

    for (int x{0}; x < MAX_SHEET_SCAN; ++x)
    {
        char entry[INI_MAX_ENTRYNAME_LENGTH];
        std::snprintf(entry, sizeof(entry), "W%d", x);
        const std::string srcWindowName{ReadIniString(par_SrcSection, entry, "", par_SrcFd)};
        if (srcWindowName.empty())
        {
            break;
        }
        const std::string srcWindowSection{std::string{"GUI/Widgets/"} + srcWindowName};
        if (0 == IniFileDataBaseLookIfSectionExist(srcWindowSection.c_str(), par_SrcFd))
        {
            continue;
        }

        std::string dstWindowName{srcWindowName};
        std::string dstWindowSection{srcWindowSection};
        if (0 != IniFileDataBaseLookIfSectionExist(dstWindowSection.c_str(), par_DstFd))
        {
            // Name clash in the destination: append " (N)" until free.
            for (int i{1}; i < MAX_SHEET_SCAN; ++i)
            {
                char suffix[32];
                std::snprintf(suffix, sizeof(suffix), " (%d)", i);
                dstWindowName = srcWindowName + suffix;
                dstWindowSection = std::string{"GUI/Widgets/"} + dstWindowName;
                if (0 == IniFileDataBaseLookIfSectionExist(dstWindowSection.c_str(), par_DstFd))
                {
                    break;
                }
            }
            (void)IniFileDataBaseCopySection(par_DstFd, par_SrcFd, dstWindowSection.c_str(),
                                             srcWindowSection.c_str());
        }
        else
        {
            (void)IniFileDataBaseCopySectionSameName(par_DstFd, par_SrcFd, srcWindowSection.c_str());
        }
        (void)IniFileDataBaseWriteString(par_DstSection, entry, dstWindowName.c_str(), par_DstFd);
        AddWindowToCorrespondAllList(dstWindowName.c_str(), dstWindowSection.c_str(), par_DstFd);
    }
}

// ---------------------------------------------------------------------------
// Control-lamp stencil helpers
// ---------------------------------------------------------------------------

void ImportExportDialogManager::LoadLampStencils(std::vector<LampStencil> &out_Stencils,
                                                 int par_FileDescriptor)
{
    out_Stencils.clear();
    for (int i{0}; i < MAX_SHEET_SCAN; ++i)
    {
        char entry[INI_MAX_ENTRYNAME_LENGTH];
        std::snprintf(entry, sizeof(entry), "Stancil_%d", i);
        const std::string text{ReadIniString(LAMP_STENCIL_SECTION, entry, "", par_FileDescriptor)};
        if (text.empty())
        {
            break;
        }
        const std::string::size_type comma{text.find(',')};
        LampStencil stencil;
        if (std::string::npos == comma)
        {
            stencil.name = text;
        }
        else
        {
            stencil.name = text.substr(0, comma);
            stencil.polygon = text.substr(comma + 1);
        }
        out_Stencils.push_back(std::move(stencil));
    }
}

void ImportExportDialogManager::SaveLampStencils(const std::vector<LampStencil> &par_Stencils,
                                                 int par_FileDescriptor)
{
    // Clear the previous stencil section, then re-emit "Stancil_<n>=name,polygon".
    (void)IniFileDataBaseWriteString(LAMP_STENCIL_SECTION, nullptr, nullptr, par_FileDescriptor);
    for (int i{0}; i < static_cast<int>(par_Stencils.size()); ++i)
    {
        char entry[INI_MAX_ENTRYNAME_LENGTH];
        std::snprintf(entry, sizeof(entry), "Stancil_%d", i);
        const LampStencil &stencil{par_Stencils[static_cast<size_t>(i)]};
        const std::string text{stencil.name + "," + stencil.polygon};
        (void)IniFileDataBaseWriteString(LAMP_STENCIL_SECTION, entry, text.c_str(),
                                         par_FileDescriptor);
    }
}

std::string ImportExportDialogManager::MakeUniqueLampName(const std::vector<LampStencil> &par_Stencils,
                                                          const std::string &par_Name) const
{
    const auto exists{[&par_Stencils](const std::string &name) {
        for (const LampStencil &stencil : par_Stencils)
        {
            if (0 == stencil.name.compare(name))
            {
                return true;
            }
        }
        return false;
    }};
    if (!exists(par_Name))
    {
        return par_Name;
    }
    for (int i{1}; i < MAX_SHEET_SCAN; ++i)
    {
        char suffix[32];
        std::snprintf(suffix, sizeof(suffix), "_%d", i);
        const std::string candidate{par_Name + suffix};
        if (!exists(candidate))
        {
            return candidate;
        }
    }
    return par_Name;
}

// ---------------------------------------------------------------------------
// Open entry points
// ---------------------------------------------------------------------------

void ImportExportDialogManager::OpenExportSheets()
{
    // Persist the live sheets so their sections exist before we copy them out.
    WindowManager::GetInstance().SaveLayout(GetMainFileDescriptor());
    RefreshSheetList(m_sheetAvailable, GetMainFileDescriptor());
    m_sheetSelected.clear();
    m_sheetAvailableSel = -1;
    m_sheetSelectedSel = -1;
    m_sheetFile.clear();
    OpenModal(Dialog::ExportSheets);
}

void ImportExportDialogManager::OpenImportSheets()
{
    m_sheetAvailable.clear();
    m_sheetSelected.clear();
    m_sheetAvailableSel = -1;
    m_sheetSelectedSel = -1;
    m_sheetFile.clear();
    OpenModal(Dialog::ImportSheets);
}

void ImportExportDialogManager::OpenExportVariableProperties()
{
    std::snprintf(m_varFilter.data(), m_varFilter.size(), "*");
    m_varExistingOnly = true;
    m_varSelected.clear();
    m_varAvailableSel = -1;
    m_varSelectedSel = -1;
    m_varFile.clear();
    RefreshVariableList(m_varAvailable, GetMainFileDescriptor(), m_varExistingOnly,
                        m_varFilter.data());
    OpenModal(Dialog::ExportVariableProps);
}

void ImportExportDialogManager::OpenImportVariableProperties()
{
    std::snprintf(m_varFilter.data(), m_varFilter.size(), "*");
    m_varExistingOnly = true;
    m_varAvailable.clear();
    m_varSelected.clear();
    m_varAvailableSel = -1;
    m_varSelectedSel = -1;
    m_varFile.clear();
    m_impUnit = m_impValue = m_impConversion = m_impDisplay = m_impStep = m_impColor = true;
    OpenModal(Dialog::ImportVariableProps);
}

void ImportExportDialogManager::OpenExportReferenceList()
{
    RefreshProcessList();
    const int fd{GetMainFileDescriptor()};
    const std::string curProcess{ReadIniString("BasicCalibrationSettings", "ApplProcessSCRefL", "", fd)};
    for (int i{0}; i < static_cast<int>(m_processes.size()); ++i)
    {
        if (0 == m_processes[static_cast<size_t>(i)].compare(curProcess))
        {
            m_selectedProcess = i;
        }
    }
    const std::string format{ReadIniString("BasicCalibrationSettings", "SCRefL_Format", "indexed", fd)};
    const bool newFormat{(0 == format.compare("not indexed")) || (0 == format.compare("notindexed")) ||
                         (0 == format.compare("not_indexed")) ||
                         (0 != s_main_ini_val.SaveRefListInNewFormatAsDefault)};
    m_refFormat = newFormat ? 1 : 0;
    m_refFile.clear();
    OpenModal(Dialog::ExportReferenceList);
}

void ImportExportDialogManager::OpenImportReferenceList()
{
    RefreshProcessList();
    const std::string curProcess{
        ReadIniString("BasicCalibrationSettings", "ApplProcessSCRefL", "", GetMainFileDescriptor())};
    for (int i{0}; i < static_cast<int>(m_processes.size()); ++i)
    {
        if (0 == m_processes[static_cast<size_t>(i)].compare(curProcess))
        {
            m_selectedProcess = i;
        }
    }
    m_refFile.clear();
    OpenModal(Dialog::ImportReferenceList);
}

void ImportExportDialogManager::OpenLoadSvl()
{
    RefreshProcessList();
    const int fd{GetMainFileDescriptor()};
    const std::string curProcess{ReadIniString("BasicCalibrationSettings", "ApplProcessSvl", "", fd)};
    for (int i{0}; i < static_cast<int>(m_processes.size()); ++i)
    {
        if (0 == m_processes[static_cast<size_t>(i)].compare(curProcess))
        {
            m_selectedProcess = i;
        }
    }
    m_svlFile = ReadIniString("BasicCalibrationSettings", "SvlReadFileName", "*.svl", fd);
    OpenModal(Dialog::LoadSvl);
}

void ImportExportDialogManager::OpenLoadValues()
{
    RefreshProcessList();
    const int fd{GetMainFileDescriptor()};
    const std::string curProcess{ReadIniString("BasicCalibrationSettings", "ApplProcessSymbol", "", fd)};
    for (int i{0}; i < static_cast<int>(m_processes.size()); ++i)
    {
        if (0 == m_processes[static_cast<size_t>(i)].compare(curProcess))
        {
            m_selectedProcess = i;
        }
    }
    m_lvSymbolFile = ReadIniString("BasicCalibrationSettings", "SymbolFileName", "*.dat", fd);
    m_lvBinaryFile = ReadIniString("BasicCalibrationSettings", "BinaryFileName", "*.bin", fd);
    m_lvSvlFile = ReadIniString("BasicCalibrationSettings", "SvlWriteFileName", "*.svl", fd);
    std::snprintf(m_lvStartAddr.data(), m_lvStartAddr.size(), "%s",
                  ReadIniString("BasicCalibrationSettings", "StartAddress", "0x0", fd).c_str());
    std::snprintf(m_lvBlockSize.data(), m_lvBlockSize.size(), "%s",
                  ReadIniString("BasicCalibrationSettings", "BlockSize", "0xFFFFFFFF", fd).c_str());
    std::snprintf(m_lvGpLabel.data(), m_lvGpLabel.size(), "%s",
                  ReadIniString("BasicCalibrationSettings", "GpLabelAddress", "", fd).c_str());
    const std::string memType{ReadIniString("BasicCalibrationSettings", "MemoryType", "LSB_FIRST", fd)};
    m_lvEndian = (0 == memType.compare("MSB_FIRST")) ? 1 : 0;
    const std::string src{ReadIniString("BasicCalibrationSettings", "ApplicationSource", "PROCESS", fd)};
    m_lvTarget = (0 == src.compare("FILE")) ? 1 : 0;
    OpenModal(Dialog::LoadValues);
}

void ImportExportDialogManager::OpenControlLamps()
{
    if (0 < m_lampFd)
    {
        (void)IniFileDataBaseClose(m_lampFd);
    }
    m_lampFd = -1;
    m_lampFile.clear();
    m_lampExternal.clear();
    m_lampExternalSel = -1;
    m_lampInternalSel = -1;
    m_lampInternalDirty = false;
    m_lampExternalDirty = false;
    LoadLampStencils(m_lampInternal, GetMainFileDescriptor());
    OpenModal(Dialog::ControlLamps);
}

void ImportExportDialogManager::OpenBinaryCompare()
{
    m_binFile1.clear();
    m_binFile2.clear();
    m_binImage1.clear();
    m_binImage2.clear();
    OpenModal(Dialog::BinaryCompare);
}

// ---------------------------------------------------------------------------
// Render dispatch
// ---------------------------------------------------------------------------

void ImportExportDialogManager::Render()
{
    switch (m_active)
    {
    case Dialog::ExportSheets:
        RenderExportSheets();
        break;
    case Dialog::ImportSheets:
        RenderImportSheets();
        break;
    case Dialog::ExportVariableProps:
        RenderExportVariableProperties();
        break;
    case Dialog::ImportVariableProps:
        RenderImportVariableProperties();
        break;
    case Dialog::ExportReferenceList:
        RenderExportReferenceList();
        break;
    case Dialog::ImportReferenceList:
        RenderImportReferenceList();
        break;
    case Dialog::LoadSvl:
        RenderLoadSvl();
        break;
    case Dialog::LoadValues:
        RenderLoadValues();
        break;
    case Dialog::ControlLamps:
        RenderControlLamps();
        break;
    case Dialog::BinaryCompare:
        RenderBinaryCompare();
        break;
    case Dialog::None:
    default:
        break;
    }
    RenderFilePicker();
}

// ---------------------------------------------------------------------------
// Sheets
// ---------------------------------------------------------------------------

void ImportExportDialogManager::RenderExportSheets()
{
    if (!BeginModal("Export sheets###ExportSheets", true))
    {
        return;
    }

    ImGui::TextUnformatted("Select the sheets to export, then choose a destination file.");
    ImGui::Separator();

    const float columnWidth{ImGui::GetContentRegionAvail().x * 0.42f};
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Available sheets");
    ImGui::SetNextItemWidth(columnWidth);
    if (DrawSelectableList("##sheetAvail", m_sheetAvailable, m_sheetAvailableSel, LIST_HEIGHT))
    {
        MoveSelected(m_sheetAvailable, m_sheetAvailableSel, m_sheetSelected);
    }
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (ImGui::Button(">>", ImVec2(40.0f, 0.0f)))
    {
        MoveSelected(m_sheetAvailable, m_sheetAvailableSel, m_sheetSelected);
    }
    if (ImGui::Button("<<", ImVec2(40.0f, 0.0f)))
    {
        MoveSelected(m_sheetSelected, m_sheetSelectedSel, m_sheetAvailable);
    }
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Sheets to export");
    ImGui::SetNextItemWidth(columnWidth);
    if (DrawSelectableList("##sheetSel", m_sheetSelected, m_sheetSelectedSel, LIST_HEIGHT))
    {
        MoveSelected(m_sheetSelected, m_sheetSelectedSel, m_sheetAvailable);
    }
    ImGui::EndGroup();

    ImGui::Separator();
    char fileBuffer[INI_MAX_LINE_LENGTH];
    std::snprintf(fileBuffer, sizeof(fileBuffer), "%s", m_sheetFile.c_str());
    ImGui::SetNextItemWidth(-90.0f);
    if (ImGui::InputText("##sheetFile", fileBuffer, sizeof(fileBuffer)))
    {
        m_sheetFile = fileBuffer;
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse..."))
    {
        OpenFilePicker("Save sheet(s) to file", ".ini", true, &m_sheetFile);
    }

    ImGui::Separator();
    if (ImGui::Button("Export", ImVec2(120.0f, 0.0f)))
    {
        if (m_sheetFile.empty())
        {
            DialogManager::GetInstance().AddErrorMessage(2, "Export sheets", "No file selected!");
        }
        else if (m_sheetSelected.empty())
        {
            DialogManager::GetInstance().AddErrorMessage(2, "Export sheets", "No sheet selected!");
        }
        else
        {
            int dstFd{IniFileDataBaseOpen(m_sheetFile.c_str())};
            if (0 >= dstFd)
            {
                dstFd = IniFileDataBaseCreateAndOpenNewIniFile(m_sheetFile.c_str());
            }
            if (0 < dstFd)
            {
                const int srcFd{GetMainFileDescriptor()};
                for (const std::string &sheetName : m_sheetSelected)
                {
                    // Locate the source sheet section by name.
                    for (int x{0}; x < MAX_SHEET_SCAN; ++x)
                    {
                        char srcSection[INI_MAX_SECTION_LENGTH];
                        std::snprintf(srcSection, sizeof(srcSection), "GUI/OpenWindowsForSheet%d", x);
                        const std::string name{ReadIniString(srcSection, "SheetName",
                                                             (0 == x) ? "Default" : "", srcFd)};
                        if (name.empty())
                        {
                            break;
                        }
                        if (0 != name.compare(sheetName))
                        {
                            continue;
                        }
                        // Find a free destination sheet index.
                        int dstIdx{0};
                        for (; dstIdx < MAX_SHEET_SCAN; ++dstIdx)
                        {
                            char dstSection[INI_MAX_SECTION_LENGTH];
                            std::snprintf(dstSection, sizeof(dstSection), "GUI/OpenWindowsForSheet%d",
                                          dstIdx);
                            if (0 == IniFileDataBaseLookIfSectionExist(dstSection, dstFd))
                            {
                                break;
                            }
                        }
                        char dstSection[INI_MAX_SECTION_LENGTH];
                        std::snprintf(dstSection, sizeof(dstSection), "GUI/OpenWindowsForSheet%d",
                                      dstIdx);
                        CopyOneSheet(srcFd, srcSection, dstFd, dstSection, sheetName.c_str());
                        break;
                    }
                }
                if (0 != IniFileDataBaseSave(dstFd, m_sheetFile.c_str(), 2))
                {
                    DialogManager::GetInstance().AddErrorMessage(2, "Export sheets",
                                                                 "cannot export sheet(s) to file");
                }
                m_active = Dialog::None;
                ImGui::CloseCurrentPopup();
            }
            else
            {
                DialogManager::GetInstance().AddErrorMessage(2, "Export sheets",
                                                             "cannot open the destination file");
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
    {
        m_active = Dialog::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void ImportExportDialogManager::RenderImportSheets()
{
    if (!BeginModal("Import sheets###ImportSheets", true))
    {
        return;
    }

    ImGui::TextUnformatted("Choose a file, pick the sheets to import.");
    char fileBuffer[INI_MAX_LINE_LENGTH];
    std::snprintf(fileBuffer, sizeof(fileBuffer), "%s", m_sheetFile.c_str());
    ImGui::SetNextItemWidth(-90.0f);
    if (ImGui::InputText("##impSheetFile", fileBuffer, sizeof(fileBuffer)))
    {
        m_sheetFile = fileBuffer;
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse..."))
    {
        OpenFilePicker("Import sheet(s) from file", ".ini", false, &m_sheetFile);
    }
    if (ImGui::Button("Load list") && !m_sheetFile.empty())
    {
        const int fd{IniFileDataBaseOpen(m_sheetFile.c_str())};
        if (0 < fd)
        {
            RefreshSheetList(m_sheetAvailable, fd);
            (void)IniFileDataBaseClose(fd);
            m_sheetSelected.clear();
        }
        else
        {
            DialogManager::GetInstance().AddErrorMessage(2, "Import sheets", "cannot open the file");
        }
    }
    ImGui::Separator();

    const float columnWidth{ImGui::GetContentRegionAvail().x * 0.42f};
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Available sheets");
    ImGui::SetNextItemWidth(columnWidth);
    if (DrawSelectableList("##impSheetAvail", m_sheetAvailable, m_sheetAvailableSel, LIST_HEIGHT))
    {
        MoveSelected(m_sheetAvailable, m_sheetAvailableSel, m_sheetSelected);
    }
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (ImGui::Button(">>", ImVec2(40.0f, 0.0f)))
    {
        MoveSelected(m_sheetAvailable, m_sheetAvailableSel, m_sheetSelected);
    }
    if (ImGui::Button("<<", ImVec2(40.0f, 0.0f)))
    {
        MoveSelected(m_sheetSelected, m_sheetSelectedSel, m_sheetAvailable);
    }
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Sheets to import");
    ImGui::SetNextItemWidth(columnWidth);
    if (DrawSelectableList("##impSheetSel", m_sheetSelected, m_sheetSelectedSel, LIST_HEIGHT))
    {
        MoveSelected(m_sheetSelected, m_sheetSelectedSel, m_sheetAvailable);
    }
    ImGui::EndGroup();

    ImGui::Separator();
    if (ImGui::Button("Import", ImVec2(120.0f, 0.0f)))
    {
        if (m_sheetFile.empty() || m_sheetSelected.empty())
        {
            DialogManager::GetInstance().AddErrorMessage(2, "Import sheets", "No sheet selected!");
        }
        else
        {
            const int srcFd{IniFileDataBaseOpen(m_sheetFile.c_str())};
            if (0 < srcFd)
            {
                // Persist current sheets, then append the imported ones and reload.
                const int dstFd{GetMainFileDescriptor()};
                WindowManager::GetInstance().SaveLayout(dstFd);

                std::vector<std::string> existing;
                RefreshSheetList(existing, dstFd);
                int dstIdx{static_cast<int>(existing.size())};

                for (const std::string &sheetName : m_sheetSelected)
                {
                    for (int x{0}; x < MAX_SHEET_SCAN; ++x)
                    {
                        char srcSection[INI_MAX_SECTION_LENGTH];
                        std::snprintf(srcSection, sizeof(srcSection), "GUI/OpenWindowsForSheet%d", x);
                        const std::string name{ReadIniString(srcSection, "SheetName",
                                                             (0 == x) ? "Default" : "", srcFd)};
                        if (name.empty())
                        {
                            break;
                        }
                        if (0 != name.compare(sheetName))
                        {
                            continue;
                        }
                        // Deduplicate the sheet name against the destination.
                        std::string dstName{sheetName};
                        for (int i{1}; std::find(existing.begin(), existing.end(), dstName) !=
                                       existing.end();
                             ++i)
                        {
                            char suffix[32];
                            std::snprintf(suffix, sizeof(suffix), " (%d)", i);
                            dstName = sheetName + suffix;
                        }
                        existing.push_back(dstName);
                        char dstSection[INI_MAX_SECTION_LENGTH];
                        std::snprintf(dstSection, sizeof(dstSection), "GUI/OpenWindowsForSheet%d",
                                      dstIdx);
                        CopyOneSheet(srcFd, srcSection, dstFd, dstSection, dstName.c_str());
                        ++dstIdx;
                        break;
                    }
                }
                (void)IniFileDataBaseClose(srcFd);
                WindowManager::GetInstance().ReloadFromIni(dstFd);
            }
            m_active = Dialog::None;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
    {
        m_active = Dialog::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Variable properties
// ---------------------------------------------------------------------------

void ImportExportDialogManager::RenderExportVariableProperties()
{
    if (!BeginModal("Export variable properties###ExportVarProps", true))
    {
        return;
    }

    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("Filter", m_varFilter.data(), m_varFilter.size());
    ImGui::SameLine();
    if (ImGui::Checkbox("Existing only", &m_varExistingOnly) || ImGui::IsItemDeactivatedAfterEdit())
    {
        RefreshVariableList(m_varAvailable, GetMainFileDescriptor(), m_varExistingOnly,
                            m_varFilter.data());
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply filter"))
    {
        RefreshVariableList(m_varAvailable, GetMainFileDescriptor(), m_varExistingOnly,
                            m_varFilter.data());
    }
    ImGui::Separator();

    const float columnWidth{ImGui::GetContentRegionAvail().x * 0.42f};
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Available variables");
    ImGui::SetNextItemWidth(columnWidth);
    if (DrawSelectableList("##varAvail", m_varAvailable, m_varAvailableSel, LIST_HEIGHT))
    {
        MoveSelected(m_varAvailable, m_varAvailableSel, m_varSelected);
    }
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (ImGui::Button(">>", ImVec2(40.0f, 0.0f)))
    {
        MoveSelected(m_varAvailable, m_varAvailableSel, m_varSelected);
    }
    if (ImGui::Button("<<", ImVec2(40.0f, 0.0f)))
    {
        MoveSelected(m_varSelected, m_varSelectedSel, m_varAvailable);
    }
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Variables to export");
    ImGui::SetNextItemWidth(columnWidth);
    if (DrawSelectableList("##varSel", m_varSelected, m_varSelectedSel, LIST_HEIGHT))
    {
        MoveSelected(m_varSelected, m_varSelectedSel, m_varAvailable);
    }
    ImGui::EndGroup();

    ImGui::Separator();
    char fileBuffer[INI_MAX_LINE_LENGTH];
    std::snprintf(fileBuffer, sizeof(fileBuffer), "%s", m_varFile.c_str());
    ImGui::SetNextItemWidth(-90.0f);
    if (ImGui::InputText("##expVarFile", fileBuffer, sizeof(fileBuffer)))
    {
        m_varFile = fileBuffer;
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse..."))
    {
        OpenFilePicker("Export variable properties to file", ".ini", true, &m_varFile);
    }

    ImGui::Separator();
    if (ImGui::Button("Export", ImVec2(120.0f, 0.0f)))
    {
        if (m_varFile.empty())
        {
            DialogManager::GetInstance().AddErrorMessage(2, "Export variable properties",
                                                         "you must select a file to export to first");
        }
        else if (m_varSelected.empty())
        {
            DialogManager::GetInstance().AddErrorMessage(2, "Export variable properties",
                                                         "No variables selected!");
        }
        else
        {
            const int dstFd{IniFileDataBaseCreateAndOpenNewIniFile(m_varFile.c_str())};
            if (0 < dstFd)
            {
                const int srcFd{GetMainFileDescriptor()};
                for (const std::string &varName : m_varSelected)
                {
                    char props[INI_MAX_LINE_LENGTH];
                    (void)IniFileDataBaseReadString("Variables", varName.c_str(), "", props,
                                                    sizeof(props), srcFd);
                    (void)IniFileDataBaseWriteString("Variables", varName.c_str(), props, dstFd);
                }
                if (0 != IniFileDataBaseSave(dstFd, m_varFile.c_str(), 2))
                {
                    DialogManager::GetInstance().AddErrorMessage(2, "Export variable properties",
                                                                 "cannot write to file");
                }
                (void)IniFileDataBaseClose(dstFd);
                m_active = Dialog::None;
                ImGui::CloseCurrentPopup();
            }
            else
            {
                DialogManager::GetInstance().AddErrorMessage(2, "Export variable properties",
                                                             "cannot export to file");
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
    {
        m_active = Dialog::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void ImportExportDialogManager::RenderImportVariableProperties()
{
    if (!BeginModal("Import variable properties###ImportVarProps", true))
    {
        return;
    }

    char fileBuffer[INI_MAX_LINE_LENGTH];
    std::snprintf(fileBuffer, sizeof(fileBuffer), "%s", m_varFile.c_str());
    ImGui::SetNextItemWidth(-90.0f);
    if (ImGui::InputText("##impVarFile", fileBuffer, sizeof(fileBuffer)))
    {
        m_varFile = fileBuffer;
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse..."))
    {
        OpenFilePicker("Import variable properties", ".ini", false, &m_varFile);
    }

    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("Filter", m_varFilter.data(), m_varFilter.size());
    ImGui::SameLine();
    ImGui::Checkbox("Existing only", &m_varExistingOnly);
    ImGui::SameLine();
    if (ImGui::Button("Load / filter") && !m_varFile.empty())
    {
        const int fd{IniFileDataBaseOpen(m_varFile.c_str())};
        if (0 < fd)
        {
            RefreshVariableList(m_varAvailable, fd, m_varExistingOnly, m_varFilter.data());
            (void)IniFileDataBaseClose(fd);
        }
        else
        {
            DialogManager::GetInstance().AddErrorMessage(2, "Import variable properties",
                                                         "cannot read file");
        }
    }
    ImGui::Separator();

    ImGui::TextUnformatted("Properties to import:");
    ImGui::Checkbox("Unit", &m_impUnit);
    ImGui::SameLine();
    ImGui::Checkbox("Min/Max", &m_impValue);
    ImGui::SameLine();
    ImGui::Checkbox("Conversion", &m_impConversion);
    ImGui::SameLine();
    ImGui::Checkbox("Display", &m_impDisplay);
    ImGui::SameLine();
    ImGui::Checkbox("Step type", &m_impStep);
    ImGui::SameLine();
    ImGui::Checkbox("Color", &m_impColor);
    ImGui::Separator();

    const float columnWidth{ImGui::GetContentRegionAvail().x * 0.42f};
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Available variables");
    ImGui::SetNextItemWidth(columnWidth);
    if (DrawSelectableList("##impVarAvail", m_varAvailable, m_varAvailableSel, LIST_HEIGHT))
    {
        MoveSelected(m_varAvailable, m_varAvailableSel, m_varSelected);
    }
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (ImGui::Button(">>", ImVec2(40.0f, 0.0f)))
    {
        MoveSelected(m_varAvailable, m_varAvailableSel, m_varSelected);
    }
    if (ImGui::Button("<<", ImVec2(40.0f, 0.0f)))
    {
        MoveSelected(m_varSelected, m_varSelectedSel, m_varAvailable);
    }
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Variables to import");
    ImGui::SetNextItemWidth(columnWidth);
    if (DrawSelectableList("##impVarSel", m_varSelected, m_varSelectedSel, LIST_HEIGHT))
    {
        MoveSelected(m_varSelected, m_varSelectedSel, m_varAvailable);
    }
    ImGui::EndGroup();

    ImGui::Separator();
    if (ImGui::Button("Import", ImVec2(120.0f, 0.0f)))
    {
        if (m_varFile.empty())
        {
            DialogManager::GetInstance().AddErrorMessage(2, "Import variable properties",
                                                         "No file is selected!");
        }
        else if (m_varSelected.empty())
        {
            DialogManager::GetInstance().AddErrorMessage(2, "Import variable properties",
                                                         "No variables selected!");
        }
        else
        {
            const int fd{IniFileDataBaseOpen(m_varFile.c_str())};
            if (0 < fd)
            {
                for (const std::string &varName : m_varSelected)
                {
                    char name[BBVARI_NAME_SIZE];
                    std::snprintf(name, sizeof(name), "%s", varName.c_str());
                    (void)ImportOneVariablePropertiesFlags(fd, name, m_impUnit ? 1 : 0,
                                                           m_impValue ? 1 : 0, m_impConversion ? 1 : 0,
                                                           m_impDisplay ? 1 : 0, m_impStep ? 1 : 0,
                                                           m_impColor ? 1 : 0);
                }
                (void)IniFileDataBaseClose(fd);
                m_active = Dialog::None;
                ImGui::CloseCurrentPopup();
            }
            else
            {
                DialogManager::GetInstance().AddErrorMessage(2, "Import variable properties",
                                                             "cannot read file");
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
    {
        m_active = Dialog::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Reference list
// ---------------------------------------------------------------------------

void ImportExportDialogManager::RenderExportReferenceList()
{
    if (!BeginModal("Export reference list###ExportRefList", false))
    {
        return;
    }

    ImGui::TextUnformatted("Process:");
    DrawSelectableList("##refExpProc", m_processes, m_selectedProcess, 160.0f);

    ImGui::RadioButton("Old format (indexed)", &m_refFormat, 0);
    ImGui::SameLine();
    ImGui::RadioButton("New format (not indexed)", &m_refFormat, 1);

    char fileBuffer[INI_MAX_LINE_LENGTH];
    std::snprintf(fileBuffer, sizeof(fileBuffer), "%s", m_refFile.c_str());
    ImGui::SetNextItemWidth(-90.0f);
    if (ImGui::InputText("##refExpFile", fileBuffer, sizeof(fileBuffer)))
    {
        m_refFile = fileBuffer;
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse..."))
    {
        OpenFilePicker("Save SCRefL file", ".SCRefL", true, &m_refFile);
    }

    ImGui::Separator();
    if (ImGui::Button("Export", ImVec2(120.0f, 0.0f)))
    {
        if (m_refFile.empty())
        {
            DialogManager::GetInstance().AddErrorMessage(2, "Export reference list",
                                                         "No File selected!");
        }
        else if ((0 > m_selectedProcess) ||
                 (m_selectedProcess >= static_cast<int>(m_processes.size())))
        {
            DialogManager::GetInstance().AddErrorMessage(2, "Export reference list",
                                                         "No Process selected!");
        }
        else
        {
            const int fd{GetMainFileDescriptor()};
            const std::string &proc{m_processes[static_cast<size_t>(m_selectedProcess)]};
            (void)IniFileDataBaseWriteString("BasicCalibrationSettings", "SCRefL_Format",
                                             (1 == m_refFormat) ? "not indexed" : "indexed", fd);
            (void)IniFileDataBaseWriteString("BasicCalibrationSettings", "ApplProcessSCRefL",
                                             proc.c_str(), fd);
            (void)IniFileDataBaseWriteString("BasicCalibrationSettings", "SCRefLReadFileName",
                                             m_refFile.c_str(), fd);
            (void)ExportVariableReferenceList(get_pid_by_name(proc.c_str()), m_refFile.c_str(),
                                              m_refFormat);
            m_active = Dialog::None;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
    {
        m_active = Dialog::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void ImportExportDialogManager::RenderImportReferenceList()
{
    if (!BeginModal("Import reference list###ImportRefList", false))
    {
        return;
    }

    ImGui::TextUnformatted("Process:");
    DrawSelectableList("##refImpProc", m_processes, m_selectedProcess, 160.0f);

    char fileBuffer[INI_MAX_LINE_LENGTH];
    std::snprintf(fileBuffer, sizeof(fileBuffer), "%s", m_refFile.c_str());
    ImGui::SetNextItemWidth(-90.0f);
    if (ImGui::InputText("##refImpFile", fileBuffer, sizeof(fileBuffer)))
    {
        m_refFile = fileBuffer;
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse..."))
    {
        OpenFilePicker("Open SCRefL file", ".SCRefL", false, &m_refFile);
    }

    ImGui::Separator();
    if (ImGui::Button("Import", ImVec2(120.0f, 0.0f)))
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (m_refFile.empty() || !fs::exists(m_refFile, ec))
        {
            DialogManager::GetInstance().AddErrorMessage(2, "Import reference list",
                                                         "No SCRefL-File selected!");
        }
        else if ((0 > m_selectedProcess) ||
                 (m_selectedProcess >= static_cast<int>(m_processes.size())))
        {
            DialogManager::GetInstance().AddErrorMessage(2, "Import reference list",
                                                         "No Process selected!");
        }
        else
        {
            const int fd{GetMainFileDescriptor()};
            const std::string &proc{m_processes[static_cast<size_t>(m_selectedProcess)]};
            (void)IniFileDataBaseWriteString("BasicCalibrationSettings", "ApplProcessSCRefL",
                                             proc.c_str(), fd);
            (void)IniFileDataBaseWriteString("BasicCalibrationSettings", "SCRefLReadFileName",
                                             m_refFile.c_str(), fd);
            (void)ImportVariableReferenceList(get_pid_by_name(proc.c_str()), m_refFile.c_str());
            m_active = Dialog::None;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
    {
        m_active = Dialog::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Load SVL
// ---------------------------------------------------------------------------

void ImportExportDialogManager::RenderLoadSvl()
{
    if (!BeginModal("Load SVL file###LoadSvl", false))
    {
        return;
    }

    ImGui::TextUnformatted("Process:");
    DrawSelectableList("##svlProc", m_processes, m_selectedProcess, 160.0f);

    char fileBuffer[INI_MAX_LINE_LENGTH];
    std::snprintf(fileBuffer, sizeof(fileBuffer), "%s", m_svlFile.c_str());
    ImGui::SetNextItemWidth(-90.0f);
    if (ImGui::InputText("##svlFile", fileBuffer, sizeof(fileBuffer)))
    {
        m_svlFile = fileBuffer;
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse..."))
    {
        OpenFilePicker("Open SVL file", ".svl", false, &m_svlFile);
    }

    ImGui::Separator();
    if (ImGui::Button("Load", ImVec2(120.0f, 0.0f)))
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (m_svlFile.empty() || !fs::exists(m_svlFile, ec))
        {
            DialogManager::GetInstance().AddErrorMessage(2, "Load SVL file", "No SVL-File selected!");
        }
        else if ((0 > m_selectedProcess) ||
                 (m_selectedProcess >= static_cast<int>(m_processes.size())))
        {
            DialogManager::GetInstance().AddErrorMessage(2, "Load SVL file", "No Process selected!");
        }
        else
        {
            const int fd{GetMainFileDescriptor()};
            const std::string &proc{m_processes[static_cast<size_t>(m_selectedProcess)]};
            (void)IniFileDataBaseWriteString("BasicCalibrationSettings", "ApplProcessSvl",
                                             proc.c_str(), fd);
            (void)IniFileDataBaseWriteString("BasicCalibrationSettings", "SvlReadFileName",
                                             m_svlFile.c_str(), fd);
            const int badLines{WriteSVLToProcess(m_svlFile.c_str(), proc.c_str())};
            if (0 < badLines)
            {
                char message[128];
                std::snprintf(message, sizeof(message),
                              "Load of SVL file was not complete successful (%d incorrect lines or "
                              "unknown labels)",
                              badLines);
                DialogManager::GetInstance().AddErrorMessage(2, "Load SVL file", message);
            }
            m_active = Dialog::None;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
    {
        m_active = Dialog::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Load values (UI shell; write path unavailable through the core seam)
// ---------------------------------------------------------------------------

void ImportExportDialogManager::RenderLoadValues()
{
    if (!BeginModal("Load values###LoadValues", true))
    {
        return;
    }

    ImGui::TextUnformatted("Process:");
    DrawSelectableList("##lvProc", m_processes, m_selectedProcess, 120.0f);

    const auto fileRow{[this](const char *par_Label, std::string &par_Target, const char *par_Title,
                              const char *par_Ext) {
        char buffer[INI_MAX_LINE_LENGTH];
        std::snprintf(buffer, sizeof(buffer), "%s", par_Target.c_str());
        ImGui::SetNextItemWidth(320.0f);
        if (ImGui::InputText(par_Label, buffer, sizeof(buffer)))
        {
            par_Target = buffer;
        }
        ImGui::SameLine();
        std::string buttonId{"Browse...##"};
        buttonId += par_Label;
        if (ImGui::Button(buttonId.c_str()))
        {
            OpenFilePicker(par_Title, par_Ext, false, &par_Target);
        }
    }};

    fileRow("Symbol file", m_lvSymbolFile, "Open symbol file", ".dat");
    fileRow("Binary file", m_lvBinaryFile, "Open binary file", ".bin");
    fileRow("SVL destination", m_lvSvlFile, "SVL destination file", ".svl");

    ImGui::InputText("Start address", m_lvStartAddr.data(), m_lvStartAddr.size());
    ImGui::InputText("Block size", m_lvBlockSize.data(), m_lvBlockSize.size());
    ImGui::InputText("GP label address", m_lvGpLabel.data(), m_lvGpLabel.size());

    ImGui::RadioButton("LSB first", &m_lvEndian, 0);
    ImGui::SameLine();
    ImGui::RadioButton("MSB first", &m_lvEndian, 1);

    ImGui::RadioButton("Write to process", &m_lvTarget, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Write to SVL file", &m_lvTarget, 1);

    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                       "Note: the symbol/binary value writer (OpenSymBin / WriteValuesToProcess /");
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                       "WriteValuesToSVL) lives only in the Qt GUI layer and is not exposed through");
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                       "the extern \"C\" core seam, so the load cannot be executed here yet.");

    ImGui::Separator();
    ImGui::BeginDisabled(true);
    (void)ImGui::Button("Load", ImVec2(120.0f, 0.0f));
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)))
    {
        m_active = Dialog::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Control lamps
// ---------------------------------------------------------------------------

void ImportExportDialogManager::RenderControlLamps()
{
    if (!BeginModal("Import/export control lamps###ControlLamps", true))
    {
        // Popup closed: release the external file descriptor if still open.
        if ((Dialog::ControlLamps != m_active) && (0 < m_lampFd))
        {
            (void)IniFileDataBaseClose(m_lampFd);
            m_lampFd = -1;
        }
        return;
    }

    char fileBuffer[INI_MAX_LINE_LENGTH];
    std::snprintf(fileBuffer, sizeof(fileBuffer), "%s", m_lampFile.c_str());
    ImGui::SetNextItemWidth(-190.0f);
    ImGui::InputText("##lampFile", fileBuffer, sizeof(fileBuffer));
    ImGui::SameLine();
    if (ImGui::Button("Open..."))
    {
        OpenFilePicker("Import/export control lamps", ".ini", false, &m_lampFile);
    }
    ImGui::SameLine();
    if (ImGui::Button("New..."))
    {
        OpenFilePicker("Export control lamps", ".ini", true, &m_lampFile);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load file") && !m_lampFile.empty())
    {
        if (0 < m_lampFd)
        {
            (void)IniFileDataBaseClose(m_lampFd);
        }
        namespace fs = std::filesystem;
        std::error_code ec;
        m_lampFd = fs::exists(m_lampFile, ec) ? IniFileDataBaseOpen(m_lampFile.c_str())
                                              : IniFileDataBaseCreateAndOpenNewIniFile(m_lampFile.c_str());
        if (0 < m_lampFd)
        {
            LoadLampStencils(m_lampExternal, m_lampFd);
            m_lampExternalDirty = false;
        }
        else
        {
            DialogManager::GetInstance().AddErrorMessage(2, "Control lamps", "cannot open the file");
            m_lampFile.clear();
        }
    }
    ImGui::Separator();

    const bool hasExternal{0 < m_lampFd};
    const float columnWidth{ImGui::GetContentRegionAvail().x * 0.42f};

    ImGui::BeginGroup();
    ImGui::TextUnformatted("Internal (running config)");
    std::vector<std::string> internalNames;
    internalNames.reserve(m_lampInternal.size());
    for (const LampStencil &stencil : m_lampInternal)
    {
        internalNames.push_back(stencil.name);
    }
    ImGui::SetNextItemWidth(columnWidth);
    DrawSelectableList("##lampInternal", internalNames, m_lampInternalSel, LIST_HEIGHT);
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::BeginDisabled(!hasExternal);
    if (ImGui::Button("< Move", ImVec2(90.0f, 0.0f)) && (0 <= m_lampExternalSel) &&
        (m_lampExternalSel < static_cast<int>(m_lampExternal.size())))
    {
        LampStencil stencil{m_lampExternal[static_cast<size_t>(m_lampExternalSel)]};
        stencil.name = MakeUniqueLampName(m_lampInternal, stencil.name);
        m_lampInternal.push_back(stencil);
        m_lampExternal.erase(m_lampExternal.begin() + m_lampExternalSel);
        m_lampExternalSel = -1;
        m_lampInternalDirty = true;
        m_lampExternalDirty = true;
    }
    if (ImGui::Button("< Copy", ImVec2(90.0f, 0.0f)) && (0 <= m_lampExternalSel) &&
        (m_lampExternalSel < static_cast<int>(m_lampExternal.size())))
    {
        LampStencil stencil{m_lampExternal[static_cast<size_t>(m_lampExternalSel)]};
        stencil.name = MakeUniqueLampName(m_lampInternal, stencil.name);
        m_lampInternal.push_back(stencil);
        m_lampInternalDirty = true;
    }
    if (ImGui::Button("Move >", ImVec2(90.0f, 0.0f)) && (0 <= m_lampInternalSel) &&
        (m_lampInternalSel < static_cast<int>(m_lampInternal.size())))
    {
        LampStencil stencil{m_lampInternal[static_cast<size_t>(m_lampInternalSel)]};
        stencil.name = MakeUniqueLampName(m_lampExternal, stencil.name);
        m_lampExternal.push_back(stencil);
        m_lampInternal.erase(m_lampInternal.begin() + m_lampInternalSel);
        m_lampInternalSel = -1;
        m_lampInternalDirty = true;
        m_lampExternalDirty = true;
    }
    if (ImGui::Button("Copy >", ImVec2(90.0f, 0.0f)) && (0 <= m_lampInternalSel) &&
        (m_lampInternalSel < static_cast<int>(m_lampInternal.size())))
    {
        LampStencil stencil{m_lampInternal[static_cast<size_t>(m_lampInternalSel)]};
        stencil.name = MakeUniqueLampName(m_lampExternal, stencil.name);
        m_lampExternal.push_back(stencil);
        m_lampExternalDirty = true;
    }
    ImGui::EndDisabled();
    if (ImGui::Button("Delete", ImVec2(90.0f, 0.0f)) && (0 <= m_lampInternalSel) &&
        (m_lampInternalSel < static_cast<int>(m_lampInternal.size())))
    {
        m_lampInternal.erase(m_lampInternal.begin() + m_lampInternalSel);
        m_lampInternalSel = -1;
        m_lampInternalDirty = true;
    }
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted("External (file)");
    std::vector<std::string> externalNames;
    externalNames.reserve(m_lampExternal.size());
    for (const LampStencil &stencil : m_lampExternal)
    {
        externalNames.push_back(stencil.name);
    }
    ImGui::SetNextItemWidth(columnWidth);
    DrawSelectableList("##lampExternal", externalNames, m_lampExternalSel, LIST_HEIGHT);
    ImGui::EndGroup();

    ImGui::Separator();
    if (ImGui::Button("OK", ImVec2(120.0f, 0.0f)))
    {
        if (m_lampInternalDirty)
        {
            SaveLampStencils(m_lampInternal, GetMainFileDescriptor());
        }
        if ((0 < m_lampFd) && m_lampExternalDirty)
        {
            SaveLampStencils(m_lampExternal, m_lampFd);
            if (0 != IniFileDataBaseSave(m_lampFd, m_lampFile.c_str(), 2))
            {
                DialogManager::GetInstance().AddErrorMessage(2, "Control lamps",
                                                             "cannot write to file");
            }
        }
        if (0 < m_lampFd)
        {
            (void)IniFileDataBaseClose(m_lampFd);
            m_lampFd = -1;
        }
        m_active = Dialog::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
    {
        if (0 < m_lampFd)
        {
            (void)IniFileDataBaseClose(m_lampFd);
            m_lampFd = -1;
        }
        m_active = Dialog::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Binary compare
// ---------------------------------------------------------------------------

namespace
{

/**
 * @brief Reads an entire file into a byte vector (best effort; empty on failure).
 */
void ReadWholeFile(const std::string &par_Path, std::vector<std::uint8_t> &out_Bytes)
{
    out_Bytes.clear();
    std::ifstream stream{par_Path, std::ios::binary};
    if (!stream)
    {
        return;
    }
    out_Bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

/**
 * @brief Draws one scrolling hex view highlighting bytes that differ from a reference image.
 * @details The pane the pointer hovers publishes its vertical scroll into @p par_SharedScroll; the
 *          other pane follows it, so both hex views stay aligned like the Qt scrollbar sync.
 * @param[in] par_Id Child-window id (must be unique per pane).
 * @param[in] par_Image Bytes shown in this pane.
 * @param[in] par_Reference Bytes of the other file used for diff highlighting.
 * @param[in,out] par_SharedScroll Shared vertical scroll offset between the two panes.
 * @param[in] par_Width Pane width in pixels.
 */
void DrawHexView(const char *par_Id, const std::vector<std::uint8_t> &par_Image,
                 const std::vector<std::uint8_t> &par_Reference, float &par_SharedScroll,
                 float par_Width)
{
    ImGui::BeginChild(par_Id, ImVec2(par_Width, 380.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    const int rowCount{(static_cast<int>(par_Image.size()) + 15) / 16};
    ImGuiListClipper clipper;
    clipper.Begin(rowCount);
    while (clipper.Step())
    {
        for (int row{clipper.DisplayStart}; row < clipper.DisplayEnd; ++row)
        {
            ImGui::Text("%08X ", static_cast<unsigned int>(row * 16));
            for (int col{0}; col < 16; ++col)
            {
                const int pos{row * 16 + col};
                ImGui::SameLine();
                if (pos < static_cast<int>(par_Image.size()))
                {
                    const std::uint8_t byte{par_Image[static_cast<size_t>(pos)]};
                    const bool differs{(pos < static_cast<int>(par_Reference.size())) &&
                                       (byte != par_Reference[static_cast<size_t>(pos)])};
                    if (differs)
                    {
                        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%02X",
                                           static_cast<unsigned int>(byte));
                    }
                    else
                    {
                        ImGui::Text("%02X", static_cast<unsigned int>(byte));
                    }
                }
                else
                {
                    ImGui::TextDisabled("  ");
                }
            }
        }
    }
    clipper.End();

    // The hovered pane drives the shared offset; the other pane follows it.
    if (ImGui::IsWindowHovered())
    {
        par_SharedScroll = ImGui::GetScrollY();
    }
    else
    {
        ImGui::SetScrollY(par_SharedScroll);
    }
    ImGui::EndChild();
}

} // namespace

void ImportExportDialogManager::RenderBinaryCompare()
{
    if (!BeginModal("Binary compare###BinaryCompare", true))
    {
        return;
    }

    const auto fileRow{[this](const char *par_Label, std::string &par_Target,
                              std::vector<std::uint8_t> &par_Image, const char *par_Title) {
        char buffer[INI_MAX_LINE_LENGTH];
        std::snprintf(buffer, sizeof(buffer), "%s", par_Target.c_str());
        ImGui::SetNextItemWidth(-190.0f);
        if (ImGui::InputText(par_Label, buffer, sizeof(buffer)))
        {
            par_Target = buffer;
        }
        ImGui::SameLine();
        std::string browseId{"Browse...##"};
        browseId += par_Label;
        if (ImGui::Button(browseId.c_str()))
        {
            OpenFilePicker(par_Title, "", false, &par_Target);
        }
        ImGui::SameLine();
        std::string loadId{"Load##"};
        loadId += par_Label;
        if (ImGui::Button(loadId.c_str()) && !par_Target.empty())
        {
            ReadWholeFile(par_Target, par_Image);
        }
    }};

    fileRow("File 1", m_binFile1, m_binImage1, "Open binary file 1");
    fileRow("File 2", m_binFile2, m_binImage2, "Open binary file 2");
    ImGui::Text("File 1: %zu bytes    File 2: %zu bytes", m_binImage1.size(), m_binImage2.size());
    ImGui::Separator();

    // Shared vertical scroll offset kept in sync between the two hex panes.
    static float sharedScroll{0.0f};
    const float columnWidth{(ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f};

    DrawHexView("##binView1", m_binImage1, m_binImage2, sharedScroll, columnWidth);
    ImGui::SameLine();
    DrawHexView("##binView2", m_binImage2, m_binImage1, sharedScroll, columnWidth);

    ImGui::Separator();
    if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)))
    {
        m_active = Dialog::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace XilEnvImGui
