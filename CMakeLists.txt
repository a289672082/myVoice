cmake_minimum_required(VERSION 3.20)
project(myVoice VERSION 0.1.0 LANGUAGES CXX)

option(MYVOICE_WITH_FTXUI "Build the FTXUI interactive console" ON)
option(MYVOICE_BUILD_TESTS "Build mock-backend integration tests" ON)

add_library(myvoice_core STATIC
    src/Process.cpp
    src/CommandShell.cpp
    src/VoiceConverter.cpp
    src/VoiceTrainer.cpp
)
add_library(myvoice::core ALIAS myvoice_core)
# 工程源码目录可通过 VS2022 任意工作目录启动；安装部署时可用 /train-script 覆盖。
target_compile_definitions(myvoice_core PRIVATE
    MYVOICE_TRAIN_BRIDGE_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/scripts/rvc_train.py"
    MYVOICE_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
)
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
    # 只从仓库本地编译；禁止网络 FetchContent 或外部 find_package。
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ftxui/include/ftxui/component/app.hpp")
        message(FATAL_ERROR "Vendored FTXUI missing: third_party/ftxui. Clone the complete myVoice repository.")
    endif()
    add_subdirectory(third_party/ftxui)
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
        add_test(NAME myvoice_cli_seed_help COMMAND myvoice_cli /Seed-VC /help)
        add_test(NAME myvoice_cli_rvc_help COMMAND myvoice_cli /RVC /help)
        add_test(NAME myvoice_cli_show COMMAND myvoice_cli /Seed-VC /engine singing /show)
        add_test(NAME myvoice_rvc_training_mock
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_rvc_train.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/rvc_train.py")
        set_tests_properties(myvoice_rvc_training_mock PROPERTIES TIMEOUT 30)
    else()
        message(STATUS "Python3 not found; mock inference integration test skipped")
    endif()
endif()

install(TARGETS myvoice_core myvoice_cli EXPORT myvoiceTargets)
install(DIRECTORY include/ DESTINATION include)
install(EXPORT myvoiceTargets NAMESPACE myvoice:: DESTINATION lib/cmake/myvoice)
