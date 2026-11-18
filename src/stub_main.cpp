// stub_main.cpp — cursorwrap.exe: the starter, without Qt.
//
// The plugin's windows are Qt Quick, and Win顺 has Qt where it is installed:
// the plugin uses that copy rather than bring its own (some 40 MB more to
// download). This starter, without Qt and with the C runtime linked in,
// adds Win顺's folder (WS_APP_DIR, set by the plugin host) to the folders
// DLLs are looked for in and loads CursorWrapApp.dll from its own folder:
// that one imports Qt, which the loader then finds there. Nothing else is
// searched (no PATH, no current folder). Vendored from WinShun's
// src/capture-plugin/stub/main.cpp (the capture plugin), with the DLL and
// entry names changed.
//
// The pointer hook, the engine and the protocol live in the DLL; nothing
// here runs after the load but the entry call.
#include <windows.h>

#include <cstdio>
#include <string>
#include <string_view>

namespace {

using Entry = int(__cdecl*)(const wchar_t* appDir);

int fail(int code, const char* what, DWORD error = 0)
{
    // stderr goes to plugins.log; stdout is the protocol's alone.
    if (error)
        std::fprintf(stderr, "cursorwrap: %s (error %lu)\n", what, error);
    else
        std::fprintf(stderr, "cursorwrap: %s\n", what);
    return code;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    // Per-monitor DPI before anything builds a window: EnumDisplayMonitors,
    // the low-level hook's coordinates and SetCursorPos must share one
    // physical-pixel space (was in main.cpp before the split).
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    std::wstring appDir;
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view argument(argv[i]);
        if (argument == L"--app-dir" && i + 1 < argc)
            appDir = argv[++i];
    }
    if (appDir.empty()) {
        appDir.resize(32768);
        const DWORD length = ::GetEnvironmentVariableW(L"WS_APP_DIR", appDir.data(), DWORD(appDir.size()));
        if (!length || length >= appDir.size()) {
            return fail(2, "WS_APP_DIR is required");
        }
        appDir.resize(length);
    }
    ::SetEnvironmentVariableW(L"WS_APP_DIR", appDir.c_str());
    if (!::SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS) || !::AddDllDirectory(appDir.c_str()))
        return fail(3, "cannot set the DLL search folders");
    std::wstring path(32768, L'\0');
    const DWORD length = ::GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (!length || length >= path.size())
        return fail(4, "cannot locate this program");
    path.resize(length);
    path.resize(path.find_last_of(L"\\/") + 1);
    path += L"CursorWrapApp.dll";
    const HMODULE module = ::LoadLibraryExW(path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
    if (!module)
        return fail(5, "cannot load CursorWrapApp.dll", ::GetLastError());
    const Entry entry = reinterpret_cast<Entry>(::GetProcAddress(module, "winshunCursorWrapMain"));
    if (!entry)
        return fail(6, "CursorWrapApp.dll has no winshunCursorWrapMain");
    return entry(appDir.c_str());
}
