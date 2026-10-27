#include "taskbar.h"

#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <filesystem>

namespace op {

namespace {

bool g_hidden = false;

std::wstring StatePath() {
    wchar_t home[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
    std::wstring base = (n > 0 && n < MAX_PATH) ? home : L".";
    return base + L"\\.config\\objectpulse\\taskbar.state";
}

bool ReadSaved(UINT& state) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, StatePath().c_str(), L"r") != 0 || !f) return false;
    unsigned value = 0;
    bool ok = fwscanf_s(f, L"%u", &value) == 1;
    fclose(f);
    state = value;
    return ok;
}

void WriteSaved(UINT state) {
    const std::wstring path = StatePath();
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"w") != 0 || !f) return;
    fwprintf(f, L"%u\n", state);
    fclose(f);
}

std::vector<HWND> TrayWindows() {
    std::vector<HWND> list;
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            wchar_t cls[32] = {};
            GetClassNameW(h, cls, 32);
            if (!wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Shell_SecondaryTrayWnd")) {
                reinterpret_cast<std::vector<HWND>*>(lp)->push_back(h);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&list));
    return list;
}

UINT GetState() {
    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    return (UINT)SHAppBarMessage(ABM_GETSTATE, &abd);
}

void SetState(UINT state) {
    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    abd.hWnd = FindWindowW(L"Shell_TrayWnd", nullptr);
    abd.lParam = state;
    SHAppBarMessage(ABM_SETSTATE, &abd);
}

void ShowTrays(int cmd) {
    for (HWND h : TrayWindows()) {
        if ((cmd == SW_HIDE) == (IsWindowVisible(h) != FALSE)) ShowWindow(h, cmd);
    }
}

std::wstring ProcessPath(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    std::wstring result;
    if (QueryFullProcessImageNameW(process, 0, path, &size)) result.assign(path, size);
    CloseHandle(process);
    return result;
}

bool IsTaskWindow(HWND h) {
    if (!IsWindowVisible(h)) return false;
    const LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if (!(ex & WS_EX_APPWINDOW) && (GetWindow(h, GW_OWNER) || (ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)))) {
        return false;
    }
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return false;
    if (GetWindowTextLengthW(h) == 0) return false;
    wchar_t cls[64] = {};
    GetClassNameW(h, cls, 64);
    return wcscmp(cls, L"Progman") && wcscmp(cls, L"WorkerW") && wcscmp(cls, L"Shell_TrayWnd") &&
           wcscmp(cls, L"Shell_SecondaryTrayWnd");
}

std::wstring WindowTitle(HWND h) {
    wchar_t buf[256];
    int n = GetWindowTextW(h, buf, 256);
    return std::wstring(buf, std::max(n, 0));
}

}  // namespace

namespace taskbar {

void Hide() {
    if (!g_hidden) {
        UINT original = 0;
        // A saved state left by a crash holds the user's real setting, not our auto-hide.
        if (!ReadSaved(original)) {
            original = GetState();
            WriteSaved(original);
        }
        g_hidden = true;
    }
    Reapply();
}

void Reapply() {
    if (!g_hidden) return;
    const UINT state = GetState();
    if (!(state & ABS_AUTOHIDE)) SetState(state | ABS_AUTOHIDE);
    ShowTrays(SW_HIDE);
}

void Restore() {
    if (!g_hidden) return;
    g_hidden = false;
    RestoreSaved();
    ShowTrays(SW_SHOWNA);
}

bool RestoreSaved() {
    UINT original = 0;
    if (!ReadSaved(original)) return false;
    SetState(original);
    ShowTrays(SW_SHOWNA);
    DeleteFileW(StatePath().c_str());
    return true;
}

}  // namespace taskbar

bool TaskList::Refresh() {
    struct Ctx {
        HMONITOR monitor;
        std::vector<HWND> found;
    } ctx{monitor_, {}};
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            if (IsTaskWindow(h) && (!c->monitor || MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST) == c->monitor)) {
                c->found.push_back(h);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));

    bool changed = false;
    const HWND top = ctx.found.empty() ? nullptr : ctx.found.front();
    if (top != top_) {
        top_ = top;
        changed = true;
    }
    std::vector<Task> next;
    next.reserve(ctx.found.size());
    for (Task& t : items_) {
        if (std::find(ctx.found.begin(), ctx.found.end(), t.hwnd) == ctx.found.end()) {
            changed = true;
            continue;
        }
        std::wstring title = WindowTitle(t.hwnd);
        if (title != t.title) {
            t.title = std::move(title);
            changed = true;
        }
        next.push_back(std::move(t));
    }
    // EnumWindows is in z-order; new windows go to the end in that order.
    for (HWND h : ctx.found) {
        if (std::any_of(next.begin(), next.end(), [h](const Task& t) { return t.hwnd == h; })) continue;
        Task t;
        t.hwnd = h;
        t.title = WindowTitle(h);
        t.exePath = ProcessPath(h);
        size_t slash = t.exePath.find_last_of(L"\\/");
        t.exe = slash == std::wstring::npos ? t.exePath : t.exePath.substr(slash + 1);
        next.push_back(std::move(t));
        changed = true;
    }
    items_ = std::move(next);
    return changed;
}

void TaskList::SetAttention(HWND hwnd, bool on) {
    for (Task& t : items_) {
        if (t.hwnd == hwnd) t.attention = on;
    }
}

void ActivateTask(HWND hwnd) {
    if (!IsWindow(hwnd)) return;
    if (IsIconic(hwnd)) {
        ShowWindowAsync(hwnd, SW_RESTORE);
    } else if (GetForegroundWindow() == hwnd) {
        ShowWindowAsync(hwnd, SW_MINIMIZE);
        return;
    }
    // We just received the click, so Windows normally lets us hand the foreground over.
    if (SetForegroundWindow(hwnd)) return;
    // An Alt tap lifts the foreground lock when it doesn't.
    INPUT alt[2] = {};
    alt[0].type = alt[1].type = INPUT_KEYBOARD;
    alt[0].ki.wVk = alt[1].ki.wVk = VK_MENU;
    alt[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, alt, sizeof(INPUT));
    SetForegroundWindow(hwnd);
}

std::vector<std::wstring> LoadPins(const std::wstring& path) {
    std::vector<std::wstring> pins;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r, ccs=UTF-8") != 0 || !f) return pins;
    wchar_t buf[1024];
    while (fgetws(buf, 1024, f)) {
        std::wstring line = Trim(buf);
        if (line.empty() || line[0] == L'#') continue;
        if (std::none_of(pins.begin(), pins.end(), [&](const std::wstring& p) { return SamePath(p, line); })) {
            pins.push_back(std::move(line));
        }
    }
    fclose(f);
    return pins;
}

bool SavePins(const std::wstring& path, const std::vector<std::wstring>& pins) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"w, ccs=UTF-8") != 0 || !f) return false;
    fputws(L"# ObjectPulse のピン留め (1 行に 1 つ、exe のパス。上から順に並びます)\n", f);
    for (const std::wstring& p : pins) {
        fputws(p.c_str(), f);
        fputws(L"\n", f);
    }
    fclose(f);
    return true;
}

bool SamePath(const std::wstring& a, const std::wstring& b) {
    return !a.empty() && CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool CanPin(const std::wstring& exePath) {
    if (exePath.empty()) return false;
    const std::wstring lower = ToLower(exePath);
    return lower.find(L"\\windowsapps\\") == std::wstring::npos &&
           lower.find(L"\\systemapps\\") == std::wstring::npos &&
           !(lower.size() >= 24 && lower.compare(lower.size() - 24, 24, L"applicationframehost.exe") == 0);
}

void LaunchApp(const std::wstring& exePath) {
    const std::wstring dir = std::filesystem::path(exePath).parent_path().wstring();
    ShellExecuteW(nullptr, L"open", exePath.c_str(), nullptr, dir.empty() ? nullptr : dir.c_str(), SW_SHOWNORMAL);
}

std::wstring DisplayName(const std::wstring& exePath) {
    std::wstring name = std::filesystem::path(exePath).stem().wstring();
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(exePath.c_str(), &handle);
    if (size == 0) return name;
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(exePath.c_str(), 0, size, data.data())) return name;
    struct Translation {
        WORD language, codepage;
    }* tr = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&tr), &len) ||
        len < sizeof(Translation)) {
        return name;
    }
    wchar_t key[64];
    swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\FileDescription", tr->language, tr->codepage);
    wchar_t* text = nullptr;
    if (VerQueryValueW(data.data(), key, reinterpret_cast<void**>(&text), &len) && text && len > 1) {
        std::wstring desc = Trim(text);
        if (!desc.empty()) return desc;
    }
    return name;
}

}  // namespace op
