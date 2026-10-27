#include "menu.h"

#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace op {

namespace {

constexpr const wchar_t* kMenuClass = L"ObjectPulseMenu";
constexpr UINT_PTR kTimer = 1;
constexpr float kShadow = 22.0f;
constexpr float kRadius = 16.0f;
constexpr float kPadX = 8.0f, kPadY = 7.0f;
constexpr float kItemH = 32.0f, kSepH = 9.0f;
constexpr float kTextRight = 16.0f;
constexpr float kMinWidth = 188.0f, kMaxWidth = 420.0f;
constexpr COLORREF kWhite = RGB(255, 255, 255);

const TextStyle kLabel = {13.0f, DWRITE_FONT_WEIGHT_NORMAL, Align::Left};

double NowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

D2D1_RECT_F Rect(float x, float y, float w, float h) { return {x, y, x + w, y + h}; }

void DrawCheck(Renderer& r, D2D1_POINT_2F c, COLORREF color, float alpha) {
    r.Line({c.x - 4.5f, c.y + 0.5f}, {c.x - 1.5f, c.y + 3.5f}, color, alpha, 1.7f);
    r.Line({c.x - 1.5f, c.y + 3.5f}, {c.x + 5.0f, c.y - 3.5f}, color, alpha, 1.7f);
}

}  // namespace

Menu::Menu(HINSTANCE instance) : instance_(instance) {}

Menu::~Menu() { Close(); }

void Menu::Show(HMONITOR monitor, float dpi, POINT cursor, HWND owner, COLORREF background, int opacity,
                COLORREF accent, std::vector<MenuItem> items, std::function<void(int)> onSelect) {
    Close();
    if (items.empty() || !renderer_.Init()) return;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = instance_;
        wc.lpszClassName = kMenuClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        registered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        if (!registered) return;
    }

    monitor_ = monitor;
    dpi_ = dpi > 0 ? dpi : 96.0f;
    background_ = background;
    accent_ = accent;
    opacity_ = opacity;
    onSelect_ = std::move(onSelect);
    rows_.clear();
    checks_ = false;
    for (MenuItem& item : items) {
        checks_ = checks_ || item.checked;
        rows_.push_back({std::move(item), {}});
    }
    hover_ = -1;
    alpha_ = 0.55f;
    seenUp_ = false;
    wasDown_ = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) || (GetAsyncKeyState(VK_RBUTTON) & 0x8000);
    openedAt_ = lastTick_ = NowMs();

    Layout();
    const int wpx = (int)std::ceil((panelW_ + 2 * kShadow) * dpi_ / 96.0f);
    const int hpx = (int)std::ceil((panelH_ + 2 * kShadow) * dpi_ / 96.0f);
    // Owned by the notch, so it stays above that topmost window. Show before the first
    // UpdateLayeredWindow: doing it afterwards clears the bitmap and leaves a blank window
    // that still receives clicks.
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kMenuClass, L"",
                            WS_POPUP, cursor.x, cursor.y, wpx, hpx, owner, nullptr, instance_, this);
    if (!hwnd_) return;
    renderer_.Resize(wpx, hpx, dpi_);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    Place(cursor);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    Render();
    SetTimer(hwnd_, kTimer, 16, nullptr);
}

void Menu::Close() {
    onSelect_ = nullptr;
    if (!hwnd_) return;
    KillTimer(hwnd_, kTimer);
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    rows_.clear();
}

void Menu::Layout() {
    const float textX = checks_ ? 34.0f : 16.0f;
    float textW = 0;
    for (const Row& row : rows_) {
        if (row.item.id == 0) continue;
        textW = std::max(textW, renderer_.MeasureText(row.item.label, kLabel));
    }
    panelW_ = std::clamp(textX + textW + kTextRight, kMinWidth, kMaxWidth);
    float y = kShadow + kPadY;
    for (Row& row : rows_) {
        if (row.item.id == 0) {
            row.rect = {0, y + kSepH / 2, 0, 0};
            y += kSepH;
            continue;
        }
        row.rect = Rect(kShadow + kPadX / 2, y, panelW_ - kPadX, kItemH);
        y += kItemH;
    }
    panelH_ = y + kPadY - kShadow;
}

void Menu::Place(POINT cursor) {
    const int shadow = (int)std::ceil(kShadow * dpi_ / 96.0f);
    const int panelW = (int)std::ceil(panelW_ * dpi_ / 96.0f);
    const int panelH = (int)std::ceil(panelH_ * dpi_ / 96.0f);
    // The pointer rests in the top padding, just clear of the first row.
    int x = cursor.x - shadow - (int)std::lround((checks_ ? 34.0f : 16.0f) * dpi_ / 96.0f);
    int y = cursor.y - shadow - (int)std::lround(4.0f * dpi_ / 96.0f);

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    RECT bounds{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    if (monitor_ && GetMonitorInfoW(monitor_, &mi)) bounds = mi.rcMonitor;
    const int margin = 8;
    if (x + shadow + panelW > bounds.right - margin) x = bounds.right - margin - panelW - shadow;
    if (y + shadow + panelH > bounds.bottom - margin) y = bounds.bottom - margin - panelH - shadow;
    if (x + shadow < bounds.left + margin) x = bounds.left + margin - shadow;
    if (y + shadow < bounds.top + margin) y = bounds.top + margin - shadow;
    pos_ = {x, y};
    SetWindowPos(hwnd_, HWND_TOPMOST, pos_.x, pos_.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

void Menu::Render() {
    if (!hwnd_ || !renderer_.Begin()) return;
    const float a = alpha_;
    const D2D1_RECT_F panel = Rect(kShadow, kShadow, panelW_, panelH_);
    for (int i = 0; i < 6; ++i) {
        const float grow = 1.0f + i * 1.7f, fade = 1.0f - i / 6.0f;
        D2D1_RECT_F s{panel.left - grow, panel.top - grow * 0.4f, panel.right + grow, panel.bottom + grow};
        renderer_.FillRound(s, kRadius + grow, RGB(0, 0, 0), 0.08f * fade * fade * a);
    }
    renderer_.FillRound(panel, kRadius, background_, opacity_ / 100.0f * a);
    renderer_.StrokeRound(panel, kRadius, kWhite, 0.10f * a, 1.0f);

    const float textX = kShadow + (checks_ ? 34.0f : 16.0f);
    for (int i = 0; i < (int)rows_.size(); ++i) {
        const Row& row = rows_[i];
        if (row.item.id == 0) {
            renderer_.Line({panel.left + 14, row.rect.top}, {panel.right - 14, row.rect.top}, kWhite, 0.10f * a, 1.0f);
            continue;
        }
        const float alpha = row.item.enabled ? 0.92f : 0.32f;
        if (i == hover_ && row.item.enabled) {
            renderer_.FillRound(row.rect, 8.0f, kWhite, 0.12f * a);
        }
        if (row.item.checked) {
            const float cy = (row.rect.top + row.rect.bottom) / 2;
            DrawCheck(renderer_, {kShadow + 16.0f, cy}, accent_, a);
        }
        renderer_.DrawLabel(row.item.label, Rect(textX, row.rect.top, panel.right - 16.0f - textX, kItemH), kLabel,
                            kWhite, alpha * a);
    }
    if (!renderer_.End()) return;
    renderer_.Present(hwnd_, pos_);
}

int Menu::RowAt(D2D1_POINT_2F dip) const {
    for (int i = 0; i < (int)rows_.size(); ++i) {
        const D2D1_RECT_F& r = rows_[i].rect;
        if (rows_[i].item.id == 0 || !rows_[i].item.enabled) continue;
        if (dip.x >= r.left && dip.x < r.right && dip.y >= r.top && dip.y < r.bottom) return i;
    }
    return -1;
}

bool Menu::ContainsScreen(POINT p) const {
    const int shadow = (int)std::ceil(kShadow * dpi_ / 96.0f);
    const int panelW = (int)std::ceil(panelW_ * dpi_ / 96.0f);
    const int panelH = (int)std::ceil(panelH_ * dpi_ / 96.0f);
    RECT r{pos_.x + shadow, pos_.y + shadow, pos_.x + shadow + panelW, pos_.y + shadow + panelH};
    return PtInRect(&r, p) != FALSE;
}

void Menu::Tick() {
    const double now = NowMs();
    const float dt = (float)std::clamp((now - lastTick_) / 1000.0, 0.0, 0.05);
    lastTick_ = now;
    if (alpha_ < 1.0f) {
        alpha_ = std::min(1.0f, alpha_ + dt / 0.09f);
        Render();
    }
    if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) && now - openedAt_ > 150.0) {
        Close();
        return;
    }
    // The press that opened the menu is still down, and the cursor is often still on the notch rather than
    // the panel. Dismissing on that press makes the menu vanish as soon as it appears.
    const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) || (GetAsyncKeyState(VK_RBUTTON) & 0x8000);
    if (!down) seenUp_ = true;
    const bool opening = !seenUp_ || now - openedAt_ < 220.0;
    if (!opening && down && !wasDown_) {
        POINT p;
        GetCursorPos(&p);
        if (!ContainsScreen(p)) Close();
    }
    wasDown_ = down;
}

void Menu::Choose(int id) {
    auto cb = std::move(onSelect_);
    Close();
    if (cb && id > 0) cb(id);
}

LRESULT CALLBACK Menu::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<Menu*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<Menu*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->Handle(msg, wp, lp);
}

LRESULT Menu::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER:
            Tick();
            return 0;
        case WM_MOUSEMOVE: {
            const D2D1_POINT_2F dip{GET_X_LPARAM(lp) * 96.0f / dpi_, GET_Y_LPARAM(lp) * 96.0f / dpi_};
            const int hit = RowAt(dip);
            if (hit != hover_) {
                hover_ = hit;
                Render();
            }
            TRACKMOUSEEVENT ev{sizeof(ev), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&ev);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (hover_ != -1) {
                hover_ = -1;
                Render();
            }
            return 0;
        case WM_LBUTTONUP: {
            const D2D1_POINT_2F dip{GET_X_LPARAM(lp) * 96.0f / dpi_, GET_Y_LPARAM(lp) * 96.0f / dpi_};
            const int hit = RowAt(dip);
            if (hit >= 0) Choose(rows_[hit].item.id);
            else if (seenUp_ && NowMs() - openedAt_ >= 220.0) Close();
            return 0;
        }
        case WM_RBUTTONUP:
            if (seenUp_ && NowMs() - openedAt_ >= 220.0) Close();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            ValidateRect(hwnd_, nullptr);
            Render();
            return 0;
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, hover_ >= 0 ? IDC_HAND : IDC_ARROW));
            return TRUE;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace op
