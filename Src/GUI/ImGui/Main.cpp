#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <locale.h>
#include <thread>

#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include "MainWinowSyncWithOtherThreads.h"

#include "Framework/A2LCalBridge.h"
#include "Framework/BlackboardDialogs.h"
#include "Framework/BusConfigDialogs.h"
#include "Framework/CanConfigTree.h"
#include "Framework/CanErrorDialog.h"
#include "Framework/ControlDialogs.h"
#include "Framework/ConversionDialogs.h"
#include "Framework/DebugDialogs.h"
#include "Framework/DialogManager.h"
#include "Framework/ImportExportDialogs.h"
#include "Framework/SettingsDialogs.h"
#include "Framework/UpdateScheduler.h"
#include "Framework/WindowManager.h"
#include "Framework/WindowTypeRegistry.h"

extern "C" {
#include "MainValues.h"
#include "StartupInit.h"
#include "ParseCommandLine.h"
#include "Scheduler.h"
#include "RpcSocketServer.h"
#include "IniDataBase.h"
extern int rm_Terminate(void);
extern void rm_Kill(void);
}

namespace {

std::atomic<bool> g_running{true};   /*!< Cleared to leave the main loop. */
std::atomic<bool> g_exitCodeSeeded{false};
int g_exitCode{0};

constexpr int WINDOW_DEFAULT_WIDTH{1280};
constexpr int WINDOW_DEFAULT_HEIGHT{800};

void SignalHandler(int par_Signal)
{
    (void)par_Signal;
    g_running.store(false);
}

void GlfwErrorCallback(int par_Error, const char *par_Description)
{
    std::fprintf(stderr, "GLFW error %d: %s\n", par_Error,
                 (nullptr != par_Description) ? par_Description : "");
}

/**
 * @brief Renders the full-viewport dock space, main menu bar, windows and status child.
 * @param[in] par_Context Per-frame render context handed to every window.
 */
void RenderMainFrame(const XilEnvImGui::WindowContext &par_Context)
{
    // The sheet dock-space host (WindowManager::Render) fills the work area itself; no separate
    // DockSpaceOverViewport is needed (it would be a redundant second, unused dock space).

    if (ImGui::BeginMainMenuBar())
    {
        XilEnvImGui::SettingsDialogManager &settings = XilEnvImGui::SettingsDialogManager::GetInstance();

        // Small helper: a menu entry whose dedicated dialog is a later M7 step. It
        // opens a modal explaining that the feature is not yet implemented so the
        // menu structure reaches parity without pulling in the CAN/XCP/CCP dialogs.
        const auto placeholder{[&settings](const char *par_Title) {
            settings.ShowInfo(par_Title, "This dialog is not yet implemented in the ImGui front-end.");
        }};

        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Save configuration", "Ctrl+S"))
            {
                (void)XilEnvImGui::SaveConfiguration();
            }
            if (ImGui::MenuItem("Save configuration as..."))
            {
                settings.OpenSaveConfigurationAs();
            }
            if (ImGui::MenuItem("Load configuration..."))
            {
                settings.ShowInfo("Load configuration",
                                  "Loading a different configuration at runtime requires re-initialising "
                                  "the scheduler and processes; that plumbing is not reachable from the "
                                  "GUI seam yet. Start XilEnvImGui with the desired ini on the command "
                                  "line instead.");
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Import/Export"))
            {
                XilEnvImGui::ImportExportDialogManager &imex =
                    XilEnvImGui::ImportExportDialogManager::GetInstance();
                if (ImGui::MenuItem("Export sheets..."))
                {
                    imex.OpenExportSheets();
                }
                if (ImGui::MenuItem("Import sheets..."))
                {
                    imex.OpenImportSheets();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Export variable properties..."))
                {
                    imex.OpenExportVariableProperties();
                }
                if (ImGui::MenuItem("Import variable properties..."))
                {
                    imex.OpenImportVariableProperties();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Export reference list..."))
                {
                    imex.OpenExportReferenceList();
                }
                if (ImGui::MenuItem("Import reference list..."))
                {
                    imex.OpenImportReferenceList();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Load SVL file..."))
                {
                    imex.OpenLoadSvl();
                }
                if (ImGui::MenuItem("Load values..."))
                {
                    imex.OpenLoadValues();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Import/Export control lamps..."))
                {
                    imex.OpenControlLamps();
                }
                if (ImGui::MenuItem("Binary compare..."))
                {
                    imex.OpenBinaryCompare();
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Export A2L file..."))
            {
                placeholder("Export A2L file");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit"))
            {
                g_running.store(false);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Window"))
        {
            if (ImGui::BeginMenu("New"))
            {
                XilEnvImGui::WindowManager::GetInstance().RenderNewWindowMenu();
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Settings"))
        {
            if (ImGui::MenuItem("Basic settings..."))
            {
                settings.OpenBasicSettings();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Function hotkeys..."))
            {
                XilEnvImGui::ControlDialogManager::GetInstance().OpenHotkeys();
            }
            if (ImGui::MenuItem("Import hotkeys..."))
            {
                placeholder("Import hotkeys");
            }
            if (ImGui::MenuItem("Export hotkeys..."))
            {
                placeholder("Export hotkeys");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Configure signal conversion..."))
            {
                XilEnvImGui::ConversionDialogManager::GetInstance().OpenPicker();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Blackboard variable info..."))
            {
                XilEnvImGui::BlackboardDialogManager::GetInstance().OpenBlackboardInfo(nullptr);
            }
            if (ImGui::MenuItem("Blackboard access..."))
            {
                XilEnvImGui::BlackboardDialogManager::GetInstance().OpenBlackboardAccess();
            }
            if (ImGui::MenuItem("Internal blackboard view..."))
            {
                XilEnvImGui::BlackboardDialogManager::GetInstance().OpenBlackboardInternal();
            }
            if (ImGui::MenuItem("Global section filter..."))
            {
                XilEnvImGui::BlackboardDialogManager::GetInstance().OpenGlobalSectionFilter();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Control"))
        {
            XilEnvImGui::ControlDialogManager &control =
                XilEnvImGui::ControlDialogManager::GetInstance();
            if (ImGui::MenuItem("External process copy list..."))
            {
                XilEnvImGui::DebugDialogManager::GetInstance().OpenExternProcessCopyList();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Configure process..."))
            {
                control.OpenConfigureProcess();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Scheduler"))
        {
            XilEnvImGui::ControlDialogManager &control =
                XilEnvImGui::ControlDialogManager::GetInstance();
            if (ImGui::MenuItem("Configure schedulers..."))
            {
                control.OpenConfigureSchedulers();
            }
            if (ImGui::MenuItem("Barrier history logging..."))
            {
                XilEnvImGui::DebugDialogManager::GetInstance().OpenBarrierHistory();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Configure barriers..."))
            {
                control.OpenConfigureBarriers();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Calibration"))
        {
            if (ImGui::MenuItem("Load SVL file..."))
            {
                XilEnvImGui::ImportExportDialogManager::GetInstance().OpenLoadSvl();
            }
            if (ImGui::MenuItem("Save process to SVL/SAL file..."))
            {
                placeholder("Save process to SVL/SAL file");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Configure XCP over ethernet links..."))
            {
                XilEnvImGui::BusConfigDialogManager::GetInstance().OpenXcpOverEthernetConfig();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("CAN"))
        {
            XilEnvImGui::BusConfigDialogManager &busConfig =
                XilEnvImGui::BusConfigDialogManager::GetInstance();
            if (ImGui::MenuItem("Config..."))
            {
                busConfig.OpenCanConfig();
            }
            if (ImGui::MenuItem("Variant/object/signal editor..."))
            {
                XilEnvImGui::CanConfigTreeManager::GetInstance().OpenEditor();
            }
            if (ImGui::MenuItem("Import DBC..."))
            {
                busConfig.OpenDbcImport();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Insert CAN error..."))
            {
                XilEnvImGui::CanErrorDialogManager::GetInstance().OpenDialog();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("CCP config..."))
            {
                busConfig.OpenCcpConfig();
            }
            if (ImGui::MenuItem("XCP config..."))
            {
                busConfig.OpenXcpConfig();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::BeginMenu("Debug"))
            {
                XilEnvImGui::DebugDialogManager &debug =
                    XilEnvImGui::DebugDialogManager::GetInstance();
                if (ImGui::MenuItem("Internals..."))
                {
                    debug.OpenInternals();
                }
                if (ImGui::MenuItem("Active equations..."))
                {
                    debug.OpenActiveEquations();
                }
                if (ImGui::MenuItem("Loaded debug infos..."))
                {
                    debug.OpenLoadedDebugInfos();
                }
                if (ImGui::MenuItem("Referenced labels..."))
                {
                    debug.OpenReferencedLabels();
                }
                if (ImGui::MenuItem("Remote master call statistics..."))
                {
                    debug.OpenRemoteMasterCallStatistics();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Extended errors..."))
                {
                    debug.OpenExtError();
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("About..."))
            {
                XilEnvImGui::ControlDialogManager::GetInstance().OpenAbout();
            }
            if (ImGui::MenuItem("Homepage"))
            {
                placeholder("Homepage");
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // Ctrl+S saves the configuration like the Qt toolbar/File action.
    const ImGuiIO &menuIo = ImGui::GetIO();
    if (menuIo.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
    {
        (void)XilEnvImGui::SaveConfiguration();
    }

    XilEnvImGui::WindowManager::GetInstance().Render(par_Context);

    // Seam-driven dialogs (progress bars, script user dialog, error/message log).
    XilEnvImGui::DialogManager::GetInstance().Render();

    // Menu-driven settings dialogs (basic settings form, save-as picker, info popups).
    XilEnvImGui::SettingsDialogManager::GetInstance().Render();

    // Menu-driven CAN / CCP / XCP / XCP-over-Ethernet configuration and DBC import.
    XilEnvImGui::BusConfigDialogManager::GetInstance().Render();

    // Menu-driven CAN variant/object/signal tree editor.
    XilEnvImGui::CanConfigTreeManager::GetInstance().Render();

    // Menu-driven runtime CAN-error injection dialog.
    XilEnvImGui::CanErrorDialogManager::GetInstance().Render();

    // Menu-driven engine-control dialogs (process/scheduler/barrier config, hotkeys, about).
    XilEnvImGui::ControlDialogManager::GetInstance().Render();

    // Read-only debug/inspection dialogs (internals, debug infos, active equations, extended
    // errors, barrier history, external-process copy list, referenced labels).
    XilEnvImGui::DebugDialogManager::GetInstance().Render();

    // Signal-conversion editor dialogs (dispatcher plus type-specific sub-editors).
    XilEnvImGui::ConversionDialogManager::GetInstance().Render();

    // Blackboard/variable dialogs (info, access, internal view, section filter, add-filter, value ops).
    XilEnvImGui::BlackboardDialogManager::GetInstance().Render();

    // File / Import-Export dialogs (sheets, variable properties, reference list, SVL, values,
    // control lamps, binary compare).
    XilEnvImGui::ImportExportDialogManager::GetInstance().Render();

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float statusHeight{ImGui::GetFrameHeight()};
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - statusHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, statusHeight));
    const ImGuiWindowFlags statusFlags{ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus};
    if (ImGui::Begin("###StatusBar", nullptr, statusFlags))
    {
        ImGui::TextUnformatted("OpenXilEnv (ImGui) - ready");
    }
    ImGui::End();
}

/**
 * @brief Runs the core startup sequence shared by every front-end.
 * @return true on success, false if StartupInit failed.
 */
bool StartCore()
{
    if (StartupInit(nullptr))
    {
        return false;
    }

    SetSyncWithGuiThreadIsInitAndRunning();

    if (GetSchedulerStopByRpc())
    {
        disable_scheduler_at_end_of_cycle(SCHEDULER_CONTROLED_BY_RPC, nullptr, nullptr);
    }
    if (GetSchedulerStopByUser())
    {
        disable_scheduler_at_end_of_cycle(SCHEDULER_CONTROLED_BY_USER, nullptr, nullptr);
    }

    SchedulersStartingShot();

    StartRemoteProcedureCallThread(s_main_ini_val.RpcOverSocketOrNamedPipe, s_main_ini_val.InstanceName,
                                   s_main_ini_val.RpcSocketPort);
    return true;
}

void StopCore()
{
    TerminateSelf();

    if (s_main_ini_val.ConnectToRemoteMaster)
    {
        rm_Terminate();
        rm_Kill();
    }
}

} // namespace

// C++-linkage entry point used by the GUI bridge (CloseFromOtherThread) to ask
// the main loop to terminate; mirrors the Console/Qt front-ends.
void terminate_main_loop(int par_ExitCode, int par_ExitCodeValid)
{
    if (!g_exitCodeSeeded.load() && (0 != par_ExitCodeValid))
    {
        g_exitCode = par_ExitCode;
        g_exitCodeSeeded.store(true);
    }
    g_running.store(false);
}

int main(int argc, char *argv[])
{
#ifndef _WIN32
    SaveCommandArgs(argc, argv);
#endif

    // Always use the "C" locale so numbers use '.' and not ','.
    setlocale(LC_ALL, "C");

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    if (!StartCore())
    {
        return 1;
    }

    // Populate the window-type registry and load the persisted sheets/windows.
    // Both only touch the blackboard and ini database, so they are safe whether
    // or not a GLFW window comes up later.
    XilEnvImGui::RegisterBuiltinWindowTypes();
    XilEnvImGui::WindowManager::GetInstance().LoadLayout(GetMainFileDescriptor());

    XilEnvImGui::UpdateScheduler updateScheduler;
    XilEnvImGui::WindowContext windowContext;

    // Try to bring up a real GLFW/OpenGL window with Dear ImGui. On a headless
    // host (no X11/Wayland backend, or no display) window/context creation
    // fails; the app then falls back to a windowless loop that still drives the
    // command-queue seam so the scheduler/RPC threads never block.
    GLFWwindow *window{nullptr};
    bool guiActive{false};

    glfwSetErrorCallback(GlfwErrorCallback);
    const bool glfwInitOk{0 != glfwInit()};
    if (glfwInitOk)
    {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
        window = glfwCreateWindow(WINDOW_DEFAULT_WIDTH, WINDOW_DEFAULT_HEIGHT, "OpenXilEnv (ImGui)",
                                  nullptr, nullptr);
        if (nullptr != window)
        {
            glfwMakeContextCurrent(window);
            glfwSwapInterval(1);

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImPlot::CreateContext();
            ImGuiIO &io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
            // Own the layout persistence (layout.toml in a later step); disable
            // Dear ImGui's automatic imgui.ini handling.
            io.IniFilename = nullptr;

            ImGui::StyleColorsDark();
            ImGuiStyle &style = ImGui::GetStyle();
            if (0 != (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable))
            {
                style.WindowRounding = 0.0f;
                style.Colors[ImGuiCol_WindowBg].w = 1.0f;
            }

            ImGui_ImplGlfw_InitForOpenGL(window, true);
            ImGui_ImplOpenGL3_Init("#version 330");
            guiActive = true;
        }
        else
        {
            std::fprintf(stderr,
                         "XilEnvImGui: no GLFW window/GL context (headless host) - "
                         "running windowless command-queue loop\n");
        }
    }
    else
    {
        std::fprintf(stderr, "XilEnvImGui: glfwInit() failed - running windowless command-queue loop\n");
    }

    if (guiActive)
    {
        while (g_running.load() && (0 == glfwWindowShouldClose(window)))
        {
            glfwPollEvents();

            // Service the GUI/core seam once per frame.
            DrainGuiCommandQueueFromOtherThread();
            XilEnvImGui::A2LCalBridge::GetInstance().Drain();

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            const ImGuiIO &frameIo = ImGui::GetIO();
            windowContext.dpiScale = frameIo.DisplayFramebufferScale.x;
            updateScheduler.Advance(static_cast<double>(frameIo.DeltaTime) * 1000.0);
            XilEnvImGui::WindowManager::GetInstance().Update(updateScheduler, windowContext);

            RenderMainFrame(windowContext);

            ImGui::Render();
            int displayW{0};
            int displayH{0};
            glfwGetFramebufferSize(window, &displayW, &displayH);
            glViewport(0, 0, displayW, displayH);
            glClearColor(0.10f, 0.10f, 0.11f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            const ImGuiIO &io = ImGui::GetIO();
            if (0 != (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable))
            {
                GLFWwindow *backupContext = glfwGetCurrentContext();
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
                glfwMakeContextCurrent(backupContext);
            }

            glfwSwapBuffers(window);
        }

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
    }
    else
    {
        if (glfwInitOk)
        {
            glfwTerminate();
        }
        while (g_running.load())
        {
            DrainGuiCommandQueueFromOtherThread();
            XilEnvImGui::A2LCalBridge::GetInstance().Drain();
            updateScheduler.Advance(16.0);
            XilEnvImGui::WindowManager::GetInstance().Update(updateScheduler, windowContext);
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

    // Drain anything queued during teardown so no thread stays blocked and any
    // still-queued INDEX_DATA_BLOCKs are freed on the render thread.
    DrainGuiCommandQueueFromOtherThread();
    XilEnvImGui::A2LCalBridge::GetInstance().Drain();

    // Persist the sheets/windows back into the ini database (routed to the
    // layout descriptor); the core flushes it to disk on exit like the Qt build.
    XilEnvImGui::WindowManager::GetInstance().SaveLayout(GetMainFileDescriptor());

    StopCore();

    return g_exitCode;
}
