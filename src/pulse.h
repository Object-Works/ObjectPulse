#pragma once

#include "frame_link.h"
#include "renderer.h"
#include "taskbar.h"
#include "views.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace op {

class App;
class Menu;

// Critically-ish damped spring; a little overshoot gives the notch its bounce.
struct Spring {
    float value = 0.0f;
    float velocity = 0.0f;
    float target = 0.0f;

    void Step(float dt);
    bool Settled() const { return value == target && velocity == 0.0f; }
    void Snap() {
        value = target;
        velocity = 0.0f;
    }
};

// A notch hanging from the top edge of one monitor that stands in for the taskbar there. It
// lists the windows on its monitor, shows ObjectFrame's workspace and layout for it, grows to
// announce changes, and expands under the mouse.
class Pulse {
public:
    // `index` is the monitor's 1-based number, left to right like ObjectFrame's.
    // `ownWindows` limits the task list to windows on this monitor.
    Pulse(App& app, HMONITOR monitor, int index, bool ownWindows);
    ~Pulse();
    Pulse(const Pulse&) = delete;
    Pulse& operator=(const Pulse&) = delete;

    static bool RegisterWindowClass(HINSTANCE instance);
    bool Create();

    int Index() const { return index_; }
    // Shown unless notices are turned off or the notch is expanded, or `always`.
    void ShowNotice(Notice notice, bool always = false);
    void OnStateChanged();       // ObjectFrame's state or the app's settings changed
    void OnClockTick();          // once a second: clock, battery, window titles
    void OnShellHook(WPARAM code, HWND hwnd);
    void OnTaskbarCreated();     // Explorer restarted and forgot every app bar
    void CheckEdge();            // opens a hidden notch when the cursor touches the top edge
    void Place();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);

    // Presentation
    void UpdateView();
    ViewModel Model() const;
    void Refresh();
    void Render();
    void Tick();
    void StartAnimation();
    void StopAnimation();
    void UpdateVisibility();

    // Placement
    void UpdateAppBar(bool force);
    void RemoveAppBar();
    bool ForegroundIsFullscreen() const;

    // Input
    void RunAction(const std::wstring& command);
    std::vector<HWND> WindowsOf(const std::wstring& exePath) const;  // listed windows of that exe
    void OpenPinned(const std::wstring& exePath);
    D2D1_POINT_2F ToDip(POINT clientPx) const;
    D2D1_POINT_2F FromClient(POINT clientPx) const;  // canvas DIPs, including a cropped window's origin
    void ApplySlider(float canvasX);
    void EndSlider();
    void PresentFrame();
    int HitAt(D2D1_POINT_2F p) const;
    bool InEdgeZone() const;  // cursor at the top edge above the notch
    void StartPolling();
    void Poll();
    void OnClick(D2D1_POINT_2F p);
    void OnMiddleClick(D2D1_POINT_2F p);
    void ShowMenu(D2D1_POINT_2F p);
    void RunMenuCommand(int cmd, const std::wstring& exePath, const std::vector<HWND>& windows);

    App& app_;
    HMONITOR monitor_;
    int index_;
    HWND hwnd_ = nullptr;
    Renderer renderer_;
    TaskList tasks_;
    bool ownWindows_;
    FrameState view_;  // ObjectFrame's state as seen from this monitor

    std::optional<Notice> notice_;
    std::optional<Notice> lastNotice_;  // keeps an expired notice drawable while it fades out

    Mode mode_ = Mode::Hidden;
    Mode previousMode_ = Mode::Hidden;
    bool fadePrevious_ = false;
    double modeChangedAt_ = 0;
    Spring width_, height_, radius_;
    bool animating_ = false;
    double lastTick_ = 0;

    bool hovering_ = false;
    bool hoverSuppressed_ = false;  // collapsed by a click; stays so until the mouse leaves
    bool polling_ = false;
    bool rightWasDown_ = false;
    double menuShownAt_ = 0;
    double insideSince_ = 0;
    double outsideSince_ = 0;
    D2D1_POINT_2F mouse_{-1000.0f, -1000.0f};
    int hoverHit_ = -1;
    int wheel_ = 0;  // accumulated wheel delta, so precision touchpads don't switch on every tiny step
    std::vector<Hit> hits_;
    D2D1_RECT_F pill_{};
    std::wstring dragging_;  // a !slider command while the fader is held
    D2D1_RECT_F dragTrack_{};
    float originX_ = 0, originY_ = 0;  // canvas DIPs of the cropped window's top-left

    RECT monitorRect_{};
    float dpi_ = 96.0f;  // includes the configured scale
    float canvasW_ = 0, canvasH_ = 0;
    POINT windowPos_{};
    bool appBar_ = false;
    RECT appBarRect_{};
    bool fullscreenApp_ = false;
    bool hidden_ = false;
    std::unique_ptr<Menu> menu_;
};

}  // namespace op
