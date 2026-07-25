# Dear ImGui front-end (XilEnvImGui): fetches ImGui (docking) + GLFW + ImPlot,
# builds the small `imgui` static library and wires the ImGui app sources into
# the XilEnvImGui executable (created in the top-level CMakeLists.txt).
# @author Martin Kiepfer martin.kiepfer@karcher.com
# @copyright Alfred Kaercher SE & Co. KG

set(XILENV_IMGUI_GUI_ROOT ${CMAKE_CURRENT_LIST_DIR})

################################################################################
#                              DEPENDENCIES                                     #
#                                                                              #
# Fetched with FetchContent, pinned by release tag (mirrors the toml++ and     #
# googletest pattern). ImGui and ImPlot ship no CMakeLists, so MakeAvailable   #
# only populates their source trees; the `imgui` target below compiles the     #
# needed translation units. ImPlot is compiled into the `imgui` lib (M5) for    #
# the oscilloscope port.                                                        #
################################################################################

include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.8-docking
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(
    implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG        v0.17
    GIT_SHALLOW    TRUE
)

################################################################################
#                              GLFW BACKEND SELECT                              #
#                                                                              #
# GLFW is built from source. Its X11 backend needs the Xrandr/Xinerama/Xcursor #
# /Xi development headers and the Wayland backend needs wayland-protocols. On a #
# headless CI/build host these may be absent, so the available window backend   #
# is probed and GLFW falls back to its Null platform (build + link succeed, no  #
# on-screen window). On a normal desktop the X11 backend is enabled.            #
################################################################################

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

if(WIN32)
    # Win32 backend has no extra dev-package dependency.
elseif(APPLE)
    # Cocoa backend has no extra dev-package dependency.
else()
    find_path(XILENV_X11_XRANDR_HEADER_DIR NAMES X11/extensions/Xrandr.h)
    find_program(XILENV_WAYLAND_SCANNER NAMES wayland-scanner)
    find_path(XILENV_WAYLAND_PROTOCOLS_DIR
        NAMES stable/xdg-shell/xdg-shell.xml
        PATH_SUFFIXES share/wayland-protocols)

    if(XILENV_X11_XRANDR_HEADER_DIR)
        set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
    else()
        set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)
        message(STATUS
            "XilEnvImGui: X11 development headers not found - GLFW X11 backend disabled")
    endif()

    if(XILENV_WAYLAND_SCANNER AND XILENV_WAYLAND_PROTOCOLS_DIR)
        set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
    else()
        set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
        message(STATUS
            "XilEnvImGui: wayland-protocols not found - GLFW Wayland backend disabled")
    endif()

    if(NOT GLFW_BUILD_X11 AND NOT GLFW_BUILD_WAYLAND)
        message(WARNING
            "XilEnvImGui: neither X11 nor Wayland available - GLFW builds the "
            "headless Null backend only (no on-screen window at runtime)")
    endif()
endif()

FetchContent_MakeAvailable(glfw imgui implot)

find_package(OpenGL REQUIRED)

################################################################################
#                              IMGUI STATIC LIB                                 #
################################################################################

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp)

target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${implot_SOURCE_DIR})

target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

################################################################################
#                              APP SOURCES                                      #
#                                                                              #
# Appended to the XilEnvImGui executable declared in the top-level             #
# CMakeLists.txt (mirrors how Src/GUI/Qt appends to XilEnvGui).                 #
################################################################################

# The windowing framework and widget ports live in subdirectories; expose this
# root as an include dir so headers resolve as "Framework/..." / "Widgets/...".
target_include_directories(XilEnvImGui PRIVATE ${XILENV_IMGUI_GUI_ROOT})

# The real scheduler-side oscilloscope sampler (ring buffers) is reused verbatim
# from the Qt tree: OscilloscopeCyclic.c is pure C (core headers only, no Qt),
# so it is compiled straight into XilEnvImGui instead of the empty M2 stub. Its
# directory is exposed so OscilloscopeData.h / OscilloscopeCyclic.h resolve for
# the window port and for StartupInit.c (which calls InitOsciCycleCriticalSection).
set(XILENV_QT_OSCILLOSCOPE_DIR ${XILENV_IMGUI_GUI_ROOT}/../Qt/Widgets/Oscilloscope)
target_include_directories(XilEnvImGui PRIVATE ${XILENV_QT_OSCILLOSCOPE_DIR})

target_sources(XilEnvImGui PRIVATE
    Main.cpp
    ImGuiSyncWithOtherThreads.cpp
    GuiBridgeStubs.cpp
    ${XILENV_QT_OSCILLOSCOPE_DIR}/OscilloscopeCyclic.c
    Framework/A2LCalBridge.cpp
    Framework/WindowTypeRegistry.cpp
    Framework/UpdateScheduler.cpp
    Framework/WindowManager.cpp
    Framework/BlackboardDragDrop.cpp
    Framework/GuiComponents.cpp
    Framework/BoundVariable.cpp
    Framework/GaugeRenderer.cpp
    Widgets/Text/TextWindow.cpp
    Widgets/VariableList/VariableListWindow.cpp
    Widgets/Slider/SliderWindow.cpp
    Widgets/Enum/EnumWindow.cpp
    Widgets/Bargraph/BargraphWindow.cpp
    Widgets/Lamps/LampsWindow.cpp
    Widgets/Knob/KnobWindow.cpp
    Widgets/Tacho/TachoWindow.cpp
    Widgets/Oscilloscope/OscilloscopeWindow.cpp
    Widgets/CanMessage/CanMessageWindow.cpp
    Widgets/A2LCalSingle/A2LCalSingleWindow.cpp
    Widgets/CalibrationTree/CalibrationTreeWindow.cpp)

target_link_libraries(XilEnvImGui PRIVATE imgui)
