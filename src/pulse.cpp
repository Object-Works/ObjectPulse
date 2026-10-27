#include "pulse.h"

#include "app.h"
#include "controls.h"
#include "menu.h"
#include "resource.h"

#include <shellapi.h>
#include <shellscalingapi.h>
#include <timeapi.h>
#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwchar>

namespace op {

namespace {

constexpr const wchar_t* kNotchClass = L"ObjectPulseNotch";
constexpr UINT WM_APP_APPBAR = WM_APP + 1;
constexpr UINT_PTR kAnimationTimer = 1;
constexpr UINT_PTR kNoticeTimer = 2;
constexpr UINT_PTR kPollTimer = 3;
constexpr UINT_PTR kPlaceTimer = 4;
constexpr float kEdgeZoneHeight = 3.0f, kEdgeZoneWidth = 320.0f;  // DIPs at the top center of the monitor
constexpr UINT kPollMs = 40;
constexpr double kLeaveDelayMs = 300;
constexpr double kFadeOutMs = 110, kFadeInDelayMs = 70, kFadeInMs = 220;
constexpr float kShadowPad = 26.0f;  // room around the largest view for the shadow and spring overshoot

enum MenuCommand : UINT {
    kMenuPause = 1,
    kMenuRetile,
    kMenuEditConfig,
    kMenuReload,
    kMenuQuit,
    kMenuCloseWindow,
    kMenuTaskManager,
    kMenuToggleTaskbar,
    kMenuLaunch,
    kMenuTogglePin,
    kMenuPinLeft,
    kMenuPinRight,
};

double NowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

float EaseOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    float q = 1.0f - t;
    return 1.0f - q * q * q;
}

// Presses Win plus `key`, which the shell handles even with the taskbar hidden.
void PressWinKey(WORD key) {
    INPUT in[4] = {};
    UINT n = 0;
    auto add = [&](WORD vk, bool up) {
        in[n].type = INPUT_KEYBOARD;
        in[n].ki.wVk = vk;
        in[n].ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
        ++n;
    };
    add(VK_LWIN, false);
    add(key, false);
    add(key, true);
    add(VK_LWIN, true);
    SendInput(n, in, sizeof(INPUT));
}

}  // namespace

void Spring::Step(float dt) {
    constexpr float kStiffness = 420.0f;
    const float damping = 2.0f * std::sqrt(kStiffness) * 0.74f;
    constexpr int kSubsteps = 4;
    const float h = dt / kSubsteps;
    for (int i = 0; i < kSubsteps; ++i) {
        float a = -kStiffness * (value - target) - damping * velocity;
        velocity += a * h;
        value += velocity * h;
    }
    if (std::fabs(value - target) < 0.15f && std::fabs(velocity) < 2.0f) Snap();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

Pulse::Pulse(App& app, HMONITOR monitor, int index, bool ownWindows)
    : app_(app), monitor_(monitor), index_(index), ownWindows_(ownWindows) {}

Pulse::~Pulse() {
    menu_.reset();
    if (!hwnd_) return;
    StopAnimation();
    RemoveAppBar();
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
    DestroyWindow(hwnd_);
}

bool Pulse::RegisterWindowClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kNotchClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_PULSE), IMAGE_ICON,
                                             GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_PULSE), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    return RegisterClassExW(&wc) != 0;
}

bool Pulse::Create() {
    if (!renderer_.Init()) return false;
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(monitor_, &mi);
    // Created on its monitor so it starts out with that monitor's DPI.
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kNotchClass,
                            L"ObjectPulse", WS_POPUP, mi.rcMonitor.left, mi.rcMonitor.top, 1, 1, nullptr, nullptr,
                            app_.Module(), this);
    if (!hwnd_) return false;
    tasks_.SetMonitor(ownWindows_ ? monitor_ : nullptr);
    tasks_.Refresh();
    UpdateView();
    Place();
    // Drops out of the top edge.
    width_.value = kCompactHeight;
    height_.value = 0;
    radius_.value = 0;
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    UpdateVisibility();
    app_.Devices().Refresh(monitor_);
    Refresh();
    StartAnimation();
    return true;
}

// ---------------------------------------------------------------------------
// Events from the app
// ---------------------------------------------------------------------------

void Pulse::OnStateChanged() {
    UpdateView();
    UpdateVisibility();
    Refresh();
}

void Pulse::OnClockTick() {
    // Titles, and anything the shell hook missed. Leave a fader the user is holding alone.
    const bool titles = tasks_.Refresh();
    bool levels = false;
    if (dragging_.empty()) {
        app_.Devices().Flush();
        levels = app_.Devices().Refresh(monitor_);
    }
    if (titles || levels) Refresh();
}

void Pulse::OnShellHook(WPARAM code, HWND hwnd) {
    switch (code) {
        case HSHELL_WINDOWCREATED:
        case HSHELL_WINDOWDESTROYED:
        case HSHELL_REDRAW:
            tasks_.Refresh();
            break;
        case HSHELL_WINDOWACTIVATED:
        case HSHELL_RUDEAPPACTIVATED:
            tasks_.Refresh();
            tasks_.SetAttention(hwnd, false);
            break;
        case HSHELL_FLASH:
            tasks_.Refresh();
            if (hwnd != GetForegroundWindow()) tasks_.SetAttention(hwnd, true);
            break;
        default:
            return;
    }
    Refresh();
}

void Pulse::OnTaskbarCreated() {
    appBar_ = false;
    UpdateAppBar(true);
}

void Pulse::CheckEdge() {
    // A hidden notch has no pixels to receive the mouse, so watch the edge instead.
    if (!polling_ && InEdgeZone()) StartPolling();
}

void Pulse::ShowNotice(Notice notice, bool always) {
    if (!always && (!app_.Settings().notify || hovering_)) return;
    if (notice.badge == Notice::Badge::Layout && notice.layout.empty()) notice.layout = view_.layout;
    const bool replacing = notice_.has_value() && mode_ == Mode::Notice;
    notice_ = std::move(notice);
    SetTimer(hwnd_, kNoticeTimer, app_.Settings().notifyDuration, nullptr);
    if (replacing) {
        // Same shape, new content: just fade the new one in.
        fadePrevious_ = false;
        modeChangedAt_ = NowMs();
    }
    Refresh();
}

// ---------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------

LRESULT CALLBACK Pulse::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<Pulse*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<Pulse*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->Handle(msg, wp, lp);
}

LRESULT Pulse::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER:
            switch (wp) {
                case kAnimationTimer: Tick(); break;
                case kNoticeTimer:
                    KillTimer(hwnd_, kNoticeTimer);
                    lastNotice_ = std::move(notice_);
                    notice_.reset();
                    Refresh();
                    break;
                case kPollTimer: Poll(); break;
                case kPlaceTimer:
                    KillTimer(hwnd_, kPlaceTimer);
                    Place();
                    Refresh();
                    break;
            }
            return 0;

        case WM_MOUSEMOVE: {
            mouse_ = FromClient({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            StartPolling();
            if (!dragging_.empty()) {
                ApplySlider(mouse_.x);
                return 0;
            }
            int hit = HitAt(mouse_);
            if (hit != hoverHit_) {
                hoverHit_ = hit;
                if (!animating_) Render();
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            const D2D1_POINT_2F p = FromClient({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            const int hit = HitAt(p);
            if (hit >= 0 && hits_[hit].command.rfind(L"!slider:", 0) == 0) {
                dragging_ = hits_[hit].command;
                dragTrack_ = hits_[hit].rect;
                SetCapture(hwnd_);
                ApplySlider(p.x);
            }
            return 0;
        }

        case WM_LBUTTONUP:
            if (!dragging_.empty()) {
                EndSlider();
                return 0;
            }
            OnClick(FromClient({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}));
            return 0;

        case WM_RBUTTONDOWN:
            // Capture so the release still arrives if the cursor slips onto a transparent pixel.
            SetCapture(hwnd_);
            StartPolling();
            return 0;

        case WM_RBUTTONUP:
            if (GetCapture() == hwnd_) ReleaseCapture();
            ShowMenu(FromClient({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}));
            return 0;

        case WM_CAPTURECHANGED:
            return 0;

        case WM_MBUTTONUP:
            OnMiddleClick(FromClient({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}));
            return 0;

        case WM_MOUSEWHEEL:
            if (!view_.online) return 0;
            wheel_ += GET_WHEEL_DELTA_WPARAM(wp);
            while (std::abs(wheel_) >= WHEEL_DELTA) {
                RunAction(wheel_ > 0 ? L"workspace prev" : L"workspace next");
                wheel_ += wheel_ > 0 ? -WHEEL_DELTA : WHEEL_DELTA;
            }
            return 0;

        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT && HitAt(mouse_) >= 0) {
                const bool slider = hits_[HitAt(mouse_)].command.rfind(L"!slider:", 0) == 0;
                SetCursor(LoadCursorW(nullptr, slider ? IDC_SIZEWE : IDC_HAND));
                return TRUE;
            }
            break;

        case WM_DPICHANGED:
            // Placement follows the monitor's DPI; the suggested rectangle doesn't fit a top-anchored notch.
            SetTimer(hwnd_, kPlaceTimer, 300, nullptr);
            return 0;

        case WM_APP_APPBAR:
            if (wp == ABN_POSCHANGED) {
                UpdateAppBar(false);
            } else if (wp == ABN_FULLSCREENAPP) {
                // Explorer also reports the desktop as "full screen" after Win+D, so check for ourselves.
                fullscreenApp_ = lp && ForegroundIsFullscreen();
                UpdateVisibility();
            }
            return 0;

        case WM_ACTIVATE:
        case WM_WINDOWPOSCHANGED:
            if (appBar_) {
                APPBARDATA abd{};
                abd.cbSize = sizeof(abd);
                abd.hWnd = hwnd_;
                SHAppBarMessage(msg == WM_ACTIVATE ? ABM_ACTIVATE : ABM_WINDOWPOSCHANGED, &abd);
            }
            break;

        case WM_CLOSE:
            // `ObjectPulse.exe quit` targets the app window; closing a single notch means quitting too.
            app_.Quit();
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void Pulse::RunAction(const std::wstring& command) {
    if (command.empty()) return;
    if (command[0] != L'!') {
        // Workspace and layout buttons act on this notch's monitor.
        const FrameState& s = app_.State();
        const bool perMonitor = command.rfind(L"workspace ", 0) == 0 || command.rfind(L"layout ", 0) == 0;
        if (perMonitor && app_.MultiMonitor() && s.online && s.monitor != index_) {
            SendFrameCommand(L"focusmonitor " + std::to_wstring(index_));
        }
        SendFrameCommand(command);
        return;
    }
    if (HWND target = WindowFromAction(command)) {
        ActivateTask(target);
    } else if (std::wstring pin = PinFromAction(command); !pin.empty()) {
        OpenPinned(pin);
    } else if (command == kTaskViewAction) {
        PressWinKey(VK_TAB);
    } else if (command == kQuickSettingsAction) {
        PressWinKey('A');
    } else if (command == kNotificationsAction) {
        PressWinKey('N');
    }
    // Collapse so the notch doesn't sit expanded over whatever just opened.
    hovering_ = false;
    hoverSuppressed_ = true;
    Refresh();
}

std::vector<HWND> Pulse::WindowsOf(const std::wstring& exePath) const {
    std::vector<HWND> list;
    for (const Task& t : tasks_.Items()) {
        if (SamePath(t.exePath, exePath)) list.push_back(t.hwnd);
    }
    return list;
}

// Like a taskbar button: starts the app, brings its window up, or cycles through its windows.
void Pulse::OpenPinned(const std::wstring& exePath) {
    const std::vector<HWND> windows = WindowsOf(exePath);
    if (windows.empty()) {
        LaunchApp(exePath);
        return;
    }
    auto fg = std::find(windows.begin(), windows.end(), GetForegroundWindow());
    if (fg == windows.end()) ActivateTask(windows.front());
    else if (windows.size() == 1) ActivateTask(*fg);  // minimizes it
    else ActivateTask(++fg == windows.end() ? windows.front() : *fg);
}

// ---------------------------------------------------------------------------
// Presentation
// ---------------------------------------------------------------------------

void Pulse::UpdateView() {
    view_ = app_.MultiMonitor() ? app_.State().ForMonitor(index_) : app_.State();
}

ViewModel Pulse::Model() const {
    ViewModel m;
    m.state = &view_;
    m.notice = notice_ ? &*notice_ : lastNotice_ ? &*lastNotice_ : nullptr;
    m.tasks = &tasks_.Items();
    m.pins = &app_.Pins();
    m.config = &app_.Settings();
    // The active app on this monitor: the foreground window if it's here, else the topmost one.
    HWND fg = GetForegroundWindow();
    const auto& items = tasks_.Items();
    const bool here = std::any_of(items.begin(), items.end(), [fg](const Task& t) { return t.hwnd == fg; });
    m.foreground = here ? fg : tasks_.Top();
    m.battery = app_.BatteryState();
    m.accent = app_.Accent();
    m.background = app_.Settings().background;
    m.clock = app_.Clock();
    m.date = app_.Date();
    m.mouse = mouse_;
    m.volume = app_.Devices().Volume();
    m.volumeMuted = app_.Devices().VolumeMuted();
    m.mic = app_.Devices().Mic();
    m.micMuted = app_.Devices().MicMuted();
    m.hasMic = app_.Devices().HasMic();
    m.brightness = app_.Devices().Brightness(monitor_);
    return m;
}

void Pulse::Refresh() {
    const Config& config = app_.Settings();
    const auto& items = tasks_.Items();
    const bool needed = config.alwaysSmall || !config.autoHide || (view_.online && view_.paused) ||
                        std::any_of(items.begin(), items.end(), [](const Task& t) { return t.attention; });
    const Mode want = hovering_ ? Mode::Expanded : notice_ ? Mode::Notice : needed ? Mode::Compact : Mode::Hidden;
    if (want != mode_) {
        previousMode_ = mode_;
        mode_ = want;
        fadePrevious_ = true;
        modeChangedAt_ = NowMs();
        hits_.clear();
        hoverHit_ = -1;
    }
    const ViewMetrics m = MeasureView(mode_, Model(), renderer_);
    width_.target = m.size.width;
    height_.target = m.size.height;
    radius_.target = m.radius;
    const double fadeDone = (fadePrevious_ ? kFadeInDelayMs : 0.0) + kFadeInMs;
    if (!width_.Settled() || !height_.Settled() || !radius_.Settled() || NowMs() - modeChangedAt_ < fadeDone) {
        StartAnimation();
    } else if (!animating_) {
        Render();
    }
}

void Pulse::Render() {
    if (hidden_ || !renderer_.Begin()) return;
    ID2D1RenderTarget* rt = renderer_.Target();
    const Config& config = app_.Settings();
    const float w = std::max(width_.value, 1.0f), h = std::max(height_.value, 0.0f);
    const float radius = std::clamp(radius_.value, 0.0f, h / 2);
    // The ears shrink with the notch, so it grows out of a flat edge.
    const float ear = std::min(kNotchEar, h * 0.4f);
    pill_ = {(canvasW_ - w) / 2, 0.0f, (canvasW_ + w) / 2, h};

    // Soft shadow under the bottom edge, fading out as the notch tucks away.
    const float presence = std::clamp(h / kCompactHeight, 0.0f, 1.0f);
    for (int i = 0; i < 6 && h >= 0.5f; ++i) {
        const float grow = 1.0f + i * 1.7f, fade = 1.0f - i / 6.0f;
        D2D1_RECT_F s{pill_.left - grow, -radius, pill_.right + grow, pill_.bottom + grow + 1.0f};
        renderer_.FillRound(s, radius + grow, RGB(0, 0, 0), 0.07f * fade * fade * presence);
    }
    ComPtr<ID2D1PathGeometry> clip = h >= 0.5f ? renderer_.NotchPath(pill_, radius, ear) : nullptr;
    if (clip) rt->FillGeometry(clip.Get(), renderer_.Brush(config.background, config.opacity / 100.0f));
    const ViewModel model = Model();
    const double t = NowMs() - modeChangedAt_;
    auto draw = [&](Mode mode, float alpha, std::vector<Hit>* hits) {
        if (alpha <= 0.01f || !clip) return;
        const ViewMetrics m = MeasureView(mode, model, renderer_);
        // Content hangs from the top edge with the notch.
        const float cx = (pill_.left + pill_.right) / 2, cy = pill_.bottom - m.size.height / 2;
        const D2D1_RECT_F area{cx - m.size.width / 2, cy - m.size.height / 2, cx + m.size.width / 2,
                               cy + m.size.height / 2};
        ComPtr<ID2D1Layer> layer;
        if (FAILED(rt->CreateLayer(nullptr, layer.GetAddressOf()))) return;
        rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                            D2D1::IdentityMatrix(), alpha),
                      layer.Get());
        // Content settles into place from slightly smaller as it fades in.
        const float scale = 0.92f + 0.08f * alpha;
        rt->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(cx, cy)));
        DrawView(mode, model, renderer_, area, hits);
        rt->SetTransform(D2D1::Matrix3x2F::Identity());
        rt->PopLayer();
    };
    if (fadePrevious_ && t < kFadeOutMs) draw(previousMode_, 1.0f - (float)(t / kFadeOutMs), nullptr);
    const float alpha = EaseOut((float)((t - (fadePrevious_ ? kFadeInDelayMs : 0.0)) / kFadeInMs));
    hits_.clear();
    // Buttons only become clickable once they have stopped moving.
    draw(mode_, alpha, alpha >= 1.0f ? &hits_ : nullptr);

    if (!renderer_.End()) {
        StartAnimation();  // device lost; the next frame recreates it
        return;
    }
    PresentFrame();
}

void Pulse::Tick() {
    const double now = NowMs();
    const float dt = (float)std::clamp((now - lastTick_) / 1000.0, 0.001, 0.05);
    lastTick_ = now;
    width_.Step(dt);
    height_.Step(dt);
    radius_.Step(dt);
    Render();
    const double fadeDone = (fadePrevious_ ? kFadeInDelayMs : 0.0) + kFadeInMs;
    if (width_.Settled() && height_.Settled() && radius_.Settled() && now - modeChangedAt_ >= fadeDone) {
        StopAnimation();
    }
}

void Pulse::StartAnimation() {
    if (animating_ || !hwnd_) return;
    animating_ = true;
    lastTick_ = NowMs();
    // The default 15.6 ms timer resolution makes the animation visibly choppy.
    timeBeginPeriod(1);
    SetTimer(hwnd_, kAnimationTimer, USER_TIMER_MINIMUM, nullptr);
}

void Pulse::StopAnimation() {
    if (!animating_) return;
    KillTimer(hwnd_, kAnimationTimer);
    timeEndPeriod(1);
    animating_ = false;
}

void Pulse::UpdateVisibility() {
    const bool frameFullscreen = view_.online && view_.fullscreen && view_.monitor == index_;
    const bool hide = app_.Settings().hideOnFullscreen && (fullscreenApp_ || frameFullscreen);
    if (hide == hidden_) {
        app_.SetCornersHidden(monitor_, hidden_);
        return;
    }
    hidden_ = hide;
    app_.SetCornersHidden(monitor_, hidden_);
    if (hide) {
        ShowWindow(hwnd_, SW_HIDE);
        return;
    }
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    Render();
}

// ---------------------------------------------------------------------------
// Placement
// ---------------------------------------------------------------------------

void Pulse::Place() {
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(monitor_, &mi);
    monitorRect_ = mi.rcMonitor;
    UINT dx = 96, dy = 96;
    if (FAILED(GetDpiForMonitor(monitor_, MDT_EFFECTIVE_DPI, &dx, &dy))) dx = 96;
    dpi_ = dx * app_.Settings().scale / 100.0f;

    canvasW_ = kMaxViewWidth + 2 * kShadowPad;
    canvasH_ = kMaxViewHeight + kShadowPad;
    const int wpx = (int)std::ceil(canvasW_ * dpi_ / 96.0f), hpx = (int)std::ceil(canvasH_ * dpi_ / 96.0f);
    windowPos_ = {monitorRect_.left + (monitorRect_.right - monitorRect_.left - wpx) / 2, monitorRect_.top};
    renderer_.Resize(wpx, hpx, dpi_);
    SetWindowPos(hwnd_, HWND_TOPMOST, windowPos_.x, windowPos_.y, wpx, hpx, SWP_NOACTIVATE);
    UpdateAppBar(true);
}

void Pulse::UpdateAppBar(bool force) {
    // Registered even without reserving space, to hear about full-screen apps.
    if (!appBar_) {
        APPBARDATA abd{};
        abd.cbSize = sizeof(abd);
        abd.hWnd = hwnd_;
        abd.uCallbackMessage = WM_APP_APPBAR;
        appBar_ = SHAppBarMessage(ABM_NEW, &abd) != FALSE;
        if (!appBar_) return;
        force = true;
    }
    const Config& config = app_.Settings();
    const bool stays = config.alwaysSmall || !config.autoHide;
    // The reserved strip is the pill itself. Extra space below the small island would show as a gap.
    const int reserve = config.reserveSpace && stays ? (int)std::ceil(kCompactHeight * dpi_ / 96.0f) : 0;
    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    abd.hWnd = hwnd_;
    abd.uEdge = ABE_TOP;
    abd.rc = {monitorRect_.left, monitorRect_.top, monitorRect_.right, monitorRect_.top + reserve};
    SHAppBarMessage(ABM_QUERYPOS, &abd);
    abd.rc.bottom = abd.rc.top + reserve;
    // Setting the same position again would make Explorer re-broadcast the work area.
    if (!force && EqualRect(&abd.rc, &appBarRect_)) return;
    appBarRect_ = abd.rc;
    SHAppBarMessage(ABM_SETPOS, &abd);
}

void Pulse::RemoveAppBar() {
    if (!appBar_) return;
    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    abd.hWnd = hwnd_;
    SHAppBarMessage(ABM_REMOVE, &abd);
    appBar_ = false;
    appBarRect_ = {};
}

bool Pulse::ForegroundIsFullscreen() const {
    HWND fg = GetForegroundWindow();
    if (!fg || fg == hwnd_) return false;
    wchar_t cls[64] = {};
    GetClassNameW(fg, cls, 64);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd")) return false;
    RECT r;
    if (!GetWindowRect(fg, &r)) return false;
    return r.left <= monitorRect_.left && r.top <= monitorRect_.top && r.right >= monitorRect_.right &&
           r.bottom >= monitorRect_.bottom;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

D2D1_POINT_2F Pulse::ToDip(POINT p) const { return {p.x * 96.0f / dpi_, p.y * 96.0f / dpi_}; }

D2D1_POINT_2F Pulse::FromClient(POINT client) const {
    D2D1_POINT_2F d = ToDip(client);
    d.x += originX_;
    d.y += originY_;
    return d;
}

void Pulse::ApplySlider(float canvasX) {
    const float span = std::max(1.0f, dragTrack_.right - dragTrack_.left);
    const float unit = std::clamp((canvasX - dragTrack_.left) / span, 0.0f, 1.0f);
    if (dragging_ == kVolumeSlider) app_.Devices().SetVolume(unit);
    else if (dragging_ == kMicSlider) app_.Devices().SetMic(unit);
    else if (dragging_ == kBrightnessSlider) app_.Devices().SetBrightness(monitor_, unit);
    if (!animating_) Render();
}

void Pulse::EndSlider() {
    if (dragging_.empty()) return;
    app_.Devices().Flush();
    dragging_.clear();
    if (GetCapture() == hwnd_) ReleaseCapture();
}

void Pulse::PresentFrame() {
    if (!app_.Settings().alwaysSmall) {
        originX_ = originY_ = 0;
        renderer_.Present(hwnd_, windowPos_);
        return;
    }
    // Ears sit beside the pill. Under the small island the window stops at the pill, so nothing is reserved below it.
    const float pad = 18.0f;
    const float below = pill_.bottom <= kCompactHeight + 0.5f ? 1.0f : pad;
    const float left = std::clamp(pill_.left - pad, 0.0f, std::max(0.0f, canvasW_ - 1.0f));
    const float right = std::clamp(pill_.right + pad, left + 1.0f, canvasW_);
    const float bottom = std::clamp(std::max(pill_.bottom, 1.0f) + below, 1.0f, canvasH_);
    originX_ = left;
    originY_ = 0;
    auto px = [&](float dip) { return (int)std::lround(dip * dpi_ / 96.0f); };
    const int widthPx = std::max(1, renderer_.WidthPx());
    const int heightPx = std::max(1, renderer_.HeightPx());
    int leftPx = std::clamp(px(left), 0, widthPx - 1);
    int rightPx = std::clamp(std::max(px(left) + 1, px(right)), leftPx + 1, widthPx);
    int bottomPx = std::clamp(std::max(1, px(bottom)), 1, heightPx);
    RECT src{leftPx, 0, rightPx, bottomPx};
    const POINT pos{windowPos_.x + src.left, windowPos_.y + src.top};
    renderer_.Present(hwnd_, pos, &src);
}

int Pulse::HitAt(D2D1_POINT_2F p) const {
    for (size_t i = 0; i < hits_.size(); ++i) {
        const D2D1_RECT_F& r = hits_[i].rect;
        if (p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom) return (int)i;
    }
    return -1;
}

bool Pulse::InEdgeZone() const {
    if (hidden_) return false;
    POINT p;
    GetCursorPos(&p);
    if (p.y < monitorRect_.top || p.x < monitorRect_.left || p.x >= monitorRect_.right) return false;
    const D2D1_POINT_2F d = ToDip({p.x - windowPos_.x, p.y - windowPos_.y});
    return d.y < kEdgeZoneHeight && std::fabs(d.x - canvasW_ / 2) < kEdgeZoneWidth / 2;
}

void Pulse::StartPolling() {
    if (polling_) return;
    polling_ = true;
    // Drop a stale "pressed since last query" bit so a click from before polling started cannot open the menu.
    rightWasDown_ = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    SetTimer(hwnd_, kPollTimer, kPollMs, nullptr);
}

// The cursor is tracked by polling because the window stops getting mouse messages as soon as
// the cursor is over a fully transparent pixel, which happens while the notch shrinks under it.
void Pulse::Poll() {
    const Config& config = app_.Settings();
    POINT p;
    GetCursorPos(&p);
    const D2D1_POINT_2F d = ToDip({p.x - windowPos_.x, p.y - windowPos_.y});
    const bool edge = InEdgeZone();
    const bool inside = edge || (!hidden_ && d.x >= pill_.left - 2 && d.x < pill_.right + 2 &&
                                 d.y >= pill_.top - 2 && d.y < pill_.bottom + 2);
    const double now = NowMs();
    if (inside) {
        mouse_ = d;
        outsideSince_ = 0;
        if (insideSince_ == 0) insideSince_ = now;
        // A hidden notch can only be reached by hover, whatever expand_on_hover says.
        const bool canHover = config.expandOnHover || (config.autoHide && mode_ == Mode::Hidden);
        if (!hovering_ && !hoverSuppressed_ && canHover && now - insideSince_ >= config.hoverDelay) {
            hovering_ = true;
            Refresh();
        }
    } else {
        mouse_ = {-1000.0f, -1000.0f};
        insideSince_ = 0;
        hoverSuppressed_ = false;
        if (hovering_) {
            // The menu hangs off the notch; don't tuck the notch away while choosing.
            if (menu_ && menu_->Open()) outsideSince_ = 0;
            else if (outsideSince_ == 0) outsideSince_ = now;
            else if (now - outsideSince_ >= kLeaveDelayMs) {
                hovering_ = false;
                Refresh();
            }
        }
    }
    const bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (!dragging_.empty()) {
        if (leftDown) ApplySlider(d.x);
        else EndSlider();
    } else if (leftDown && inside) {
        const int held = HitAt(d);
        if (held >= 0 && hits_[held].command.rfind(L"!slider:", 0) == 0) {
            dragging_ = hits_[held].command;
            dragTrack_ = hits_[held].rect;
            SetCapture(hwnd_);
            ApplySlider(d.x);
        }
    }
    const int hit = HitAt(mouse_);
    if (hit != hoverHit_) {
        hoverHit_ = hit;
        if (!animating_) Render();
    }
    // Layered pixels drop clicks while the notch is moving. A press since the last poll still counts,
    // even when the button is already up again.
    const SHORT right = GetAsyncKeyState(VK_RBUTTON);
    const bool rightDown = (right & 0x8000) != 0;
    if (inside && ((right & 1) || (rightDown && !rightWasDown_))) ShowMenu(d);
    rightWasDown_ = rightDown;
    if (dragging_.empty() && !inside && !hovering_ && !(menu_ && menu_->Open())) {
        KillTimer(hwnd_, kPollTimer);
        polling_ = false;
    }
}

void Pulse::OnClick(D2D1_POINT_2F p) {
    const int hit = HitAt(p);
    if (hit >= 0) {
        if (hits_[hit].command.rfind(L"!slider:", 0) == 0) return;
        RunAction(hits_[hit].command);
        return;
    }
    if (p.x < pill_.left || p.x >= pill_.right || p.y < pill_.top || p.y >= pill_.bottom) return;
    // Clicking the notch itself expands it right away (or collapses it when hover expansion is off).
    if (!hovering_) {
        hovering_ = true;
        StartPolling();
    } else if (!app_.Settings().expandOnHover) {
        hovering_ = false;
    }
    Refresh();
}

void Pulse::OnMiddleClick(D2D1_POINT_2F p) {
    const int hit = HitAt(p);
    if (hit < 0) return;
    if (HWND target = WindowFromAction(hits_[hit].command)) PostMessageW(target, WM_CLOSE, 0, 0);
    // Like the taskbar: another instance of the pinned app.
    else if (std::wstring pin = PinFromAction(hits_[hit].command); !pin.empty()) LaunchApp(pin);
}

void Pulse::ShowMenu(D2D1_POINT_2F p) {
    // The press is seen both as a window message and by polling. Ignore the second one.
    const double now = NowMs();
    if (menu_ && menu_->Open() && now - menuShownAt_ < 280.0) return;
    menuShownAt_ = now;

    const FrameState& state = app_.State();
    const int hit = HitAt(p);
    const std::wstring command = hit >= 0 ? hits_[hit].command : std::wstring();
    const HWND window = WindowFromAction(command);
    // The app under the mouse, pinned or not, and the windows it has here.
    std::wstring exePath = PinFromAction(command);
    if (window) {
        for (const Task& t : tasks_.Items()) {
            if (t.hwnd == window) exePath = t.exePath;
        }
    }
    const std::vector<HWND> windows = window ? std::vector<HWND>{window} : WindowsOf(exePath);
    const bool pinned = !exePath.empty() && app_.IsPinned(exePath);
    const auto& pins = app_.Pins();
    const bool first = pinned && SamePath(pins.front().path, exePath);
    const bool last = pinned && SamePath(pins.back().path, exePath);

    std::vector<MenuItem> items;
    auto add = [&](int id, const wchar_t* label, bool enabled = true, bool checked = false) {
        items.push_back({label, id, enabled, checked});
    };
    if (window || pinned) {
        const bool launchable = CanPin(exePath);
        add(kMenuLaunch, windows.empty() ? L"起動" : L"新しいウィンドウを開く", launchable);
        add(kMenuTogglePin, pinned ? L"ピン留めを外す" : L"ピン留めする", pinned || launchable);
        if (pinned) {
            add(kMenuPinLeft, L"左へ移動", !first);
            add(kMenuPinRight, L"右へ移動", !last);
        }
        if (!windows.empty()) {
            add(kMenuCloseWindow, windows.size() > 1 ? L"すべてのウィンドウを閉じる" : L"ウィンドウを閉じる");
        }
        items.push_back({});
    }
    add(kMenuTaskManager, L"タスク マネージャー");
    add(kMenuToggleTaskbar, L"Windows のタスクバーを表示", true, !app_.TaskbarHidden());
    if (state.online) {
        items.push_back({});
        add(kMenuPause, state.paused ? L"ObjectFrame を再開" : L"ObjectFrame を一時停止");
        add(kMenuRetile, L"再タイル");
    }
    items.push_back({});
    add(kMenuEditConfig, L"設定ファイルを開く");
    add(kMenuReload, L"設定を再読み込み");
    items.push_back({});
    add(kMenuQuit, L"ObjectPulse を終了");

    if (!menu_) menu_ = std::make_unique<Menu>(app_.Module());
    POINT pt;
    GetCursorPos(&pt);
    const Config& config = app_.Settings();
    menu_->Show(monitor_, dpi_, pt, hwnd_, config.background, config.opacity, app_.Accent(), std::move(items),
                [this, exePath, windows](int cmd) { RunMenuCommand(cmd, exePath, windows); });
    StartPolling();
}

void Pulse::RunMenuCommand(int cmd, const std::wstring& exePath, const std::vector<HWND>& windows) {
    switch (cmd) {
        case kMenuLaunch: LaunchApp(exePath); break;
        case kMenuTogglePin: app_.TogglePin(exePath); break;
        case kMenuPinLeft: app_.MovePin(exePath, -1); break;
        case kMenuPinRight: app_.MovePin(exePath, +1); break;
        case kMenuCloseWindow:
            for (HWND w : windows) PostMessageW(w, WM_CLOSE, 0, 0);
            break;
        case kMenuTaskManager: ShellExecuteW(nullptr, L"open", L"taskmgr.exe", nullptr, nullptr, SW_SHOWNORMAL); break;
        case kMenuToggleTaskbar: app_.ToggleTaskbar(); break;
        case kMenuPause: SendFrameCommand(L"pause"); break;
        case kMenuRetile: SendFrameCommand(L"retile"); break;
        case kMenuEditConfig:
            ShellExecuteW(nullptr, L"open", L"notepad.exe", (L"\"" + app_.ConfigPath() + L"\"").c_str(), nullptr,
                          SW_SHOWNORMAL);
            break;
        case kMenuReload: app_.Reload(); break;
        case kMenuQuit: app_.Quit(); break;
    }
}

}  // namespace op
