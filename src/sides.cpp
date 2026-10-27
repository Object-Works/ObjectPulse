#include "sides.h"

#include "app.h"
#include "controls.h"
#include "renderer.h"
#include "views.h"

#include <shellscalingapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace op {

namespace {

constexpr const wchar_t* kSideClass = L"ObjectPulseSide";
constexpr float kShadow = 16.0f;   // inward room for the same soft shadow as the top notch
constexpr float kAlongPad = 18.0f; // ears and shadow along the screen edge
constexpr float kPad = 14.0f;  // clears the notch's rounded end when the faders are open
constexpr float kCompactPad = 8.0f;
constexpr float kIcon = 14.0f;
constexpr float kTrack = 72.0f;
constexpr float kInner = 6.0f;
constexpr float kBlock = kIcon + kInner + kTrack;
constexpr float kPairGap = 10.0f;
constexpr float kDepth = kCompactHeight;  // how far the side notch stands out, matching the top notch's height
constexpr float kEdgeDip = 16.0f;     // how far onto the screen the cursor must come to count as hovering the edge
constexpr float kEdgeSlop = 28.0f;    // extra reach around the pill, along the screen edge
constexpr ULONGLONG kLeaveMs = 280;
constexpr UINT_PTR kAnimTimer = 1;
constexpr COLORREF kWhite = RGB(255, 255, 255);

struct Slider {
    D2D1_RECT_F track{};
    const wchar_t* command = nullptr;
};

D2D1_RECT_F Box(float x, float y, float w, float h) { return {x, y, x + w, y + h}; }
D2D1_POINT_2F Pt(float x, float y) { return {x, y}; }

void Glyph(Renderer& r, const wchar_t* kind, float x, float cy, bool muted) {
    const float alpha = 0.92f;
    const float cx = x + kIcon / 2;
    if (!wcscmp(kind, kBrightnessSlider)) {
        r.FillRound(Box(cx - 2.7f, cy - 2.7f, 5.4f, 5.4f), 2.7f, kWhite, alpha);
        for (int i = 0; i < 8; ++i) {
            const float a = i * 3.14159265f / 4.0f;
            const float cs = std::cos(a), sn = std::sin(a);
            r.Line(Pt(cx + cs * 4.6f, cy + sn * 4.6f), Pt(cx + cs * 6.4f, cy + sn * 6.4f), kWhite, alpha, 1.2f);
        }
    } else if (!wcscmp(kind, kVolumeSlider)) {
        r.FillRound(Box(cx - 6.2f, cy - 1.7f, 3.0f, 3.4f), 0.6f, kWhite, alpha);
        r.FillTriangle(Pt(cx - 3.4f, cy - 1.7f), Pt(cx + 1.6f, cy - 5.0f), Pt(cx + 1.6f, cy + 5.0f), kWhite, alpha);
        r.Line(Pt(cx + 3.4f, cy - 2.6f), Pt(cx + 5.2f, cy), kWhite, alpha, 1.2f);
        r.Line(Pt(cx + 5.2f, cy), Pt(cx + 3.4f, cy + 2.6f), kWhite, alpha, 1.2f);
    } else {
        r.FillRound(Box(cx - 2.4f, cy - 6.0f, 4.8f, 7.6f), 2.4f, kWhite, alpha);
        r.StrokeRound(Box(cx - 4.4f, cy - 2.4f, 8.8f, 6.6f), 3.6f, kWhite, alpha, 1.15f);
        r.Line(Pt(cx, cy + 4.4f), Pt(cx, cy + 6.6f), kWhite, alpha, 1.15f);
        r.Line(Pt(cx - 2.8f, cy + 6.6f), Pt(cx + 2.8f, cy + 6.6f), kWhite, alpha, 1.15f);
    }
    if (muted) r.Line(Pt(cx - 6.0f, cy + 5.5f), Pt(cx + 6.0f, cy - 5.5f), kWhite, alpha, 1.2f);
}

// Full is at the top of the track. `x` is the left of the icon column. A short track draws the icon only.
Slider DrawFader(Renderer& r, float x, float top, float trackLen, float unit, bool muted, const wchar_t* command) {
    const float cx = x + kIcon / 2;
    Glyph(r, command, x, top + kIcon / 2, muted);
    if (trackLen < 2.0f) return {};
    const float ty = top + kIcon + kInner;
    const float value = std::clamp(unit, 0.0f, 1.0f);
    const float thumbY = ty + trackLen * (1.0f - value);
    r.FillRound(Box(cx - 1.6f, ty, 3.2f, trackLen), 1.6f, kWhite, 0.22f);
    r.FillRound(Box(cx - 1.6f, thumbY, 3.2f, std::max(3.2f, ty + trackLen - thumbY)), 1.6f, kWhite,
                muted ? 0.45f : 0.95f);
    r.FillRound(Box(cx - 4.5f, thumbY - 4.5f, 9.0f, 9.0f), 4.5f, kWhite, 1.0f);
    return {Box(cx - 14.0f, ty, 28.0f, trackLen), command};
}

float ExpandedAlong(bool left) { return kPad + kBlock + (left ? 0.0f : kPairGap + kBlock) + kPad; }

float CompactAlong(bool left) { return kCompactPad + kIcon + (left ? 0.0f : kPairGap + kIcon) + kCompactPad; }

void StepAlong(float& value, float& velocity, float target, float dt) {
    constexpr float kStiffness = 420.0f;
    const float damping = 2.0f * std::sqrt(kStiffness) * 0.74f;
    constexpr int kSubsteps = 4;
    const float h = dt / kSubsteps;
    for (int i = 0; i < kSubsteps; ++i) {
        const float a = -kStiffness * (value - target) - damping * velocity;
        velocity += a * h;
        value += velocity * h;
    }
    if (std::fabs(value - target) < 0.15f && std::fabs(velocity) < 2.0f) {
        value = target;
        velocity = 0.0f;
    }
}

}  // namespace

struct SideIslands::Island {
    App* app = nullptr;
    HMONITOR monitor = nullptr;
    HWND hwnd = nullptr;
    Renderer renderer;
    SideIslands* owner = nullptr;
    bool left = false;
    bool suppressed = false;  // fullscreen: stay hidden even when the edge is touched
    bool shown = false;
    bool timing = false;
    float along = 0.0f;
    float alongVelocity = 0.0f;
    float alongTarget = 0.0f;
    POINT anchor{};  // full canvas on screen; it does not shrink, so closing cannot leave a scrap behind
    ULONGLONG outsideSince = 0;
    ULONGLONG animTick = 0;
    RECT bounds{};
    float dpi = 96.0f;
    POINT pos{};
    int widthPx = 1;
    int heightPx = 1;
    Slider sliders[2]{};
    int sliderCount = 0;
    std::wstring dragging;
    D2D1_RECT_F dragTrack{};
};

SideIslands::SideIslands() = default;
SideIslands::~SideIslands() { Clear(); }

void SideIslands::Clear() {
    auto held = std::move(islands_);
    for (auto& island : held) {
        if (island->hwnd) DestroyWindow(island->hwnd);
    }
}

LRESULT CALLBACK SideIslands::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* island = static_cast<Island*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(island));
        island->hwnd = hwnd;
    }
    auto* island = reinterpret_cast<Island*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!island || !island->app) return DefWindowProcW(hwnd, msg, wp, lp);

    const auto dip = [island](POINT p) {
        return D2D1_POINT_2F{p.x * 96.0f / island->dpi, p.y * 96.0f / island->dpi};
    };
    auto hit = [&](D2D1_POINT_2F p) -> const Slider* {
        for (int i = 0; i < island->sliderCount; ++i) {
            const D2D1_RECT_F& r = island->sliders[i].track;
            if (p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom) return &island->sliders[i];
        }
        return nullptr;
    };
    auto apply = [&](float y) {
        const float span = std::max(1.0f, island->dragTrack.bottom - island->dragTrack.top);
        const float unit = std::clamp(1.0f - (y - island->dragTrack.top) / span, 0.0f, 1.0f);
        if (island->dragging == kVolumeSlider) island->app->Devices().SetVolume(unit);
        else if (island->dragging == kMicSlider) island->app->Devices().SetMic(unit);
        else if (island->dragging == kBrightnessSlider) island->app->Devices().SetBrightness(island->monitor, unit);
    };

    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_LBUTTONDOWN: {
            const D2D1_POINT_2F p = dip({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            if (const Slider* slider = hit(p)) {
                island->dragging = slider->command;
                island->dragTrack = slider->track;
                SetCapture(hwnd);
                apply(p.y);
                App::Instance().RefreshSides();
            }
            return 0;
        }
        case WM_MOUSEMOVE:
            if (!island->dragging.empty()) {
                apply(dip({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}).y);
                App::Instance().RefreshSides();
            }
            return 0;
        case WM_LBUTTONUP:
            if (!island->dragging.empty()) {
                island->app->Devices().Flush();
                island->dragging.clear();
                if (GetCapture() == hwnd) ReleaseCapture();
                App::Instance().RefreshSides();
            }
            return 0;
        case WM_TIMER:
            if (wp == kAnimTimer && island->owner) island->owner->Animate(*island);
            return 0;
        case WM_SETCURSOR: {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            const bool drag = !island->dragging.empty() || hit(dip(pt));
            SetCursor(LoadCursorW(nullptr, drag ? IDC_SIZENS : IDC_ARROW));
            return TRUE;
        }
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

void SideIslands::Paint(Island& island) {
    if (!island.shown || !island.hwnd || !island.renderer.Begin()) return;
    ID2D1RenderTarget* rt = island.renderer.Target();
    const Config& config = island.app->Settings();
    const float opacity = config.opacity / 100.0f;
    const float along = std::max(island.along, 1.0f);
    const float canvasH = ExpandedAlong(island.left) + kAlongPad * 2;
    const float bodyX = island.left ? 0.0f : kShadow;
    const float centerY = canvasH / 2;
    const D2D1_POINT_2F center{bodyX + kDepth / 2, centerY};
    // The same notch as the top, turned so its flat edge joins this screen edge. It grows from that edge.
    rt->SetTransform(D2D1::Matrix3x2F::Rotation(island.left ? -90.0f : 90.0f, center));
    const D2D1_RECT_F local{center.x - along / 2, center.y - kDepth / 2, center.x + along / 2, center.y + kDepth / 2};
    const float radius = std::min(kNotchRadius, kDepth / 2);
    const float ear = std::min(kNotchEar, std::max(0.0f, kDepth - radius));
    for (int i = 0; i < 6; ++i) {
        const float grow = 1.0f + i * 1.7f, fade = 1.0f - i / 6.0f;
        D2D1_RECT_F shadow{local.left - grow, local.top - radius, local.right + grow, local.bottom + grow + 1.0f};
        island.renderer.FillRound(shadow, radius + grow, RGB(0, 0, 0), 0.07f * fade * fade);
    }
    ComPtr<ID2D1PathGeometry> path = island.renderer.NotchPath(local, radius, ear);
    const D2D1_MATRIX_3X2_F spin = D2D1::Matrix3x2F::Rotation(island.left ? -90.0f : 90.0f, center);
    if (path) {
        rt->SetTransform(spin);
        rt->FillGeometry(path.Get(), island.renderer.Brush(config.background, opacity));
    }
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    // Icons are clipped to the pill. Otherwise they stay on screen after the black shape has tucked away.
    ComPtr<ID2D1Layer> layer;
    const bool clipped = path && SUCCEEDED(rt->CreateLayer(nullptr, layer.GetAddressOf()));
    if (clipped) {
        rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), path.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, spin,
                                            1.0f),
                      layer.Get());
    }

    const float compact = CompactAlong(island.left);
    const float full = ExpandedAlong(island.left);
    const float open = std::clamp((along - compact) / std::max(1.0f, full - compact), 0.0f, 1.0f);
    const float earPad = 12.0f;
    const float track = kTrack * open;
    const float block = kIcon + (track >= 2.0f ? kInner + track : 0.0f);
    const int count = island.left ? 1 : 2;
    const float gap = track >= 2.0f ? kPairGap : kPairGap;
    const float stack = count * block + (count - 1) * gap;
    const float column = bodyX + (kDepth - kIcon) / 2;
    float y = centerY - stack / 2;
    const DeviceLevels& levels = island.app->Devices();
    island.sliderCount = 0;
    auto place = [&](float unit, bool muted, const wchar_t* command) {
        Slider slider = DrawFader(island.renderer, column, y, track, unit, muted, command);
        if (slider.track.bottom > slider.track.top + 8.0f && island.sliderCount < 2) {
            island.sliders[island.sliderCount++] = slider;
        }
        y += block + gap;
    };
    if (island.left) {
        const float unit = levels.Brightness(island.monitor) < 0 ? 1.0f : levels.Brightness(island.monitor) / 100.0f;
        place(unit, false, kBrightnessSlider);
    } else {
        place(levels.Volume(), levels.VolumeMuted(), kVolumeSlider);
        const bool mic = levels.HasMic();
        place(mic ? levels.Mic() : 0.0f, levels.MicMuted() || !mic, kMicSlider);
    }
    if (clipped) rt->PopLayer();
    if (!island.renderer.End()) return;

    // The window stays the full canvas. A shrinking layered window leaves the previous frame's
    // icons on screen for a moment; drawing into the same rectangle replaces them with transparency.
    const float topDip = std::max(0.0f, centerY - along / 2 - earPad);
    const float bottomDip = std::min(canvasH, centerY + along / 2 + earPad);
    const int visibleH = std::max(1, (int)std::lround((bottomDip - topDip) * island.dpi / 96.0f));
    const int mid = (island.bounds.top + island.bounds.bottom) / 2;
    island.heightPx = visibleH;
    island.pos = {island.anchor.x, mid - visibleH / 2};
    island.renderer.Present(island.hwnd, island.anchor);
}

void SideIslands::Apply(App& app, const std::vector<HMONITOR>& monitors) {
    app_ = &app;
    Clear();
    if (monitors.empty()) return;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = app.Module();
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kSideClass;
        registered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        if (!registered) return;
    }

    for (HMONITOR monitor : monitors) {
        app.Devices().Refresh(monitor);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(monitor, &mi)) continue;
        UINT dx = 96, dy = 96;
        if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dx, &dy))) dx = 96;
        const float dpi = dx * app.Settings().scale / 100.0f;
        const RECT rc = mi.rcMonitor;
        const float widthDip = kDepth + kShadow;

        for (int side = 0; side < 2; ++side) {
            const bool left = side == 0;
            const float heightDip = ExpandedAlong(left) + kAlongPad * 2;
            const int widthPx = std::max(1, (int)std::ceil(widthDip * dpi / 96.0f));
            const int heightPx = std::max(1, (int)std::ceil(heightDip * dpi / 96.0f));
            const int y = rc.top + (rc.bottom - rc.top - heightPx) / 2;
            auto held = std::make_unique<Island>();
            Island& island = *held;
            island.app = &app;
            island.owner = this;
            island.monitor = monitor;
            island.left = left;
            island.along = island.alongTarget = 0.0f;
            island.dpi = dpi;
            island.bounds = rc;
            island.widthPx = widthPx;
            island.heightPx = heightPx;
            island.anchor = island.pos = {left ? rc.left : rc.right - widthPx, y};
            if (!island.renderer.Init()) continue;
            island.renderer.Resize(widthPx, heightPx, dpi);
            island.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                                          kSideClass, L"", WS_POPUP, island.pos.x, island.pos.y, widthPx, heightPx,
                                          nullptr, nullptr, app.Module(), &island);
            if (!island.hwnd) continue;
            islands_.push_back(std::move(held));
        }
    }
}

void SideIslands::Animate(Island& island) {
    const ULONGLONG now = GetTickCount64();
    float dt = island.animTick ? (now - island.animTick) / 1000.0f : 0.016f;
    island.animTick = now;
    dt = std::clamp(dt, 0.001f, 0.05f);
    StepAlong(island.along, island.alongVelocity, island.alongTarget, dt);
    // Hide while the pill still covers the icons. Waiting until it is gone paints one last icon on its own.
    if (island.alongTarget < 1.0f && island.along <= kIcon) {
        Hide(island);
        return;
    }
    Paint(island);
    if (island.along == island.alongTarget && island.alongVelocity == 0.0f) {
        island.timing = false;
        if (island.hwnd) KillTimer(island.hwnd, kAnimTimer);
        if (island.alongTarget < 1.0f) Hide(island);
    }
}

void SideIslands::Hide(Island& island) {
    if (!island.shown || !island.hwnd) return;
    island.shown = false;
    island.timing = false;
    island.outsideSince = 0;
    KillTimer(island.hwnd, kAnimTimer);
    ShowWindow(island.hwnd, SW_HIDE);
}

bool SideIslands::Hot(const Island& island, POINT p) {
    if (p.y < island.bounds.top || p.y >= island.bounds.bottom) return false;
    if (island.shown && p.x >= island.pos.x && p.x < island.pos.x + island.widthPx && p.y >= island.pos.y &&
        p.y < island.pos.y + island.heightPx)
        return true;
    const int slop = (int)std::lround(kEdgeSlop * island.dpi / 96.0f);
    const int edge = (int)std::lround(kEdgeDip * island.dpi / 96.0f);
    const int alongPx = (int)std::lround(ExpandedAlong(island.left) * island.dpi / 96.0f);
    const int mid = (island.bounds.top + island.bounds.bottom) / 2;
    if (p.y < mid - alongPx / 2 - slop || p.y >= mid + alongPx / 2 + slop) return false;
    if (island.left) return p.x >= island.bounds.left && p.x < island.bounds.left + edge;
    return p.x < island.bounds.right && p.x >= island.bounds.right - edge;
}

void SideIslands::SetHidden(HMONITOR monitor, bool hidden) {
    for (auto& held : islands_) {
        Island& island = *held;
        if (island.monitor != monitor || !island.hwnd) continue;
        island.suppressed = hidden;
        if (hidden) Hide(island);
    }
}

void SideIslands::CheckHover() {
    POINT p;
    if (!GetCursorPos(&p)) return;
    const ULONGLONG now = GetTickCount64();
    for (auto& held : islands_) {
        Island& island = *held;
        if (!island.hwnd || island.suppressed) continue;
        const bool hot = !island.dragging.empty() || Hot(island, p);
        if (hot) {
            island.outsideSince = 0;
            island.alongTarget = ExpandedAlong(island.left);
            if (!island.shown) {
                island.shown = true;
                island.along = 0.0f;
                island.alongVelocity = 0.0f;
                ShowWindow(island.hwnd, SW_SHOWNOACTIVATE);
                Paint(island);
            }
        } else if (island.shown && island.alongTarget > 0.0f) {
            if (island.outsideSince == 0) island.outsideSince = now;
            else if (now - island.outsideSince >= kLeaveMs) island.alongTarget = 0.0f;
        }
        const bool moving = island.along != island.alongTarget || island.alongVelocity != 0.0f;
        if (moving && !island.timing) {
            island.timing = true;
            island.animTick = now;
            SetTimer(island.hwnd, kAnimTimer, 16, nullptr);
        }
    }
}

void SideIslands::Refresh() {
    for (auto& held : islands_) Paint(*held);
}

}  // namespace op
