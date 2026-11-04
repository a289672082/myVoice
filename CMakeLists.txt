cmake_minimum_required(VERSION 3.20)
project(myVoice VERSION 0.1.0 LANGUAGES CXX)

option(MYVOICE_WITH_FTXUI "Build the FTXUI interactive console" ON)
option(MYVOICE_BUILD_TESTS "Build mock-backend integration tests" ON)

add_library(myvoice_core STATIC
    src/Process.cpp
    src/CommandShell.cpp
    src/VoiceConverter.cpp
)
add_library(myvoice::core ALIAS myvoice_core)
target_include_directories(myvoice_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(myvoice_core PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(myvoice_core PRIVATE /W4 /utf-8)
    target_compile_definitions(myvoice_core PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(myvoice_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(myvoice_cli apps/myvoice_cli.cpp)
target_link_libraries(myvoice_cli PRIVATE myvoice::core)
if(MYVOICE_WITH_FTXUI)
    # 仅使用已安装的 FTXUI，或在配置时固定拉取 v7.0.3。
    # 可使用 FETCHCONTENT_SOURCE_DIR_FTXUI 指向本地依赖以便离线构建。
    find_package(ftxui 7.0.3 QUIET CONFIG)
    if(NOT ftxui_FOUND)
        include(FetchContent)
        set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(ftxui
            GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
            GIT_TAG v7.0.3
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(ftxui)
    endif()
    target_sources(myvoice_cli PRIVATE apps/myvoice_tui.cpp)
    target_compile_definitions(myvoice_cli PRIVATE MYVOICE_WITH_FTXUI=1)
    target_link_libraries(myvoice_cli PRIVATE ftxui::component ftxui::dom ftxui::screen)
endif()
if(MSVC)
    target_compile_options(myvoice_cli PRIVATE /utf-8)
endif()

include(CTest)
if(BUILD_TESTING AND MYVOICE_BUILD_TESTS)
    add_executable(myvoice_shell_tests tests/myvoice_shell_tests.cpp)
    target_link_libraries(myvoice_shell_tests PRIVATE myvoice::core)
    add_test(NAME myvoice_shell_commands COMMAND myvoice_shell_tests)
endif()
if(BUILD_TESTING AND MYVOICE_BUILD_TESTS)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_executable(myvoice_tests tests/myvoice_tests.cpp)
        target_compile_features(myvoice_tests PRIVATE cxx_std_17)
        target_link_libraries(myvoice_tests PRIVATE myvoice::core)
        add_test(NAME myvoice_integration
            COMMAND myvoice_tests "${Python3_EXECUTABLE}")
        set_tests_properties(myvoice_integration PROPERTIES TIMEOUT 20)
        add_test(NAME myvoice_cli_help COMMAND myvoice_cli /help)
        add_test(NAME myvoice_cli_show COMMAND myvoice_cli /engine singing /show)
    else()
        message(STATUS "Python3 not found; mock inference integration test skipped")
    endif()
endif()

install(TARGETS myvoice_core myvoice_cli EXPORT myvoiceTargets)
install(DIRECTORY include/ DESTINATION include)
install(EXPORT myvoiceTargets NAMESPACE myvoice:: DESTINATION lib/cmake/myvoice)
