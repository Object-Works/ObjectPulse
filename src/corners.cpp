#include "corners.h"

#include "views.h"

#include <shellscalingapi.h>

#include <algorithm>
#include <cmath>

namespace op {

namespace {

constexpr const wchar_t* kCornerClass = L"ObjectPulseCorner";
constexpr float kShadow = 14.0f;  // room inside the curve for the same soft shadow as the notch

// The corner of the square is filled; the inner edge is a quarter circle, so the screen reads as rounded.
void DrawCap(Renderer& r, float radius, COLORREF color, float opacity) {
    ID2D1RenderTarget* rt = r.Target();
    if (!rt || radius < 1.0f) return;
    ComPtr<ID2D1PathGeometry> arc;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(r.Factory()->CreatePathGeometry(arc.GetAddressOf())) || FAILED(arc->Open(sink.GetAddressOf()))) return;
    sink->BeginFigure({radius, 0.0f}, D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment({0.0f, radius}, {radius, radius}, 0.0f, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                                  D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    if (FAILED(sink->Close())) return;

    // Shadow falls onto the screen, on the inner side of the curve. The solid cap covers the outer half.
    for (int i = 4; i >= 1; --i) {
        const float fade = 1.0f - i / 5.0f;
        rt->DrawGeometry(arc.Get(), r.Brush(RGB(0, 0, 0), 0.22f * fade * fade), 2.0f + i * 2.4f, r.RoundStroke());
    }

    ComPtr<ID2D1PathGeometry> cap;
    ComPtr<ID2D1GeometrySink> capSink;
    if (FAILED(r.Factory()->CreatePathGeometry(cap.GetAddressOf())) || FAILED(cap->Open(capSink.GetAddressOf()))) return;
    capSink->BeginFigure({0.0f, 0.0f}, D2D1_FIGURE_BEGIN_FILLED);
    capSink->AddLine({radius, 0.0f});
    capSink->AddArc(D2D1::ArcSegment({0.0f, radius}, {radius, radius}, 0.0f, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                                     D2D1_ARC_SIZE_SMALL));
    capSink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(capSink->Close())) return;
    rt->FillGeometry(cap.Get(), r.Brush(color, opacity));
    rt->DrawGeometry(arc.Get(), r.Brush(RGB(255, 255, 255), 0.14f * opacity), 1.0f, r.RoundStroke());
}

}  // namespace

LRESULT CALLBACK ScreenCorners::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ScreenCorners::Clear() {
    for (auto& set : sets_) {
        for (HWND hwnd : set->hwnd) {
            if (hwnd) DestroyWindow(hwnd);
        }
    }
    sets_.clear();
}

void ScreenCorners::Paint(Set& set) {
    if (!set.renderer.Init()) return;
    const float radius = kNotchRadius;
    const float side = radius + kShadow;
    const float opacity = config_.opacity / 100.0f;
    // 0 top-left, then clockwise, matching the window order in Apply.
    for (int i = 0; i < 4; ++i) {
        if (!set.hwnd[i] || !set.renderer.Begin()) return;
        ID2D1RenderTarget* rt = set.renderer.Target();
        const float c = side / 2.0f;
        rt->SetTransform(D2D1::Matrix3x2F::Rotation(90.0f * i, D2D1::Point2F(c, c)));
        DrawCap(set.renderer, radius, config_.background, opacity);
        if (!set.renderer.End()) return;
        set.renderer.Present(set.hwnd[i], set.pos[i]);
    }
}

void ScreenCorners::Apply(HINSTANCE instance, const std::vector<HMONITOR>& monitors, const Config& config) {
    instance_ = instance;
    config_ = config;
    Clear();
    if (!config_.corners || monitors.empty()) return;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = instance_;
        wc.lpszClassName = kCornerClass;
        registered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        if (!registered) return;
    }

    sets_.reserve(monitors.size());
    for (HMONITOR monitor : monitors) {
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(monitor, &mi)) continue;
        UINT dx = 96, dy = 96;
        if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dx, &dy))) dx = 96;
        auto held = std::make_unique<Set>();
        Set& set = *held;
        set.monitor = monitor;
        set.dpi = dx * config_.scale / 100.0f;
        const float side = kNotchRadius + kShadow;
        set.sizePx = (int)std::ceil(side * set.dpi / 96.0f);
        const RECT rc = mi.rcMonitor;
        const int s = set.sizePx;
        set.pos[0] = {rc.left, rc.top};
        set.pos[1] = {rc.right - s, rc.top};
        set.pos[2] = {rc.right - s, rc.bottom - s};
        set.pos[3] = {rc.left, rc.bottom - s};
        if (!set.renderer.Init()) continue;
        set.renderer.Resize(s, s, set.dpi);
        for (int i = 0; i < 4; ++i) {
            set.hwnd[i] = CreateWindowExW(
                WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, kCornerClass,
                L"", WS_POPUP, set.pos[i].x, set.pos[i].y, s, s, nullptr, nullptr, instance_, nullptr);
            if (set.hwnd[i]) ShowWindow(set.hwnd[i], SW_SHOWNOACTIVATE);
        }
        Paint(set);
        sets_.push_back(std::move(held));
    }
}

void ScreenCorners::SetHidden(HMONITOR monitor, bool hidden) {
    for (auto& held : sets_) {
        Set& set = *held;
        if (set.monitor != monitor || set.hidden == hidden) continue;
        set.hidden = hidden;
        for (HWND hwnd : set.hwnd) {
            if (!hwnd) continue;
            ShowWindow(hwnd, hidden ? SW_HIDE : SW_SHOWNOACTIVATE);
        }
        if (!hidden) Paint(set);
    }
}

}  // namespace op
