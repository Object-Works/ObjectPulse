#pragma once

#include "common.h"

#include <string>
#include <vector>

namespace op {

// What ObjectFrame reports about the focused monitor and window. Workspace numbers are 1-based.
struct FrameState {
    bool online = false;
    int monitor = 1;
    int monitors = 1;
    int workspace = 1;
    int workspaces = 0;
    std::vector<bool> occupied;  // indexed by workspace - 1
    std::vector<bool> visible;
    std::wstring layout;  // dwindle | master | monocle | scroll
    std::vector<int> monitorWorkspaces;         // active workspace of every monitor, by monitor - 1
    std::vector<std::wstring> monitorLayouts;  // its layout
    int windows = 0;  // on the focused workspace; -1 when unknown
    bool paused = false;
    bool managed = false;
    bool floating = false;
    bool fullscreen = false;
    HWND hwnd = nullptr;  // foreground application window, if any
    std::wstring exe;
    std::wstring title;
    bool hasAccent = false;
    COLORREF accent = 0;
    std::wstring event;  // one-off notice such as "reload"

    bool Occupied(int ws) const { return ws >= 1 && ws <= (int)occupied.size() && occupied[ws - 1]; }
    bool Visible(int ws) const { return ws >= 1 && ws <= (int)visible.size() && visible[ws - 1]; }
    // The state as seen from `monitorIndex` (1-based): its own workspace and layout.
    FrameState ForMonitor(int monitorIndex) const;
};

// Parses the "key=value\n" text ObjectFrame sends with WM_COPYDATA.
FrameState ParseFrameState(const std::wstring& text);

// Runs an ObjectFrame command, e.g. "workspace 3" (same syntax as `ofwm dispatch`).
bool SendFrameCommand(const std::wstring& command);

}  // namespace op
