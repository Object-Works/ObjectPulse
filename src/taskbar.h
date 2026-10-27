#pragma once

#include "common.h"

#include <string>
#include <vector>

namespace op {

// Hiding the Windows taskbar. It is switched to auto-hide (so the work area covers the whole screen)
// and its windows are hidden. The original auto-hide setting is saved to a file next to the config,
// so it can be restored even after a crash with `ObjectPulse.exe restore-taskbar`.
namespace taskbar {

void Hide();
// Hides it again if Explorer brought it back (restart, Start menu on Windows 10, ...).
void Reapply();
void Restore();
// Restores from the saved file without a running pulse. Returns false if nothing was saved.
bool RestoreSaved();

}  // namespace taskbar

// A window the taskbar would show a button for.
struct Task {
    HWND hwnd = nullptr;
    std::wstring title;
    std::wstring exe;      // file name, e.g. "Code.exe"
    std::wstring exePath;  // full path, for the icon when the window has none
    bool attention = false;  // flashing to get the user's attention
};

// Running windows in first-seen order, like taskbar buttons.
class TaskList {
public:
    // Only windows on `monitor` are listed, unless it is null.
    void SetMonitor(HMONITOR monitor) { monitor_ = monitor; }
    // Returns true if anything visible changed.
    bool Refresh();
    void SetAttention(HWND hwnd, bool on);
    const std::vector<Task>& Items() const { return items_; }
    // The listed window highest in z-order: the "current" one of a monitor without the focus.
    HWND Top() const { return top_; }

private:
    std::vector<Task> items_;
    HMONITOR monitor_ = nullptr;
    HWND top_ = nullptr;
};

// Brings a window to the front, or minimizes it if it already is, like clicking a taskbar button.
void ActivateTask(HWND hwnd);

struct Pin {
    std::wstring path;  // the exe
    std::wstring name;  // DisplayName(path)
};

// Pinned apps are kept as exe paths, one per line, in display order.
std::vector<std::wstring> LoadPins(const std::wstring& path);
bool SavePins(const std::wstring& path, const std::vector<std::wstring>& pins);
bool SamePath(const std::wstring& a, const std::wstring& b);
// Store apps run inside hosts or protected folders, so their exe can't be started by path.
bool CanPin(const std::wstring& exePath);
void LaunchApp(const std::wstring& exePath);
// The exe's file description ("Visual Studio Code"), else its file name without ".exe".
std::wstring DisplayName(const std::wstring& exePath);

}  // namespace op
