#include "views.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <iterator>

namespace op {

namespace {

constexpr COLORREF kWhite = RGB(255, 255, 255);
constexpr COLORREF kWarn = RGB(0xfa, 0xb3, 0x87);
constexpr COLORREF kGood = RGB(0x8b, 0xe0, 0x8f);
constexpr COLORREF kLow = RGB(0xf3, 0x8b, 0xa8);

constexpr float kNoticeHeight = 60.0f;
constexpr float kCompactMinWidth = 200.0f;
constexpr float kExpandedMinWidth = 360.0f;
constexpr float kExpandedPad = 18.0f;
constexpr float kNameTop = 10.0f, kNameH = 46.0f, kAppsTop = 62.0f, kFrameTop = 108.0f;
constexpr float kOnlineExpandedHeight = kFrameTop + 26.0f + 14.0f;
constexpr float kOfflineExpandedHeight = kAppsTop + 32.0f + 14.0f;
constexpr float kButton = 30.0f, kButtonGap = 4.0f;
constexpr float kTaskButton = 32.0f, kTaskGap = 4.0f;
constexpr float kLayoutButtonW = 30.0f, kLayoutButtonH = 26.0f, kLayoutGap = 4.0f;
constexpr float kPauseButton = 26.0f;
const wchar_t* const kLayouts[] = {L"dwindle", L"master", L"monocle", L"scroll"};

TextStyle Style(float size, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL, Align align = Align::Left) {
    return {size, weight, align};
}

const TextStyle kClockStyle = Style(13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);

D2D1_RECT_F Rect(float x, float y, float w, float h) { return {x, y, x + w, y + h}; }
D2D1_POINT_2F Pt(float x, float y) { return {x, y}; }
D2D1_POINT_2F Center(const D2D1_RECT_F& r) { return {(r.left + r.right) / 2, (r.top + r.bottom) / 2}; }
bool Contains(const D2D1_RECT_F& r, D2D1_POINT_2F p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

struct ButtonSize {
    float size, gap;
};
ButtonSize WorkspaceButtons(int count) {
    if (count <= 10) return {26.0f, 6.0f};
    if (count <= 14) return {22.0f, 4.0f};
    return {18.0f, 3.0f};
}

// ---- glyphs ---------------------------------------------------------------

void DrawLayoutGlyph(Renderer& r, const std::wstring& layout, const D2D1_RECT_F& b, COLORREF color, float alpha) {
    const float w = b.right - b.left, h = b.bottom - b.top;
    const float gap = std::max(1.2f, w * 0.08f), radius = 1.5f;
    // Fractions of the glyph box; interior edges leave half a gap so tiles read as separate windows.
    auto box = [&](float x0, float y0, float x1, float y1, float a) {
        D2D1_RECT_F q{b.left + x0 * w + (x0 > 0 ? gap / 2 : 0), b.top + y0 * h + (y0 > 0 ? gap / 2 : 0),
                      b.left + x1 * w - (x1 < 1 ? gap / 2 : 0), b.top + y1 * h - (y1 < 1 ? gap / 2 : 0)};
        r.FillRound(q, radius, color, a);
    };
    if (layout == L"master") {
        box(0, 0, 0.55f, 1, alpha);
        box(0.55f, 0, 1, 0.34f, alpha);
        box(0.55f, 0.34f, 1, 0.67f, alpha);
        box(0.55f, 0.67f, 1, 1, alpha);
    } else if (layout == L"monocle") {
        box(0.14f, 0, 1, 0.84f, alpha * 0.45f);
        box(0, 0.16f, 0.86f, 1, alpha);
    } else if (layout == L"scroll") {
        r.Target()->PushAxisAlignedClip(b, D2D1_ANTIALIAS_MODE_ALIASED);
        box(-0.3f, 0, 0.14f, 1, alpha * 0.5f);
        box(0.2f, 0, 0.8f, 1, alpha);
        box(0.86f, 0, 1.3f, 1, alpha * 0.5f);
        r.Target()->PopAxisAlignedClip();
    } else {
        box(0, 0, 0.5f, 1, alpha);
        box(0.5f, 0, 1, 0.5f, alpha);
        box(0.5f, 0.5f, 0.75f, 1, alpha);
        box(0.75f, 0.5f, 1, 1, alpha);
    }
}

void DrawPauseBars(Renderer& r, D2D1_POINT_2F c, float h, COLORREF color, float alpha) {
    float bw = h * 0.26f;
    r.FillRound(Rect(c.x - bw * 1.4f, c.y - h / 2, bw, h), bw / 2, color, alpha);
    r.FillRound(Rect(c.x + bw * 0.4f, c.y - h / 2, bw, h), bw / 2, color, alpha);
}

void DrawPlay(Renderer& r, D2D1_POINT_2F c, float h, COLORREF color, float alpha) {
    r.FillTriangle(Pt(c.x - h * 0.36f, c.y - h / 2), Pt(c.x + h * 0.5f, c.y), Pt(c.x - h * 0.36f, c.y + h / 2), color,
                   alpha);
}

constexpr float kBatteryW = 23.0f, kBatteryH = 11.0f;

// Battery outline with its charge; `left`/`cy` place the body.
void DrawBattery(Renderer& r, const Battery& b, float left, float cy, float alpha) {
    const D2D1_RECT_F body = Rect(left, cy - kBatteryH / 2, kBatteryW - 2.5f, kBatteryH);
    r.StrokeRound(body, 3.0f, kWhite, 0.55f * alpha, 1.2f);
    r.FillRound(Rect(body.right + 0.8f, cy - 2.2f, 1.7f, 4.4f), 0.8f, kWhite, 0.55f * alpha);
    const float inner = body.right - body.left - 4.0f;
    const float fill = std::max(1.5f, inner * std::clamp(b.percent, 0, 100) / 100.0f);
    COLORREF color = b.charging ? kGood : b.percent <= 20 ? kLow : kWhite;
    r.FillRound(Rect(body.left + 2, body.top + 2, fill, kBatteryH - 4), 1.5f, color, alpha);
}

// Rounded square with the app's icon, or its initial when it has none. Without a window, the
// icon comes from `exePath`.
void DrawAppIcon(Renderer& r, HWND hwnd, const std::wstring& exe, const std::wstring& exePath, const D2D1_RECT_F& box,
                 COLORREF accent, float alpha = 1.0f) {
    if (ID2D1Bitmap* icon = hwnd ? r.WindowIcon(hwnd, exePath) : r.FileIcon(exePath)) {
        r.DrawBitmap(icon, box, alpha);
        return;
    }
    float size = box.right - box.left;
    r.FillRound(box, size * 0.28f, accent, 0.22f * alpha);
    std::wstring name = AppName(exe);
    std::wstring initial = name.empty() ? L"•" : std::wstring(1, (wchar_t)std::towupper(name[0]));
    r.DrawLabel(initial, box, Style(size * 0.5f, DWRITE_FONT_WEIGHT_BOLD, Align::Center), accent, alpha);
}

void DrawBadge(Renderer& r, const Notice& n, const D2D1_RECT_F& circle, COLORREF tint) {
    const float d = circle.right - circle.left;
    const D2D1_POINT_2F c = Center(circle);
    if (n.badge != Notice::Badge::Window) r.FillRound(circle, d / 2, tint, 0.2f);
    switch (n.badge) {
        case Notice::Badge::Number:
            r.DrawLabel(std::to_wstring(n.number), circle, Style(17.0f, DWRITE_FONT_WEIGHT_BOLD, Align::Center), tint,
                        1.0f);
            break;
        case Notice::Badge::Layout:
            DrawLayoutGlyph(r, n.layout, Rect(c.x - 9, c.y - 7, 18, 14), tint, 1.0f);
            break;
        case Notice::Badge::Pause:
            DrawPauseBars(r, c, 14.0f, tint, 1.0f);
            break;
        case Notice::Badge::Play:
            DrawPlay(r, Pt(c.x + 1, c.y), 14.0f, tint, 1.0f);
            break;
        case Notice::Badge::Window:
            DrawAppIcon(r, n.hwnd, n.subtitle, {}, Rect(c.x - 14, c.y - 14, 28, 28), tint);
            break;
        case Notice::Badge::Check:
            r.Line(Pt(c.x - 7, c.y + 0.5f), Pt(c.x - 2.5f, c.y + 5), tint, 1.0f, 2.4f);
            r.Line(Pt(c.x - 2.5f, c.y + 5), Pt(c.x + 7, c.y - 5), tint, 1.0f, 2.4f);
            break;
        case Notice::Badge::Cross:
            r.Line(Pt(c.x - 5.5f, c.y - 5.5f), Pt(c.x + 5.5f, c.y + 5.5f), tint, 1.0f, 2.4f);
            r.Line(Pt(c.x + 5.5f, c.y - 5.5f), Pt(c.x - 5.5f, c.y + 5.5f), tint, 1.0f, 2.4f);
            break;
        case Notice::Badge::Float:
            r.StrokeRound(Rect(c.x - 8, c.y - 7, 11, 9), 2.0f, tint, 0.55f, 1.6f);
            r.FillRound(Rect(c.x - 3, c.y - 2, 11, 9), 2.0f, tint, 1.0f);
            break;
        case Notice::Badge::Fullscreen: {
            const float s = 7.0f, k = 3.5f;
            for (int sx : {-1, 1}) {
                for (int sy : {-1, 1}) {
                    D2D1_POINT_2F corner{c.x + sx * s, c.y + sy * s};
                    r.Line(corner, Pt(corner.x - sx * k, corner.y), tint, 1.0f, 2.0f);
                    r.Line(corner, Pt(corner.x, corner.y - sy * k), tint, 1.0f, 2.0f);
                }
            }
            break;
        }
    }
}

// A round icon button; returns its rect.
D2D1_RECT_F IconButton(Renderer& r, const ViewModel& m, float x, float y, float size, bool active = false) {
    D2D1_RECT_F b = Rect(x, y, size, size);
    if (active) r.FillRound(b, size * 0.32f, m.accent, 0.22f);
    else if (Contains(b, m.mouse)) r.FillRound(b, size * 0.32f, kWhite, 0.12f);
    return b;
}

const Config& Settings(const ViewModel& m) {
    static const Config defaults;
    return m.config ? *m.config : defaults;
}

bool Shows(const ViewModel& m, const wchar_t* expandedItem) {
    return HasItem(Settings(m).expandedItems, expandedItem);
}

// A button in the expanded task row: a pinned app (with its windows here, if any) or a window.
struct TaskButton {
    const Task* task = nullptr;  // the window it stands for; null for a pinned app that isn't running
    const Pin* pin = nullptr;
    int windows = 0;
    bool attention = false;
};

std::vector<TaskButton> TaskButtons(const ViewModel& m) {
    std::vector<TaskButton> list;
    const std::vector<Task> none;
    const std::vector<Task>& tasks = m.tasks ? *m.tasks : none;
    std::vector<bool> grouped(tasks.size(), false);
    if (m.pins && Shows(m, L"pins")) {
        for (const Pin& pin : *m.pins) {
            TaskButton b;
            b.pin = &pin;
            for (size_t i = 0; i < tasks.size(); ++i) {
                const Task& t = tasks[i];
                if (!SamePath(t.exePath, pin.path)) continue;
                grouped[i] = true;
                ++b.windows;
                b.attention |= t.attention;
                // It stands for the focused window if that is one of them, else the first.
                if (!b.task || t.hwnd == m.foreground) b.task = &t;
            }
            list.push_back(b);
        }
    }
    if (Shows(m, L"tasks")) {
        for (size_t i = 0; i < tasks.size(); ++i) {
            if (grouped[i]) continue;
            TaskButton b;
            b.task = &tasks[i];
            b.windows = 1;
            b.attention = tasks[i].attention;
            list.push_back(b);
        }
    }
    return list;
}

std::wstring FileName(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

const Task* AttentionTask(const ViewModel& m) {
    if (!m.tasks) return nullptr;
    for (const Task& t : *m.tasks) {
        if (t.attention) return &t;
    }
    return nullptr;
}

// ---- layouts ----------------------------------------------------------------
// Each layout function measures when `area` is null (returning the content width) and draws otherwise.

float LayoutCompact(Renderer& r, const ViewModel& m, const D2D1_RECT_F* area) {
    const float pad = 16.0f, gap = 12.0f;
    const FrameState* s = m.state;
    const bool online = s && s->online;
    const Task* attention = AttentionTask(m);
    const TextStyle dateStyle = Style(12.0f);
    const float clockW = r.MeasureText(m.clock, kClockStyle);
    const float dateW = r.MeasureText(m.date, dateStyle);
    const int n = online ? s->workspaces : 0;
    const TextStyle wsStyle = Style(12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    const std::wstring wsText = n > 10 ? std::to_wstring(s->workspace) + L" / " + std::to_wstring(n) : L"";

    auto items = [&](float x, float cy, bool draw) {
        const float top = cy - kCompactHeight / 2, start = x;
        for (const std::wstring& item : Settings(m).compactItems) {
            const float before = x;
            if (item == L"clock" || item == L"date" || item == L"battery") {
                // Date and clock stay together on the left. The battery stays on the right.
            } else if (item == L"attention" && attention) {
                if (draw) {
                    r.FillRound(Rect(x - 3, cy - 11, 22, 22), 7.0f, kWarn, 0.28f);
                    DrawAppIcon(r, attention->hwnd, attention->exe, attention->exePath, Rect(x, cy - 8, 16, 16),
                                kWarn);
                }
                x += 16;
            } else if (item == L"paused" && online && s->paused) {
                if (draw) DrawPauseBars(r, Pt(x + 6, cy), 12.0f, kWarn, 1.0f);
                x += 12;
            } else if (item == L"workspaces" && n >= 1 && n <= 10) {
                for (int i = 1; i <= n; ++i) {
                    const bool active = i == s->workspace;
                    const float w = active ? 18.0f : 6.0f;
                    if (draw) {
                        COLORREF color = active || s->Visible(i) ? m.accent : kWhite;
                        float alpha = active ? 1.0f : s->Visible(i) ? 0.55f : s->Occupied(i) ? 0.7f : 0.2f;
                        r.FillRound(Rect(x, cy - 3, w, 6), 3.0f, color, alpha);
                    }
                    x += w + (i < n ? 5 : 0);
                }
            } else if (item == L"workspaces" && n > 10) {
                const float w = r.MeasureText(wsText, wsStyle);
                if (draw) r.DrawLabel(wsText, Rect(x, top, w, kCompactHeight), wsStyle, kWhite, 0.85f);
                x += w;
            } else if (item == L"clock") {
                if (draw) r.DrawLabel(m.clock, Rect(x, top, clockW, kCompactHeight), kClockStyle, kWhite, 0.95f);
                x += clockW;
            } else if (item == L"date") {
                if (draw) r.DrawLabel(m.date, Rect(x, top, dateW, kCompactHeight), dateStyle, kWhite, 0.55f);
                x += dateW;
            }
            if (x > before) x += gap;
        }
        return x > start ? x - gap : x;
    };

    const float mid = items(0, 0, false);
    bool wantClock = false, wantDate = false, wantBattery = false;
    for (const std::wstring& item : Settings(m).compactItems) {
        if (item == L"clock") wantClock = true;
        if (item == L"date") wantDate = true;
        if (item == L"battery") wantBattery = true;
    }
    const bool clock = wantClock && !m.clock.empty();
    const bool date = wantDate && !m.date.empty();
    const bool battery = wantBattery && m.battery.present;
    const float pairGap = date && clock ? 8.0f : 0.0f;
    const float leftW = (date ? dateW : 0.0f) + pairGap + (clock ? clockW : 0.0f);
    const float rightW = battery ? kBatteryW : 0.0f;
    float width = pad;
    if (leftW > 0) width += leftW;
    if (mid > 0) width += (width > pad ? gap : 0.0f) + mid;
    if (rightW > 0) width += (width > pad ? gap : 0.0f) + rightW;
    width += pad;
    // Without ObjectFrame there is nothing to put in the middle, so the pill hugs the clock and battery.
    if (online) width = std::max(kCompactMinWidth, width);
    if (area) {
        const float cy = area->top + kCompactHeight / 2;
        const float top = cy - kCompactHeight / 2;
        float x = area->left + pad;
        float right = area->right - pad;
        if (date) {
            r.DrawLabel(m.date, Rect(x, top, dateW, kCompactHeight), dateStyle, kWhite, 0.55f);
            x += dateW + pairGap;
        }
        if (clock) {
            r.DrawLabel(m.clock, Rect(x, top, clockW, kCompactHeight), kClockStyle, kWhite, 0.95f);
            x += clockW + gap;
        }
        if (battery) {
            DrawBattery(r, m.battery, right - kBatteryW, cy, 1.0f);
            right -= kBatteryW + gap;
        }
        if (mid > 0) items(x + std::max(0.0f, (right - x - mid) / 2), cy, true);
    }
    return width;
}

float LayoutNotice(Renderer& r, const ViewModel& m, const D2D1_RECT_F* area) {
    const Notice& n = *m.notice;
    const TextStyle titleStyle = Style(15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD), subStyle = Style(12.0f);
    const float textW = std::min(std::max(r.MeasureText(n.title, titleStyle), r.MeasureText(n.subtitle, subStyle)),
                                 340.0f);
    const float width = std::max(18 + 36 + 12 + textW + 26, 240.0f);
    if (!area) return width;

    const float cy = (area->top + area->bottom) / 2;
    float x = area->left + 18;
    DrawBadge(r, n, Rect(x, cy - 18, 36, 36), n.warn ? kWarn : m.accent);
    x += 36 + 12;
    if (n.subtitle.empty()) {
        r.DrawLabel(n.title, Rect(x, area->top, textW, kNoticeHeight), titleStyle, kWhite, 0.95f);
    } else {
        r.DrawLabel(n.title, Rect(x, cy - 19, textW, 21), titleStyle, kWhite, 0.95f);
        r.DrawLabel(n.subtitle, Rect(x, cy + 2, textW, 17), subStyle, kWhite, 0.55f);
    }
    return width;
}

std::wstring BatteryText(const Battery& b) { return std::to_wstring(b.percent) + L"%"; }

struct ExpandedSizes {
    float tray;   // quick settings, notifications, battery
    float frame;  // workspace buttons + layouts + pause
};

// The row of ObjectFrame controls under the apps.
bool HasFrameRow(const ViewModel& m) {
    return m.state && m.state->online && (Shows(m, L"workspaces") || Shows(m, L"layouts") || Shows(m, L"pause"));
}

ExpandedSizes MeasureExpanded(Renderer& r, const ViewModel& m) {
    ExpandedSizes s{};
    auto add = [&](float w) { s.tray += (s.tray > 0 ? kButtonGap : 0) + w; };
    if (Shows(m, L"battery") && m.battery.present) {
        add(kBatteryW + 6 + r.MeasureText(BatteryText(m.battery), Style(12.0f)) + 16);
    }
    if (HasFrameRow(m)) {
        if (Shows(m, L"workspaces")) {
            const int n = m.state->workspaces;
            const ButtonSize ws = WorkspaceButtons(n);
            s.frame += (n > 0 ? n * ws.size + (n - 1) * ws.gap : 0.0f) + 20;
        }
        if (Shows(m, L"layouts")) s.frame += 4 * kLayoutButtonW + 3 * kLayoutGap + 10;
        if (Shows(m, L"pause")) s.frame += kPauseButton;
    }
    return s;
}

float LayoutExpanded(Renderer& r, const ViewModel& m, const D2D1_RECT_F* area, std::vector<Hit>* hits) {
    const float pad = kExpandedPad;
    const FrameState* s = m.state;
    const ExpandedSizes sz = MeasureExpanded(r, m);
    const std::vector<TaskButton> buttons = TaskButtons(m);
    const size_t taskCount = buttons.size();
    const float tasksW = taskCount ? taskCount * kTaskButton + (taskCount - 1) * kTaskGap : 0.0f;
    const float want = std::max(pad * 2 + tasksW + 24 + sz.tray, pad * 2 + sz.frame);
    const float width = std::min(std::max(kExpandedMinWidth, want), kMaxViewWidth);
    if (!area) return width;

    auto hit = [&](const D2D1_RECT_F& rect, std::wstring command) {
        if (hits) hits->push_back({rect, std::move(command)});
    };
    const float left = area->left + pad, right = area->right - pad;
    const float row2 = area->top + kAppsTop;

    // Clock stays at the right of the name row. Apps and the tray sit on the row below.
    float limit = right;
    if (Shows(m, L"clock")) {
        const TextStyle clockStyle = Style(16.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, Align::Right);
        const TextStyle dateStyle = Style(12.0f, DWRITE_FONT_WEIGHT_NORMAL, Align::Right);
        const float clockW = r.MeasureText(m.clock, clockStyle);
        const float dateW = m.date.empty() ? 0.0f : r.MeasureText(m.date, dateStyle);
        const float pairGap = dateW > 0 ? 8.0f : 0.0f;
        const float total = dateW + pairGap + clockW;
        const float row1 = area->top + kNameTop;
        const float origin = right - total;
        D2D1_RECT_F clock = Rect(origin - 8, row1, total + 16, kNameH);
        if (Contains(clock, m.mouse)) r.FillRound(clock, 10.0f, kWhite, 0.1f);
        if (dateW > 0) r.DrawLabel(m.date, Rect(origin, row1, dateW, kNameH), dateStyle, kWhite, 0.55f);
        r.DrawLabel(m.clock, Rect(origin + dateW + pairGap, row1, clockW, kNameH), clockStyle, kWhite, 0.95f);
        hit(clock, kNotificationsAction);
        limit = clock.left - 12;
    }

    float x = left;
    float tx = right;
    if (Shows(m, L"battery") && m.battery.present) {
        const TextStyle style = Style(12.0f);
        const std::wstring text = BatteryText(m.battery);
        const float w = kBatteryW + 6 + r.MeasureText(text, style) + 16;
        tx -= w;
        D2D1_RECT_F pill = Rect(tx, row2 + 1, w, kButton);
        if (Contains(pill, m.mouse)) r.FillRound(pill, kButton * 0.32f, kWhite, 0.12f);
        DrawBattery(r, m.battery, tx + 8, row2 + 16, 1.0f);
        r.DrawLabel(text, Rect(tx + 8 + kBatteryW + 6, row2 + 1, w - kBatteryW - 22, kButton), style, kWhite, 0.8f);
        hit(pill, kQuickSettingsAction);
        tx -= kButtonGap;
    }

    // Pinned and running apps from the left edge up to the tray.
    const TaskButton* hovered = nullptr;
    const TaskButton* focused = nullptr;
    {
        const float avail = tx - 12 - x;
        size_t fit = (size_t)std::max(0.0f, std::floor((avail + kTaskGap) / (kTaskButton + kTaskGap)));
        const bool overflow = taskCount > fit;
        const size_t shown = overflow && fit > 0 ? fit - 1 : std::min(fit, taskCount);
        for (size_t i = 0; i < taskCount; ++i) {
            const TaskButton& tb = buttons[i];
            const bool active = tb.task && tb.task->hwnd == m.foreground;
            if (active) focused = &tb;
            if (i >= shown) continue;
            D2D1_RECT_F box = Rect(x, row2, kTaskButton, kTaskButton);
            const bool hover = Contains(box, m.mouse);
            if (hover) hovered = &tb;
            if (tb.attention) r.FillRound(box, 10.0f, kWarn, 0.3f);
            else if (active) r.FillRound(box, 10.0f, kWhite, 0.14f);
            else if (hover) r.FillRound(box, 10.0f, kWhite, 0.09f);
            const D2D1_RECT_F iconBox = Rect(x + 6, row2 + 6, 20, 20);
            const float iconAlpha = active || hover ? 1.0f : 0.85f;
            if (tb.pin) {
                DrawAppIcon(r, nullptr, FileName(tb.pin->path), tb.pin->path, iconBox, m.accent, iconAlpha);
            } else {
                DrawAppIcon(r, tb.task->hwnd, tb.task->exe, tb.task->exePath, iconBox, m.accent, iconAlpha);
            }
            // Running: a bar like the taskbar's, wider for several windows. Pinned but closed: none.
            if (tb.windows > 0) {
                const float bar = active ? 12.0f : tb.windows > 1 ? 8.0f : 4.0f;
                r.FillRound(Rect(x + (kTaskButton - bar) / 2, row2 + kTaskButton - 3, bar, 2.5f), 1.25f,
                            tb.attention ? kWarn : active ? m.accent : kWhite, active || tb.attention ? 1.0f : 0.4f);
            }
            hit(box, tb.pin ? PinAction(tb.pin->path) : WindowAction(tb.task->hwnd));
            x += kTaskButton + kTaskGap;
        }
        if (overflow && fit > 0) {
            D2D1_RECT_F more = IconButton(r, m, x, row2, kTaskButton);
            r.DrawLabel(L"+" + std::to_wstring(taskCount - shown), more,
                        Style(12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, Align::Center), kWhite, 0.75f);
            hit(more, kTaskViewAction);
        }
    }

    // The active app, or the one under the pointer: its icon and name. No connection status.
    {
        const TaskButton* tb = hovered ? hovered : focused;
        const float row1 = area->top + kNameTop;
        const D2D1_RECT_F icon = Rect(left, row1 + (kNameH - 30.0f) / 2.0f, 30, 30);
        std::wstring name = L"デスクトップ";
        if (tb && tb->pin && !tb->task) {
            DrawAppIcon(r, nullptr, FileName(tb->pin->path), tb->pin->path, icon, m.accent);
            name = tb->pin->name;
        } else if (tb && tb->task) {
            DrawAppIcon(r, tb->task->hwnd, tb->task->exe, tb->task->exePath, icon, m.accent);
            name = tb->pin ? tb->pin->name : AppName(tb->task->exe);
        } else {
            r.FillRound(icon, 9.0f, kWhite, 0.08f);
            const wchar_t* layout = s && s->online ? s->layout.c_str() : L"dwindle";
            DrawLayoutGlyph(r, layout, Rect(icon.left + 7, icon.top + 9, 16, 12), m.accent, 1.0f);
        }
        const float textX = icon.right + 10;
        r.DrawLabel(name, Rect(textX, row1, std::max(0.0f, limit - textX), kNameH),
                    Style(14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD), kWhite, 0.95f);
    }

    if (!HasFrameRow(m)) return width;

    // Workspace buttons on the left, layouts and pause on the right.
    const float row3 = area->top + kFrameTop;
    r.Line(Pt(left, row3 - 7), Pt(right, row3 - 7), kWhite, 0.08f, 1.0f);
    const int n = Shows(m, L"workspaces") ? s->workspaces : 0;
    const ButtonSize ws = WorkspaceButtons(n);
    float bx = left;
    const float by = row3 + (kLayoutButtonH - ws.size) / 2;
    for (int i = 1; i <= n; ++i) {
        D2D1_RECT_F wb = Rect(bx, by, ws.size, ws.size);
        const float radius = ws.size / 2;
        const bool active = i == s->workspace, hover = Contains(wb, m.mouse);
        COLORREF text = kWhite;
        float textAlpha = 0.35f;
        if (active) {
            r.FillRound(wb, radius, m.accent, 1.0f);
            text = m.background;
            textAlpha = 1.0f;
        } else {
            if (s->Occupied(i)) {
                r.FillRound(wb, radius, kWhite, 0.12f);
                textAlpha = 0.9f;
            }
            if (s->Visible(i)) {
                r.StrokeRound(wb, radius, m.accent, 0.9f, 1.5f);
                textAlpha = 0.9f;
            }
            if (hover) r.FillRound(wb, radius, kWhite, 0.14f);
        }
        r.DrawLabel(std::to_wstring(i), wb, Style(ws.size * 0.46f, DWRITE_FONT_WEIGHT_SEMI_BOLD, Align::Center), text,
                    textAlpha);
        hit(wb, L"workspace " + std::to_wstring(i));
        bx += ws.size + ws.gap;
    }

    const bool layouts = Shows(m, L"layouts"), pauseButton = Shows(m, L"pause");
    float lx = right - (layouts ? 4 * kLayoutButtonW + 3 * kLayoutGap : 0) - (layouts && pauseButton ? 10 : 0) -
               (pauseButton ? kPauseButton : 0);
    if (layouts) {
        for (const wchar_t* name : kLayouts) {
            D2D1_RECT_F lb = Rect(lx, row3, kLayoutButtonW, kLayoutButtonH);
            const bool active = s->layout == name;
            if (active) r.FillRound(lb, 9.0f, m.accent, 0.22f);
            else if (Contains(lb, m.mouse)) r.FillRound(lb, 9.0f, kWhite, 0.1f);
            const D2D1_POINT_2F c = Center(lb);
            DrawLayoutGlyph(r, name, Rect(c.x - 8, c.y - 6, 16, 12), active ? m.accent : kWhite,
                            active ? 1.0f : 0.6f);
            hit(lb, std::wstring(L"layout ") + name);
            lx += kLayoutButtonW + kLayoutGap;
        }
        lx += 10 - kLayoutGap;
    }
    if (!pauseButton) return width;
    D2D1_RECT_F pause = Rect(lx, row3, kPauseButton, kPauseButton);
    const D2D1_POINT_2F pc = Center(pause);
    if (s->paused) {
        r.FillRound(pause, kPauseButton / 2, kWarn, 0.22f);
        DrawPlay(r, Pt(pc.x + 1, pc.y), 11.0f, kWarn, 1.0f);
    } else {
        if (Contains(pause, m.mouse)) r.FillRound(pause, kPauseButton / 2, kWhite, 0.1f);
        DrawPauseBars(r, pc, 11.0f, kWhite, 0.6f);
    }
    hit(pause, L"pause");
    return width;
}

}  // namespace

std::wstring WindowAction(HWND hwnd) {
    wchar_t buf[32];
    swprintf_s(buf, L"%llx", (unsigned long long)(ULONG_PTR)hwnd);
    return kWindowActionPrefix + std::wstring(buf);
}

HWND WindowFromAction(const std::wstring& command) {
    const size_t prefix = std::wcslen(kWindowActionPrefix);
    if (command.compare(0, prefix, kWindowActionPrefix) != 0) return nullptr;
    return reinterpret_cast<HWND>((ULONG_PTR)std::wcstoull(command.c_str() + prefix, nullptr, 16));
}

std::wstring PinAction(const std::wstring& exePath) { return kPinActionPrefix + exePath; }

std::wstring PinFromAction(const std::wstring& command) {
    const size_t prefix = std::wcslen(kPinActionPrefix);
    if (command.compare(0, prefix, kPinActionPrefix) != 0) return {};
    return command.substr(prefix);
}

ViewMetrics MeasureView(Mode mode, const ViewModel& m, Renderer& r) {
    if (mode == Mode::Hidden) return {{kCompactMinWidth * 0.6f, 0.0f}, 0.0f};
    if (mode == Mode::Notice && m.notice) return {{LayoutNotice(r, m, nullptr), kNoticeHeight}, 22.0f};
    if (mode == Mode::Expanded) {
        float h = HasFrameRow(m) ? kOnlineExpandedHeight : kOfflineExpandedHeight;
        return {{LayoutExpanded(r, m, nullptr, nullptr), h}, 26.0f};
    }
    return {{LayoutCompact(r, m, nullptr), kCompactHeight}, kNotchRadius};
}

void DrawView(Mode mode, const ViewModel& m, Renderer& r, const D2D1_RECT_F& area, std::vector<Hit>* hits) {
    if (mode == Mode::Notice && m.notice) LayoutNotice(r, m, &area);
    else if (mode == Mode::Expanded) LayoutExpanded(r, m, &area, hits);
    else if (mode == Mode::Compact) LayoutCompact(r, m, &area);
}

std::wstring LayoutLabel(const std::wstring& layout) {
    if (layout == L"dwindle") return L"Dwindle";
    if (layout == L"master") return L"Master";
    if (layout == L"monocle") return L"Monocle";
    if (layout == L"scroll") return L"Scroll";
    return layout;
}

std::wstring AppName(const std::wstring& exe) {
    if (exe.size() > 4 && ToLower(exe.substr(exe.size() - 4)) == L".exe") return exe.substr(0, exe.size() - 4);
    return exe;
}

}  // namespace op
