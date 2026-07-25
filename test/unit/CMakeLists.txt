# Unit / characterization tests for the OpenXilEnv C core (GoogleTest + GMock, CTest)
# @author Martin Kiepfer martin.kiepfer@karcher.com
# @copyright Alfred Kaercher SE & Co. KG

################################################################################
#                              GOOGLETEST                                      #
################################################################################

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
)
# Match the runtime library on Windows/MSVC builds.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

################################################################################
#                          CORE TEST-SUPPORT SOURCES                           #
#                                                                              #
# xilenv_core is compiled once WITHOUT NO_GUI and carries unresolved           #
# references to the GUI/core-seam ABI and to the four NO_GUI border files. The #
# test binary behaves like the headless XilEnv: it compiles the border files   #
# with NO_GUI and links the GUI-free Console stub implementation of the seam   #
# (everything except Console/Main.cpp, which would clash with gtest_main).     #
################################################################################

set(XILENV_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

set(XilEnvTestSupportSources
    ${XILENV_ROOT}/Src/Global/ThrowError.c
    ${XILENV_ROOT}/Src/Global/StartupInit.c
    ${XILENV_ROOT}/Src/Global/ParseCommandLine.c
    ${XILENV_ROOT}/Src/A2lParser/A2LLink.c
    ${XILENV_ROOT}/Src/GUI/Console/AllOtherStubs.cpp
    ${XILENV_ROOT}/Src/GUI/Console/ErrorDialog.cpp
    ${XILENV_ROOT}/Src/GUI/Console/MainWindow.cpp
    ${XILENV_ROOT}/Src/GUI/Console/MainWinowSyncWithOtherThreads.cpp
    ${XILENV_ROOT}/Src/GUI/Console/MessageWindow.cpp
    ${XILENV_ROOT}/Src/GUI/Console/ScriptErrorDialog.cpp
    ${XILENV_ROOT}/Src/GUI/Console/OscilloscopeCyclic.c
    ${XILENV_ROOT}/Src/GUI/Console/WaitUntilCanStart.c
)

################################################################################
#                              TEST TARGET                                     #
################################################################################

add_executable(xilenv_core_tests
    ini/test_ini_database.cpp
    blackboard/test_blackboard_access.cpp
    blackboard/test_blackboard_convert.cpp
    a2l/test_a2l_value.cpp
    can/test_can_database.cpp
    global/test_global_helpers.cpp
    equations/test_equation_parser.cpp
    utilities/test_string_max_char.cpp
    utilities/test_print_format_to_string.cpp
    test_support_stubs.cpp
    ${XilEnvTestSupportSources}
)

target_compile_definitions(xilenv_core_tests PRIVATE NO_GUI _M_X64 _CONSOLE)
target_include_directories(xilenv_core_tests PRIVATE
    ${XILENV_ROOT}/Src/GUI/Console)

target_link_libraries(xilenv_core_tests PRIVATE
    xilenv_core
    GTest::gtest_main
    GTest::gmock)

if(WIN32)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_definitions(xilenv_core_tests PRIVATE
            WINVER=0x0602 _WIN32_WINNT=0x0602 __STDC_FORMAT_MACROS)
        target_link_libraries(xilenv_core_tests PRIVATE ws2_32 Psapi z Winmm.lib)
    endif()
else()
    target_link_libraries(xilenv_core_tests PRIVATE pthread z dl)
endif()

include(GoogleTest)
gtest_discover_tests(xilenv_core_tests)
