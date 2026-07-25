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

#include "MainWinowSyncWithOtherThreads.h"

extern "C" {
#include "MainValues.h"
#include "StartupInit.h"
#include "ParseCommandLine.h"
#include "Scheduler.h"
#include "RpcSocketServer.h"
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
 * @brief Renders the full-viewport dock space, main menu bar and status child.
 */
void RenderMainFrame()
{
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Exit"))
            {
                g_running.store(false);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

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

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            RenderMainFrame();

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
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

    // Drain anything queued during teardown so no thread stays blocked.
    DrainGuiCommandQueueFromOtherThread();

    StopCore();

    return g_exitCode;
}
