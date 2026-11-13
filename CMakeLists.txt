cmake_minimum_required(VERSION 3.16)
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
    # A published plugin must run on machines without a MinGW toolchain:
    # link libstdc++/libgcc/winpthread into cursorwrap.exe.
    add_link_options(-static -static-libgcc -static-libstdc++)
endif()

add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    WINVER=0x0A00 _WIN32_WINNT=0x0A00 CRT_SECURE_NO_WARNINGS)

add_executable(WinShunCursorWrap WIN32
    src/main.cpp
    src/wrap_topology.cpp
    src/wrap_logic.cpp
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
if(MSVC)
    target_link_options(WinShunCursorWrap PRIVATE /ENTRY:mainCRTStartup)
endif()

target_include_directories(WinShunCursorWrap PRIVATE src)
target_link_libraries(WinShunCursorWrap PRIVATE user32 gdi32)

# The exe must sit next to plugin.json: "runtime.program" is a relative path.
set_target_properties(WinShunCursorWrap PROPERTIES
    OUTPUT_NAME "cursorwrap"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
    SUFFIX ".exe")
foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER "${cfg}" cfg_upper)
    set_target_properties(WinShunCursorWrap PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${cfg_upper} "${CMAKE_SOURCE_DIR}")
endforeach()

enable_testing()

# cw_devshell — the plugin's own engine, overlay and data window, driven
# without Win顺 and without touching the real pointer. Same translation units
# as cursorwrap.exe, so a wrong pixel here is a wrong pixel in the shipped
# plugin. See tools/devshell/devshell.cpp for why it exists.
if(WIN32)
    add_executable(cw_devshell
        tools/devshell/devshell.cpp
        src/wrap_topology.cpp
        src/wrap_logic.cpp
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
