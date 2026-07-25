#include "Framework/DialogManager.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

#include "imgui.h"

extern "C" {
#include "Blackboard.h"
#include "BlackboardAccess.h"
}

namespace XilEnvImGui
{

namespace
{

constexpr int PROGRESS_ID_WRAP{0x7FFF};
constexpr int SCRIPT_LEVEL_WARNING{1};

const char *const NEWPAGE_MARKER{"*NEWPAGE*"};
const char *const HEADLINE_MARKER{"*HEADLINE*"};

/**
 * @brief Trims leading and trailing ASCII whitespace.
 */
std::string Trim(const char *par_Text)
{
    if (nullptr == par_Text)
    {
        return std::string{};
    }
    std::string text{par_Text};
    std::size_t begin{0};
    std::size_t end{text.size()};
    while ((begin < end) && (0 != std::isspace(static_cast<unsigned char>(text[begin]))))
    {
        ++begin;
    }
    while ((end > begin) && (0 != std::isspace(static_cast<unsigned char>(text[end - 1]))))
    {
        --end;
    }
    return text.substr(begin, end - begin);
}

/**
 * @brief Maps a message level to a display colour (red for errors, yellow for warnings).
 */
ImVec4 LevelColor(int par_Level)
{
    if (SCRIPT_LEVEL_WARNING == par_Level)
    {
        return ImVec4(1.0f, 0.85f, 0.3f, 1.0f);
    }
    if (2 <= par_Level)
    {
        return ImVec4(1.0f, 0.45f, 0.45f, 1.0f);
    }
    return ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
}

} // namespace

DialogManager &DialogManager::GetInstance()
{
    static DialogManager s_instance;
    return s_instance;
}

// -------------------------------------------------------------------------
// Progress bars
// -------------------------------------------------------------------------

int DialogManager::OpenProgressBar(const char *par_Name)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (PROGRESS_ID_WRAP <= ++m_progressIdCounter)
    {
        m_progressIdCounter = 1;
    }
    ProgressBarEntry entry;
    entry.id = m_progressIdCounter;
    entry.name = (nullptr != par_Name) ? par_Name : "";
    entry.value = 0;
    m_progressBars.push_back(std::move(entry));
    return m_progressIdCounter;
}

void DialogManager::SetProgressBar(int par_Id, int par_Value)
{
    const int clamped{std::max(0, std::min(100, par_Value))};
    std::lock_guard<std::mutex> lock(m_mutex);
    for (ProgressBarEntry &entry : m_progressBars)
    {
        if (par_Id == entry.id)
        {
            entry.value = clamped;
            return;
        }
    }
}

void DialogManager::CloseProgressBar(int par_Id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (std::size_t i{0}; i < m_progressBars.size(); ++i)
    {
        if (par_Id == m_progressBars[i].id)
        {
            m_progressBars.erase(m_progressBars.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }
}

// -------------------------------------------------------------------------
// Error / message log
// -------------------------------------------------------------------------

void DialogManager::AddErrorMessage(int par_Level, const char *par_Header, const char *par_Message)
{
    std::string line;
    if ((nullptr != par_Header) && ('\0' != par_Header[0]))
    {
        line = par_Header;
        line += ": ";
    }
    if (nullptr != par_Message)
    {
        line += par_Message;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_log.push_back(LogEntry{par_Level, std::move(line)});
    m_errorLogOpen = true;
}

void DialogManager::AddScriptErrorMessage(int par_Level, int par_LineNr, const char *par_Filename,
                                          const char *par_Message)
{
    char prefix[512];
    if ((nullptr != par_Filename) && (0 < par_LineNr))
    {
        std::snprintf(prefix, sizeof(prefix), "%s(%d)", par_Filename, par_LineNr);
    }
    else if (nullptr != par_Filename)
    {
        std::snprintf(prefix, sizeof(prefix), "%s", par_Filename);
    }
    else
    {
        prefix[0] = '\0';
    }
    AddErrorMessage(par_Level, prefix, par_Message);
}

// -------------------------------------------------------------------------
// Script dialog - model mutation (render thread only)
// -------------------------------------------------------------------------

void DialogManager::AttachScriptItemVariable(ScriptItem &par_Item)
{
    par_Item.vid = add_bbvari(par_Item.variable.c_str(), BB_UNKNOWN, "");
    if (0 < par_Item.vid)
    {
        par_Item.minValue = get_bbvari_min(par_Item.vid);
        par_Item.maxValue = get_bbvari_max(par_Item.vid);
    }
}

int DialogManager::ScriptCreateDialog(const char *par_Headline)
{
    m_scriptDialog = ScriptDialogModel{};
    m_scriptDialog.headline = (nullptr != par_Headline) ? par_Headline : "Script Dialog";
    m_scriptDialog.pages.push_back(ScriptPage{"Main", {}});
    m_scriptDialog.active = true;
    return 0;
}

int DialogManager::ScriptAddDialogItem(const char *par_Element, const char *par_Label)
{
    if (!m_scriptDialog.active)
    {
        return -1;
    }

    const std::string element{Trim(par_Element)};
    const std::string label{Trim(par_Label)};
    if (label.empty())
    {
        // Matches the Qt behaviour: an empty label is rejected.
        return -1;
    }

    if (NEWPAGE_MARKER == label)
    {
        std::string pageName{element};
        if (NEWPAGE_MARKER == pageName)
        {
            pageName = "Page(" + std::to_string(m_scriptDialog.pages.size()) + ")";
        }
        m_scriptDialog.pages.push_back(ScriptPage{pageName, {}});
        return 0;
    }

    if (m_scriptDialog.pages.empty())
    {
        m_scriptDialog.pages.push_back(ScriptPage{"Main", {}});
    }
    ScriptPage &page = m_scriptDialog.pages.back();

    if (HEADLINE_MARKER == label)
    {
        ScriptItem item;
        item.kind = ScriptItemKind::Headline;
        item.text = element;
        page.items.push_back(std::move(item));
        return 0;
    }

    ScriptItem item;
    item.kind = ScriptItemKind::Value;
    item.text = element;
    item.variable = label;
    AttachScriptItemVariable(item);
    page.items.push_back(std::move(item));
    return 0;
}

void DialogManager::SampleScriptValues()
{
    for (ScriptPage &page : m_scriptDialog.pages)
    {
        for (ScriptItem &item : page.items)
        {
            if ((ScriptItemKind::Value == item.kind) && (0 < item.vid))
            {
                item.value = read_bbvari_double(item.vid);
            }
        }
    }
}

int DialogManager::ScriptShowDialog()
{
    if (!m_scriptDialog.active)
    {
        return -1;
    }
    SampleScriptValues();
    m_scriptDialog.visible = true;
    m_scriptDialog.closed = false;
    return 0;
}

void DialogManager::ReleaseScriptVariables(bool par_WriteBack)
{
    for (ScriptPage &page : m_scriptDialog.pages)
    {
        for (ScriptItem &item : page.items)
        {
            if ((ScriptItemKind::Value == item.kind) && (0 < item.vid))
            {
                if (par_WriteBack)
                {
                    write_bbvari_minmax_check(item.vid, item.value);
                }
                remove_bbvari(item.vid);
                item.vid = 0;
            }
        }
    }
}

void DialogManager::FinishScriptDialog(bool par_Accepted)
{
    // On accept the edited values are written back to the blackboard; on cancel the
    // attached variables are only released. Either way the dialog is now closed.
    ReleaseScriptVariables(par_Accepted);
    m_scriptDialog.accepted = par_Accepted;
    m_scriptDialog.closed = true;
    m_scriptDialog.visible = false;
}

int DialogManager::ScriptDestroyDialog()
{
    if (!m_scriptDialog.active)
    {
        return -1;
    }
    // Release any variables still attached (dialog destroyed without user action).
    ReleaseScriptVariables(false);
    m_scriptDialog = ScriptDialogModel{};
    return 0;
}

int DialogManager::IsScriptDialogClosed()
{
    if (!m_scriptDialog.active)
    {
        return 1;
    }
    if (m_scriptDialog.closed)
    {
        // Mirror the Qt slot: report closed and tear the dialog down so the next
        // CREATE_DIALOG starts from a clean model.
        m_scriptDialog = ScriptDialogModel{};
        return 1;
    }
    return 0;
}

// -------------------------------------------------------------------------
// Rendering (render thread)
// -------------------------------------------------------------------------

void DialogManager::Render()
{
    RenderProgressBars();
    RenderScriptDialog();
    RenderErrorLog();
}

void DialogManager::RenderProgressBars()
{
    std::vector<ProgressBarEntry> snapshot;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        snapshot = m_progressBars;
    }
    if (snapshot.empty())
    {
        return;
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float width{360.0f};
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - width - 10.0f,
                                   viewport->WorkPos.y + viewport->WorkSize.y - 40.0f),
                            ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
    const ImGuiWindowFlags flags{ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing};
    if (ImGui::Begin("Progress###ProgressBars", nullptr, flags))
    {
        for (const ProgressBarEntry &entry : snapshot)
        {
            if (!entry.name.empty())
            {
                ImGui::TextUnformatted(entry.name.c_str());
            }
            const float fraction{static_cast<float>(entry.value) / 100.0f};
            ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f));
        }
    }
    ImGui::End();
}

void DialogManager::RenderScriptDialog()
{
    if (!m_scriptDialog.active || !m_scriptDialog.visible || m_scriptDialog.closed)
    {
        return;
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->GetCenter().x, viewport->GetCenter().y),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);

    bool open{true};
    const std::string title{m_scriptDialog.headline + "###ScriptUserDialog"};
    const ImGuiWindowFlags flags{ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                                 ImGuiWindowFlags_NoCollapse};
    if (ImGui::Begin(title.c_str(), &open, flags))
    {
        if (ImGui::BeginTabBar("###ScriptPages"))
        {
            for (std::size_t p{0}; p < m_scriptDialog.pages.size(); ++p)
            {
                ScriptPage &page = m_scriptDialog.pages[p];
                const std::string tabId{page.name + "###ScriptPage" + std::to_string(p)};
                if (ImGui::BeginTabItem(tabId.c_str()))
                {
                    for (std::size_t r{0}; r < page.items.size(); ++r)
                    {
                        ScriptItem &item = page.items[r];
                        if (ScriptItemKind::Headline == item.kind)
                        {
                            ImGui::SeparatorText(item.text.c_str());
                            continue;
                        }

                        ImGui::PushID(static_cast<int>(r));
                        ImGui::TextUnformatted(item.text.c_str());
                        ImGui::SameLine(200.0f);
                        ImGui::SetNextItemWidth(180.0f);
                        if (0 < item.vid)
                        {
                            if (ImGui::InputDouble("###val", &item.value, 0.0, 0.0, "%.6g"))
                            {
                                if (item.maxValue > item.minValue)
                                {
                                    item.value = std::max(item.minValue,
                                                          std::min(item.maxValue, item.value));
                                }
                            }
                        }
                        else
                        {
                            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f),
                                               "Variable \"%s\" doesn't exist", item.variable.c_str());
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }

        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(100.0f, 0.0f)))
        {
            FinishScriptDialog(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
        {
            FinishScriptDialog(false);
        }
    }
    ImGui::End();

    // Window-close button ([x]) behaves like Cancel.
    if (!open && m_scriptDialog.active && !m_scriptDialog.closed)
    {
        FinishScriptDialog(false);
    }
}

void DialogManager::RenderErrorLog()
{
    std::vector<LogEntry> snapshot;
    bool open{false};
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_log.empty() || !m_errorLogOpen)
        {
            return;
        }
        snapshot = m_log;
        open = m_errorLogOpen;
    }

    ImGui::SetNextWindowSize(ImVec2(640.0f, 240.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Messages###ErrorLog", &open))
    {
        if (ImGui::Button("Clear"))
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_log.clear();
        }
        ImGui::SameLine();
        ImGui::Text("%d message(s)", static_cast<int>(snapshot.size()));
        ImGui::Separator();

        if (ImGui::BeginChild("###ErrorLogScroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                              ImGuiWindowFlags_HorizontalScrollbar))
        {
            for (const LogEntry &entry : snapshot)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(entry.level));
                ImGui::TextUnformatted(entry.text.c_str());
                ImGui::PopStyleColor();
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_errorLogOpen = open;
    }
}

} // namespace XilEnvImGui
