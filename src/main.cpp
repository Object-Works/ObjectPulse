#include "app.h"
#include "common.h"
#include "config.h"
#include "taskbar.h"
#include "version.h"

#include <objbase.h>
#include <shellapi.h>

#include <string>

namespace {

void PrintToParentConsole(const std::wstring& text) {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    std::wstring line = text + L"\n";
    WriteConsoleW(out, line.c_str(), (DWORD)line.size(), &written, nullptr);
    FreeConsole();
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    std::wstring configPath = op::DefaultConfigPath();
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"quit" || a == L"--quit") {
            if (HWND running = FindWindowW(op::kWindowClass, nullptr)) PostMessageW(running, WM_CLOSE, 0, 0);
            return 0;
        }
        if (a == L"reload") {
            // Same message App handles as WM_APP_RELOAD.
            if (HWND running = FindWindowW(op::kWindowClass, nullptr)) PostMessageW(running, WM_APP + 2, 0, 0);
            return 0;
        }
        if (a == L"restore-taskbar") {
            // The running pulse restores it as it closes; the saved state covers a crashed one.
            if (HWND running = FindWindowW(op::kWindowClass, nullptr)) {
                PostMessageW(running, WM_CLOSE, 0, 0);
                for (int wait = 0; wait < 30 && IsWindow(running); ++wait) Sleep(100);
            }
            op::taskbar::RestoreSaved();
            return 0;
        }
        if ((a == L"--config" || a == L"-c") && i + 1 < argc) {
            configPath = argv[++i];
        } else if (a == L"--version" || a == L"-v") {
            PrintToParentConsole(L"ObjectPulse " OP_VERSION_WSTRING);
            return 0;
        } else if (a == L"--help" || a == L"-h") {
            PrintToParentConsole(L"ObjectPulse " OP_VERSION_WSTRING L" - status notch and taskbar for ObjectFrame\n"
                                 L"  ObjectPulse [--config <path>]  start the pulse\n"
                                 L"  ObjectPulse quit               close the running pulse\n"
                                 L"  ObjectPulse reload             reload pulse.conf in the running pulse\n"
                                 L"  ObjectPulse restore-taskbar    close it and bring the Windows taskbar back\n"
                                 L"  ObjectPulse --version          print the version");
            return 0;
        }
    }

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\ObjectPulse");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;  // e.g. started again by ObjectFrame's exec-once

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    op::EnsureConfigFile(configPath);

    auto& app = op::App::Instance();
    if (!app.Init(instance, configPath)) {
        MessageBoxW(nullptr, L"Failed to initialize ObjectPulse.", L"ObjectPulse", MB_ICONERROR);
        return 1;
    }
    int code = app.Run();

    LocalFree(argv);
    if (mutex) CloseHandle(mutex);
    return code;
}
