#pragma once

#include "config.h"
#include "frame_link.h"
#include "renderer.h"
#include "taskbar.h"

#include <string>
#include <vector>

namespace op {

enum class Mode {
    Hidden,    // nothing to show: tucked into the top edge
    Compact,   // the notch: battery, workspace dots, clock
    Notice,    // briefly grown to announce a change
    Expanded,  // under the mouse: running apps, system buttons, workspaces and layouts
};

struct Notice {
    enum class Badge { Number, Layout, Pause, Play, Window, Check, Cross, Float, Fullscreen };
    Badge badge = Badge::Check;
    std::wstring title;
    std::wstring subtitle;
    int number = 0;       // Badge::Number
    std::wstring layout;  // Badge::Layout
    HWND hwnd = nullptr;  // Badge::Window; `subtitle` is then the app name
    bool warn = false;    // tinted orange instead of the accent color
};

// A clickable area in window DIPs and what it does: an ObjectFrame command, or one of the
// pulse's own actions starting with '!' (see the k*Action constants).
struct Hit {
    D2D1_RECT_F rect;
    std::wstring command;
};

constexpr const wchar_t* kTaskViewAction = L"!taskview";  // the "+N" button when apps overflow
constexpr const wchar_t* kQuickSettingsAction = L"!quick";
constexpr const wchar_t* kNotificationsAction = L"!notify";
constexpr const wchar_t* kWindowActionPrefix = L"!win:";  // followed by the HWND in hex
constexpr const wchar_t* kPinActionPrefix = L"!pin:";     // followed by the pinned exe path
constexpr const wchar_t* kBrightnessSlider = L"!slider:brightness";
constexpr const wchar_t* kVolumeSlider = L"!slider:volume";
constexpr const wchar_t* kMicSlider = L"!slider:mic";

std::wstring WindowAction(HWND hwnd);
HWND WindowFromAction(const std::wstring& command);  // null if `command` isn't a window action
std::wstring PinAction(const std::wstring& exePath);
std::wstring PinFromAction(const std::wstring& command);  // empty if `command` isn't a pin action

struct Battery {
    bool present = false;
    int percent = 0;
    bool charging = false;
};

struct ViewModel {
    const FrameState* state = nullptr;
    const Notice* notice = nullptr;
    const std::vector<Task>* tasks = nullptr;
    const std::vector<Pin>* pins = nullptr;
    const Config* config = nullptr;  // which items to show
    HWND foreground = nullptr;
    Battery battery;
    COLORREF accent = 0;
    COLORREF background = 0;
    std::wstring clock;
    std::wstring date;
    D2D1_POINT_2F mouse{-1000.0f, -1000.0f};  // window DIPs, for hover highlights
    float volume = 1.0f;      // master output, 0-1; drawn on the always-small notch
    bool volumeMuted = false;
    float mic = 1.0f;
    bool micMuted = false;
    bool hasMic = false;
    int brightness = -1;  // 0-100 for this monitor, or -1 when it can't be read yet
};

struct ViewMetrics {
    D2D1_SIZE_F size;
    float radius;  // of the notch's bottom corners
};

constexpr float kCompactHeight = 26.0f;
constexpr float kNotchRadius = 13.0f;  // bottom corners of the compact notch; the screen corners use this too
constexpr float kMaxViewHeight = 150.0f;
constexpr float kMaxViewWidth = 480.0f;
constexpr float kNotchEar = 10.0f;  // the concave fillets joining the notch to the screen edge

ViewMetrics MeasureView(Mode mode, const ViewModel& model, Renderer& r);
// Draws the content of `mode` into `area`, which has the size MeasureView returned.
void DrawView(Mode mode, const ViewModel& model, Renderer& r, const D2D1_RECT_F& area, std::vector<Hit>* hits);

std::wstring LayoutLabel(const std::wstring& layout);
std::wstring AppName(const std::wstring& exe);

}  // namespace op
