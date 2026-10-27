#include "app.h"

#include "controls.h"
#include "corners.h"
#include "pulse.h"
#include "sides.h"
#include "resource.h"
#include "taskbar.h"

#include <algorithm>
#include <cwchar>

namespace op {

App::~App() = default;

DeviceLevels& App::Devices() { return *devices_; }

namespace {

constexpr UINT WM_APP_STATE = WM_APP + 1;
constexpr UINT WM_APP_RELOAD = WM_APP + 2;
constexpr UINT_PTR kClockTimer = 1;
constexpr UINT_PTR kAliveTimer = 2;
constexpr UINT_PTR kDisplayTimer = 3;
constexpr UINT_PTR kEdgeTimer = 4;
constexpr UINT kEdgeMs = 60;

struct MonitorEntry {
    HMONITOR handle;
    RECT rect;
};

// Left to right, then top to bottom: the same numbering ObjectFrame uses.
std::vector<MonitorEntry> SortedMonitors() {
    std::vector<MonitorEntry> list;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR h, HDC, LPRECT r, LPARAM lp) -> BOOL {
            reinterpret_cast<std::vector<MonitorEntry>*>(lp)->push_back({h, *r});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&list));
    std::sort(list.begin(), list.end(), [](const MonitorEntry& a, const MonitorEntry& b) {
        return a.rect.left != b.rect.left ? a.rect.left < b.rect.left : a.rect.top < b.rect.top;
    });
    return list;
}

Notice MakeNotice(Notice::Badge badge, std::wstring title, std::wstring subtitle = {}) {
    Notice n;
    n.badge = badge;
    n.title = std::move(title);
    n.subtitle = std::move(subtitle);
    return n;
}

Notice ConfigWarning(const std::wstring& warning) {
    Notice n = MakeNotice(Notice::Badge::Cross, L"設定ファイルに問題があります", warning);
    n.warn = true;
    return n;
}

}  // namespace

App& App::Instance() {
    static App instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool App::Init(HINSTANCE instance, const std::wstring& configPath) {
    instance_ = instance;
    configPath_ = configPath;
    devices_ = std::make_unique<DeviceLevels>();
    std::vector<std::wstring> warnings;
    config_ = LoadConfig(configPath_, warnings);
    LoadPinList();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_PULSE), IMAGE_ICON,
                                             GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_PULSE), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    if (!RegisterClassExW(&wc) || !Pulse::RegisterWindowClass(instance)) return false;
    // A hidden top-level window rather than a message-only one, so ObjectFrame's FindWindow sees it
    // and it receives broadcasts such as WM_DISPLAYCHANGE.
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"ObjectPulse", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                            instance, nullptr);
    if (!hwnd_) return false;
    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");
    shellHookMsg_ = RegisterWindowMessageW(L"SHELLHOOK");
    RegisterShellHookWindow(hwnd_);

    UpdateClock();
    UpdateBattery();
    taskbarHidden_ = config_.hideTaskbar;
    ApplyTaskbar();
    BuildPulses();
    if (pulses_.empty()) return false;

    helloFrame_ = FindWindowW(kFrameWindowClass, nullptr);
    if (helloFrame_) helloSent_ = SendFrameCommand(L"pulse");
    SetTimer(hwnd_, kClockTimer, 1000, nullptr);
    SetTimer(hwnd_, kAliveTimer, 3000, nullptr);
    SetTimer(hwnd_, kEdgeTimer, kEdgeMs, nullptr);

    if (!warnings.empty()) {
        ShowNotice(ConfigWarning(warnings.front()), true);
    } else if (config_.autoHide) {
        // Otherwise there would be no sign it started at all.
        ShowNotice(MakeNotice(Notice::Badge::Check, L"ObjectPulse", L"画面上端の中央にマウスを当てると開きます"), true);
    }
    return true;
}

int App::Run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

// Deferred: it destroys the notches, and may be called from one of their menus.
void App::Quit() { PostMessageW(hwnd_, WM_CLOSE, 0, 0); }
void App::Reload() { PostMessageW(hwnd_, WM_APP_RELOAD, 0, 0); }

void App::ApplyCorners(const std::vector<HMONITOR>& monitors) {
    if (!corners_) corners_ = std::make_unique<ScreenCorners>();
    corners_->Apply(instance_, monitors, config_);
}

void App::SetCornersHidden(HMONITOR monitor, bool hidden) {
    if (corners_) corners_->SetHidden(monitor, hidden);
    if (sides_) sides_->SetHidden(monitor, hidden);
}

void App::ApplySides(const std::vector<HMONITOR>& monitors) {
    if (!sides_) sides_ = std::make_unique<SideIslands>();
    sides_->Apply(*this, monitors);
}

void App::RefreshSides() {
    if (sides_) sides_->Refresh();
}

void App::BuildPulses() {
    pulses_.clear();
    const auto monitors = SortedMonitors();
    std::vector<HMONITOR> shown;
    if (config_.monitor == kAllMonitors) {
        const bool own = monitors.size() > 1;
        for (size_t i = 0; i < monitors.size(); ++i) {
            shown.push_back(monitors[i].handle);
            pulses_.push_back(std::make_unique<Pulse>(*this, monitors[i].handle, (int)i + 1, own));
        }
    } else {
        HMONITOR chosen = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
        if (config_.monitor >= 1 && config_.monitor <= (int)monitors.size()) {
            chosen = monitors[config_.monitor - 1].handle;
        }
        int index = 1;
        for (size_t i = 0; i < monitors.size(); ++i) {
            if (monitors[i].handle == chosen) index = (int)i + 1;
        }
        shown.push_back(chosen);
        pulses_.push_back(std::make_unique<Pulse>(*this, chosen, index, false));
    }
    ApplyCorners(shown);
    ApplySides(shown);
    for (auto it = pulses_.begin(); it != pulses_.end();) {
        it = (*it)->Create() ? it + 1 : pulses_.erase(it);
    }
}

void App::ForEachPulse(void (Pulse::*method)()) {
    for (auto& p : pulses_) ((*p).*method)();
}

// ---------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App& self = Instance();
    if (!self.hwnd_) return DefWindowProcW(hwnd, msg, wp, lp);
    return self.Handle(msg, wp, lp);
}

LRESULT App::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == taskbarCreatedMsg_ && msg != 0) {
        // Explorer restarted: it shows the taskbar again and forgot our shell hook and app bars.
        ApplyTaskbar();
        RegisterShellHookWindow(hwnd_);
        ForEachPulse(&Pulse::OnTaskbarCreated);
        return 0;
    }
    if (msg == shellHookMsg_ && msg != 0) {
        for (auto& p : pulses_) p->OnShellHook(wp, reinterpret_cast<HWND>(lp));
        return 0;
    }

    switch (msg) {
        case WM_COPYDATA: {
            auto* cds = reinterpret_cast<COPYDATASTRUCT*>(lp);
            if (!cds || cds->dwData != kStateMagic || !cds->lpData || cds->cbData < sizeof(wchar_t)) return FALSE;
            std::wstring text(static_cast<const wchar_t*>(cds->lpData), cds->cbData / sizeof(wchar_t));
            while (!text.empty() && text.back() == L'\0') text.pop_back();
            // Handled after returning so ObjectFrame isn't kept waiting while we draw.
            pendingStates_.push_back(std::move(text));
            PostMessageW(hwnd_, WM_APP_STATE, 0, 0);
            return TRUE;
        }

        case WM_APP_STATE:
            while (!pendingStates_.empty()) {
                std::wstring text = std::move(pendingStates_.front());
                pendingStates_.pop_front();
                OnFrameState(text);
            }
            return 0;

        case WM_APP_RELOAD: {
            std::vector<std::wstring> warnings;
            const bool hideTaskbarBefore = config_.hideTaskbar;
            config_ = LoadConfig(configPath_, warnings);
            LoadPinList();
            if (config_.hideTaskbar != hideTaskbarBefore) {
                taskbarHidden_ = config_.hideTaskbar;
                ApplyTaskbar();
            }
            UpdateClock();
            BuildPulses();
            if (warnings.empty()) {
                ShowNotice(MakeNotice(Notice::Badge::Check, L"設定を再読み込みしました", L"ObjectPulse"), true);
            } else {
                ShowNotice(ConfigWarning(warnings.front()), true);
            }
            return 0;
        }

        case WM_TIMER:
            switch (wp) {
                case kClockTimer: {
                    const bool changed = UpdateClock() | UpdateBattery();
                    if (taskbarHidden_) taskbar::Reapply();
                    for (auto& p : pulses_) {
                        if (changed) p->OnStateChanged();
                        p->OnClockTick();
                    }
                    RefreshSides();
                    break;
                }
                case kAliveTimer: {
                    HWND frame = FindWindowW(kFrameWindowClass, nullptr);
                    if (state_.online && !frame) OnFrameOffline();
                    // Once per ObjectFrame instance; a version without ObjectPulse support would just log it.
                    if (!state_.online && frame && frame != helloFrame_) {
                        helloFrame_ = frame;
                        SendFrameCommand(L"pulse");
                    }
                    break;
                }
                case kDisplayTimer:
                    KillTimer(hwnd_, kDisplayTimer);
                    BuildPulses();
                    break;
                case kEdgeTimer:
                    ForEachPulse(&Pulse::CheckEdge);
                    if (sides_) sides_->CheckHover();
                    break;
            }
            return 0;

        case WM_DISPLAYCHANGE:
            // Monitors come and go in bursts; rebuild once they settle.
            SetTimer(hwnd_, kDisplayTimer, 500, nullptr);
            return 0;

        case WM_POWERBROADCAST:
            if (wp == PBT_APMPOWERSTATUSCHANGE && UpdateBattery()) ForEachPulse(&Pulse::OnStateChanged);
            return TRUE;

        case WM_ENDSESSION:
            if (wp) {
                pulses_.clear();
                taskbar::Restore();
            }
            return 0;

        case WM_CLOSE:
            DestroyWindow(hwnd_);
            return 0;

        case WM_DESTROY:
            pulses_.clear();
            corners_.reset();
            sides_.reset();
            DeregisterShellHookWindow(hwnd_);
            taskbar::Restore();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// ObjectFrame
// ---------------------------------------------------------------------------

void App::OnFrameState(const std::wstring& text) {
    FrameState next = ParseFrameState(text);
    if (!next.online) return OnFrameOffline();
    FrameState before = std::move(state_);
    state_ = std::move(next);
    ForEachPulse(&Pulse::OnStateChanged);
    if (!before.online) {
        if (!helloSent_) {
            ShowNotice(MakeNotice(Notice::Badge::Check, L"ObjectFrame に接続しました",
                                  L"ワークスペース " + std::to_wstring(state_.workspace)));
        }
        helloSent_ = false;
    } else {
        DetectNotice(before, state_);
    }
}

void App::OnFrameOffline() {
    if (state_.online) {
        state_ = FrameState{};
        ShowNotice(MakeNotice(Notice::Badge::Cross, L"ObjectFrame が終了しました"));
    }
    ForEachPulse(&Pulse::OnStateChanged);
}

void App::DetectNotice(const FrameState& a, const FrameState& b) {
    const bool sameWindow = b.hwnd && a.hwnd == b.hwnd;
    const std::wstring app = AppName(b.exe);
    if (b.event == L"reload") {
        ShowNotice(MakeNotice(Notice::Badge::Check, L"設定を再読み込みしました", L"ObjectFrame"));
    } else if (a.paused != b.paused) {
        Notice n = b.paused ? MakeNotice(Notice::Badge::Pause, L"一時停止中", L"ObjectFrame はウィンドウを操作しません")
                            : MakeNotice(Notice::Badge::Play, L"再開しました", L"ObjectFrame");
        n.warn = b.paused;
        ShowNotice(n);
    } else if (a.workspace != b.workspace || a.monitor != b.monitor) {
        // The compact notch already shows the workspace. Don't pop an island for a
        // workspace change or for the pointer moving onto another monitor.
    } else if (a.layout != b.layout) {
        Notice n = MakeNotice(Notice::Badge::Layout, LayoutLabel(b.layout),
                              L"ワークスペース " + std::to_wstring(b.workspace) + L" のレイアウト");
        n.layout = b.layout;
        ShowNotice(n);
    } else if (sameWindow && a.managed && b.managed && a.floating != b.floating) {
        ShowNotice(MakeNotice(b.floating ? Notice::Badge::Float : Notice::Badge::Layout,
                              b.floating ? L"フローティング" : L"タイルに戻しました", app));
    } else if (sameWindow && a.fullscreen != b.fullscreen) {
        ShowNotice(MakeNotice(Notice::Badge::Fullscreen, b.fullscreen ? L"全画面" : L"全画面を解除", app));
    } else if (config_.notifyFocus && b.hwnd && a.hwnd != b.hwnd) {
        Notice n = MakeNotice(Notice::Badge::Window, b.title.empty() ? app : b.title, app);
        n.hwnd = b.hwnd;
        ShowNotice(n);
    }
}

void App::ShowNotice(const Notice& notice, bool always) {
    if (pulses_.empty()) return;
    Pulse* target = nullptr;
    const HMONITOR primary = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    const auto monitors = SortedMonitors();
    for (auto& p : pulses_) {
        const int i = p->Index() - 1;
        const bool focused = state_.online && p->Index() == state_.monitor;
        const bool isPrimary = !state_.online && i >= 0 && i < (int)monitors.size() && monitors[i].handle == primary;
        if (focused || isPrimary) target = p.get();
    }
    (target ? target : pulses_.front().get())->ShowNotice(notice, always);
}

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------

COLORREF App::Accent() const {
    return config_.accentFromFrame && state_.online && state_.hasAccent ? state_.accent : config_.accent;
}

bool App::UpdateClock() {
    static const wchar_t* const kDays[] = {L"日", L"月", L"火", L"水", L"木", L"金", L"土"};
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t clock[32], date[32];
    if (config_.clockSeconds) swprintf_s(clock, L"%d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    else swprintf_s(clock, L"%d:%02d", st.wHour, st.wMinute);
    swprintf_s(date, L"%d/%d (%s)", st.wMonth, st.wDay, kDays[st.wDayOfWeek % 7]);
    if (clock_ == clock && date_ == date) return false;
    clock_ = clock;
    date_ = date;
    return true;
}

bool App::UpdateBattery() {
    SYSTEM_POWER_STATUS ps{};
    Battery b;
    if (GetSystemPowerStatus(&ps) && !(ps.BatteryFlag & 128) && ps.BatteryFlag != 255 && ps.BatteryLifePercent <= 100) {
        b.present = true;
        b.percent = ps.BatteryLifePercent;
        b.charging = ps.ACLineStatus == 1;
    }
    if (b.present == battery_.present && b.percent == battery_.percent && b.charging == battery_.charging) {
        return false;
    }
    battery_ = b;
    return true;
}

void App::ToggleTaskbar() {
    taskbarHidden_ = !taskbarHidden_;
    ApplyTaskbar();
}

void App::ApplyTaskbar() {
    if (taskbarHidden_) taskbar::Hide();
    else taskbar::Restore();
}

// ---------------------------------------------------------------------------
// Pins and shown items
// ---------------------------------------------------------------------------

std::wstring App::PinsPath() const {
    const size_t slash = configPath_.find_last_of(L"\\/");
    return (slash == std::wstring::npos ? std::wstring() : configPath_.substr(0, slash + 1)) + L"pins.txt";
}

void App::LoadPinList() {
    pins_.clear();
    for (std::wstring& path : LoadPins(PinsPath())) {
        std::wstring name = DisplayName(path);
        pins_.push_back({std::move(path), std::move(name)});
    }
}

void App::SavePinList() {
    std::vector<std::wstring> paths;
    for (const Pin& p : pins_) paths.push_back(p.path);
    SavePins(PinsPath(), paths);
    ForEachPulse(&Pulse::OnStateChanged);
}

bool App::IsPinned(const std::wstring& exePath) const {
    return std::any_of(pins_.begin(), pins_.end(), [&](const Pin& p) { return SamePath(p.path, exePath); });
}

void App::TogglePin(const std::wstring& exePath) {
    auto it = std::find_if(pins_.begin(), pins_.end(), [&](const Pin& p) { return SamePath(p.path, exePath); });
    if (it != pins_.end()) {
        pins_.erase(it);
    } else {
        if (!CanPin(exePath)) return;
        pins_.push_back({exePath, DisplayName(exePath)});
    }
    SavePinList();
}

void App::MovePin(const std::wstring& exePath, int delta) {
    auto it = std::find_if(pins_.begin(), pins_.end(), [&](const Pin& p) { return SamePath(p.path, exePath); });
    if (it == pins_.end()) return;
    const ptrdiff_t from = it - pins_.begin(), to = from + delta;
    if (to < 0 || to >= (ptrdiff_t)pins_.size()) return;
    std::swap(pins_[from], pins_[to]);
    SavePinList();
}

void App::ToggleItem(bool expanded, const std::wstring& item) {
    std::vector<std::wstring>& items = expanded ? config_.expandedItems : config_.compactItems;
    items = op::ToggleItem(items, expanded ? kExpandedItems : kCompactItems, item);
    SaveConfigValue(configPath_, expanded ? L"expanded_items" : L"compact_items", JoinItems(items));
    ForEachPulse(&Pulse::OnStateChanged);
}

void App::ToggleClockSeconds() {
    config_.clockSeconds = !config_.clockSeconds;
    SaveConfigValue(configPath_, L"clock_seconds", config_.clockSeconds ? L"true" : L"false");
    UpdateClock();
    ForEachPulse(&Pulse::OnStateChanged);
}

}  // namespace op
