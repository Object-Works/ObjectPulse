#pragma once

#include "config.h"
#include "frame_link.h"
#include "views.h"

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace op {

class Pulse;
class ScreenCorners;
class SideIslands;
class DeviceLevels;

// Owns everything shared by the notches: the config, ObjectFrame's state, the battery, the clock,
// and the hidden Windows taskbar. Its hidden window is the one ObjectFrame sends its state to.
class App {
public:
    static App& Instance();

    bool Init(HINSTANCE instance, const std::wstring& configPath);
    int Run();
    void Quit();

    HINSTANCE Module() const { return instance_; }
    const Config& Settings() const { return config_; }
    const std::wstring& ConfigPath() const { return configPath_; }
    const FrameState& State() const { return state_; }
    const Battery& BatteryState() const { return battery_; }
    const std::wstring& Clock() const { return clock_; }
    const std::wstring& Date() const { return date_; }
    COLORREF Accent() const;
    bool MultiMonitor() const { return pulses_.size() > 1; }
    DeviceLevels& Devices();

    bool TaskbarHidden() const { return taskbarHidden_; }
    void ToggleTaskbar();
    void Reload();
    // Fullscreen hides the rounded corners and the side islands on that monitor, along with the notch.
    void SetCornersHidden(HMONITOR monitor, bool hidden);
    void RefreshSides();

    // Pinned apps, shared by every notch and saved to pins.txt next to the config.
    const std::vector<Pin>& Pins() const { return pins_; }
    bool IsPinned(const std::wstring& exePath) const;
    void TogglePin(const std::wstring& exePath);
    void MovePin(const std::wstring& exePath, int delta);  // -1 = left, +1 = right

    // Turns an item of the compact or expanded view on or off and writes it to the config file.
    void ToggleItem(bool expanded, const std::wstring& item);
    void ToggleClockSeconds();
    // Shown on the notch of the monitor ObjectFrame has focused (or the first one).
    void ShowNotice(const Notice& notice, bool always = false);

private:
    App() = default;
    ~App();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);

    void BuildPulses();
    void ApplyCorners(const std::vector<HMONITOR>& monitors);
    void ApplySides(const std::vector<HMONITOR>& monitors);
    void OnFrameState(const std::wstring& text);
    void OnFrameOffline();
    void DetectNotice(const FrameState& before, const FrameState& after);
    bool UpdateClock();
    bool UpdateBattery();
    void ApplyTaskbar();
    void ForEachPulse(void (Pulse::*method)());
    std::wstring PinsPath() const;
    void LoadPinList();
    void SavePinList();

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    UINT taskbarCreatedMsg_ = 0;
    UINT shellHookMsg_ = 0;
    std::wstring configPath_;
    Config config_;

    FrameState state_;
    bool helloSent_ = false;     // the next state answers our own request, so it isn't announced
    HWND helloFrame_ = nullptr;  // ObjectFrame instance last asked for its state
    std::deque<std::wstring> pendingStates_;

    Battery battery_;
    bool taskbarHidden_ = false;
    std::wstring clock_, date_;
    std::vector<Pin> pins_;
    std::unique_ptr<DeviceLevels> devices_;
    std::vector<std::unique_ptr<Pulse>> pulses_;
    std::unique_ptr<ScreenCorners> corners_;
    std::unique_ptr<SideIslands> sides_;
};

}  // namespace op
