cmake_minimum_required(VERSION 3.21)
project(WinShunCursorWrap CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "WinShunCursorWrap builds on Windows only")
endif()

if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive-)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    add_link_options(-static -static-libgcc -static-libstdc++)
endif()

add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    WINVER=0x0A00 _WIN32_WINNT=0x0A00 CRT_SECURE_NO_WARNINGS)

option(CW_BUILD_PLUGIN "Build the shared-Qt plugin" ON)
if(CW_BUILD_PLUGIN)
    if(NOT MSVC)
        message(FATAL_ERROR "The plugin needs the MSVC Qt kit used by WinShun. Use CW_BUILD_PLUGIN=OFF for core tests with another compiler.")
    endif()
    find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Gui Quick)
    qt_policy(SET QTP0001 NEW)
    qt_policy(SET QTP0004 NEW)

    add_executable(WinShunCursorWrap WIN32 src/stub_main.cpp cursorwrap.rc)
    target_link_options(WinShunCursorWrap PRIVATE /ENTRY:wmainCRTStartup)
    set_target_properties(WinShunCursorWrap PROPERTIES OUTPUT_NAME cursorwrap)

    set(ws_vendor "${CMAKE_CURRENT_SOURCE_DIR}/third_party/winshun")
    set(ws_app "${ws_vendor}/src/app")
    set(ws_qml "${ws_app}/qml")
    set(tuning_qml qml/TuningWindow.qml)
    set_source_files_properties(qml/TuningWindow.qml PROPERTIES QT_RESOURCE_ALIAS TuningWindow.qml)
    set_source_files_properties("${ws_qml}/Theme.qml" PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    foreach(name Theme UiScale Hook Glyph Caret TitleBar FlatButton InputBox ToggleSwitch)
        set(file "${ws_qml}/${name}.qml")
        set_source_files_properties("${file}" PROPERTIES QT_RESOURCE_ALIAS "${name}.qml")
        list(APPEND tuning_qml "${file}")
    endforeach()
    foreach(name Look InputField KeySurface Toggle)
        set(file "${ws_qml}/themes/fluent/${name}.qml")
        set_source_files_properties("${file}" PROPERTIES
            QT_RESOURCE_ALIAS "themes/fluent/${name}.qml")
        list(APPEND tuning_qml "${file}")
    endforeach()

    function(add_tuning_ui target directory)
        set_target_properties(${target} PROPERTIES
            AUTOMOC ON
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
            QT_QMLCACHEGEN_ARGUMENTS "--only-bytecode")
        target_compile_features(${target} PRIVATE cxx_std_20)
        qt_add_qml_module(${target} URI WinShun NO_PLUGIN
            OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${directory}/WinShun"
            QML_FILES ${tuning_qml}
            SOURCES
                "${ws_app}/SystemTheme.cpp" "${ws_app}/SystemTheme.h"
                "${ws_app}/Phosphor.cpp" "${ws_app}/PhosphorGlyph.cpp"
                "${ws_app}/PhosphorGlyph.h"
                "${ws_app}/WindowFrame.cpp" "${ws_app}/WindowFrame.h"
                "${ws_app}/platform/WindowEffects.cpp" "${ws_app}/platform/Foreground.cpp")
        target_include_directories(${target} PRIVATE "${ws_app}" "${ws_vendor}/src/core")
        qt_add_resources(${target} phosphor
            PREFIX /fonts BASE "${ws_vendor}/third_party/phosphor"
            FILES "${ws_vendor}/third_party/phosphor/Phosphor.ttf"
                  "${ws_vendor}/third_party/phosphor/Phosphor-Fill.ttf")
        target_link_libraries(${target} PRIVATE Qt6::Quick user32 dwmapi comctl32)
    endfunction()

    set(tuning_sources
        src/theme_bridge.cpp
        src/theme_bridge.h
        src/tuning_window_qt.cpp
        src/tuning_window_qt.h
    )
    qt_add_library(CursorWrapApp SHARED
        src/app_main.cpp
        src/main_plugin.cpp
        ${tuning_sources}
        src/wrap_topology.cpp
        src/wrap_logic.cpp
        src/arrival_hist.cpp
        src/query_parser.cpp
        src/settings_store.cpp
        src/mouse_hook.cpp
        src/monitor_watcher.cpp
        src/overlay.cpp
        src/readout_layout.cpp
        src/impact_flash.cpp
        src/data_window.cpp
        cursorwrap.rc
    )
    add_tuning_ui(CursorWrapApp qml)
    target_link_libraries(CursorWrapApp PRIVATE gdi32)
    find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS Test)
    add_executable(cw_tuning_test
        tests/test_tuning_window.cpp ${tuning_sources}
        src/settings_store.cpp src/wrap_logic.cpp src/wrap_topology.cpp
    )
    add_tuning_ui(cw_tuning_test testqml)
    target_include_directories(cw_tuning_test PRIVATE src)
    target_link_libraries(cw_tuning_test PRIVATE Qt6::Test)
    add_test(NAME cw_tuning_test COMMAND cw_tuning_test)
    set_tests_properties(cw_tuning_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    set_target_properties(WinShunCursorWrap CursorWrapApp PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugin/$<CONFIG>")
endif()

enable_testing()

# cw_devshell — the plugin's own engine, overlay and data window, driven
# without Win顺 and without touching the real pointer. Same translation units
# as the shipped DLL, so a wrong pixel here is a wrong pixel in the shipped
# plugin. See tools/devshell/devshell.cpp for why it exists.
if(WIN32)
    add_executable(cw_devshell
        tools/devshell/devshell.cpp
        src/wrap_topology.cpp
        src/wrap_logic.cpp
        src/arrival_hist.cpp
        src/settings_store.cpp
        src/overlay.cpp
        src/readout_layout.cpp
        src/impact_flash.cpp
        src/data_window.cpp
    )
    target_include_directories(cw_devshell PRIVATE src)
    target_link_libraries(cw_devshell PRIVATE user32 gdi32)
    set_target_properties(cw_devshell PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build")
    foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER "${cfg}" cfg_upper)
        set_target_properties(cw_devshell PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${cfg_upper} "${CMAKE_SOURCE_DIR}/build")
    endforeach()
endif()

add_executable(cw_tests
    tests/test_main.cpp
    tests/test_wrap_topology.cpp
    tests/test_wrap_logic.cpp
    tests/test_edge_cases.cpp
    tests/test_impact_gate.cpp
    tests/test_tuner.cpp
    tests/test_query_parser.cpp
    tests/test_settings_store.cpp
    tests/test_settings_roundtrip.cpp
    tests/test_hook_latency.cpp
    tests/test_guard_rearm.cpp
    tests/test_readout_layout.cpp
    tests/test_impact_flash.cpp
    tests/test_arrival_hist.cpp
    src/arrival_hist.cpp
    src/readout_layout.cpp
    src/impact_flash.cpp
    src/wrap_topology.cpp
    src/wrap_logic.cpp
    src/query_parser.cpp
    src/settings_store.cpp
)
target_include_directories(cw_tests PRIVATE src tests)
target_compile_definitions(cw_tests PRIVATE UNICODE _UNICODE NOMINMAX
    WIN32_LEAN_AND_MEAN CRT_SECURE_NO_WARNINGS)
target_link_libraries(cw_tests PRIVATE user32)
add_test(NAME cw_tests COMMAND cw_tests "${CMAKE_SOURCE_DIR}/tests/layouts")
