#include "frame_link.h"

#include <algorithm>
#include <cwchar>

namespace op {

namespace {

std::vector<bool> ParseList(const std::wstring& value, int count) {
    std::vector<bool> out(std::max(count, 0), false);
    if (value.empty()) return out;
    for (auto& part : Split(value, L',')) {
        int n = _wtoi(part.c_str());
        if (n >= 1 && n <= count) out[n - 1] = true;
    }
    return out;
}

}  // namespace

FrameState ParseFrameState(const std::wstring& text) {
    FrameState s;
    std::wstring occupied, visible;
    for (auto& line : Split(text, L'\n')) {
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring k = line.substr(0, eq), v = line.substr(eq + 1);
        if (!v.empty() && v.back() == L'\r') v.pop_back();
        if (k == L"of") s.online = v == L"1";
        else if (k == L"monitor") s.monitor = _wtoi(v.c_str());
        else if (k == L"monitors") s.monitors = _wtoi(v.c_str());
        else if (k == L"workspace") s.workspace = _wtoi(v.c_str());
        else if (k == L"workspaces") s.workspaces = std::max(0, std::min(_wtoi(v.c_str()), 50));
        else if (k == L"occupied") occupied = v;
        else if (k == L"visible") visible = v;
        else if (k == L"layout") s.layout = v;
        else if (k == L"monitor_workspaces") {
            for (auto& part : Split(v, L',')) s.monitorWorkspaces.push_back(_wtoi(part.c_str()));
        } else if (k == L"monitor_layouts") s.monitorLayouts = Split(v, L',');
        else if (k == L"windows") s.windows = _wtoi(v.c_str());
        else if (k == L"paused") s.paused = v == L"1";
        else if (k == L"managed") s.managed = v == L"1";
        else if (k == L"floating") s.floating = v == L"1";
        else if (k == L"fullscreen") s.fullscreen = v == L"1";
        else if (k == L"hwnd") s.hwnd = (HWND)(UINT_PTR)_wcstoui64(v.c_str(), nullptr, 10);
        else if (k == L"exe") s.exe = v;
        else if (k == L"title") s.title = v;
        else if (k == L"event") s.event = v;
        else if (k == L"accent" && v.size() == 8 && v.rfind(L"0x", 0) == 0) {
            unsigned long rgb = std::wcstoul(v.c_str() + 2, nullptr, 16);
            s.accent = RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
            s.hasAccent = true;
        }
    }
    s.occupied = ParseList(occupied, s.workspaces);
    s.visible = ParseList(visible, s.workspaces);
    return s;
}

FrameState FrameState::ForMonitor(int monitorIndex) const {
    FrameState s = *this;
    const size_t i = (size_t)(monitorIndex - 1);
    if (monitorIndex >= 1 && i < monitorWorkspaces.size() && monitorWorkspaces[i] >= 1) {
        s.workspace = monitorWorkspaces[i];
    }
    if (monitorIndex >= 1 && i < monitorLayouts.size() && !monitorLayouts[i].empty()) s.layout = monitorLayouts[i];
    if (monitorIndex != monitor) {
        // The focused window and its details belong to another monitor.
        s.hwnd = nullptr;
        s.exe.clear();
        s.title.clear();
        s.managed = s.floating = s.fullscreen = false;
        s.windows = -1;  // only reported for the focused monitor
    }
    return s;
}

bool SendFrameCommand(const std::wstring& command) {
    HWND frame = FindWindowW(kFrameWindowClass, nullptr);
    if (!frame) return false;
    std::wstring buf = command;
    COPYDATASTRUCT cds{};
    cds.dwData = kFrameCommandMagic;
    cds.cbData = (DWORD)((buf.size() + 1) * sizeof(wchar_t));
    cds.lpData = buf.data();
    DWORD_PTR result = 0;
    return SendMessageTimeoutW(frame, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds), SMTO_ABORTIFHUNG, 1000,
                               &result) &&
           result;
}

}  // namespace op
