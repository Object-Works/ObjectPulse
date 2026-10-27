#pragma once

#include "common.h"

namespace op {

// Master volume, the default microphone, and the monitor brightness behind the small notch.
class DeviceLevels {
public:
    DeviceLevels();
    ~DeviceLevels();
    DeviceLevels(const DeviceLevels&) = delete;
    DeviceLevels& operator=(const DeviceLevels&) = delete;

    // Reads the endpoints again. True when a value the notch shows has changed.
    bool Refresh(HMONITOR monitor);
    // Writes a brightness the fader changed, if it was still waiting.
    void Flush();

    float Volume() const { return volume_; }
    bool VolumeMuted() const { return volumeMuted_; }
    float Mic() const { return mic_; }
    bool MicMuted() const { return micMuted_; }
    bool HasMic() const { return hasMic_; }
    int Brightness(HMONITOR monitor) const;  // 0-100, or -1 when this monitor can't say

    void SetVolume(float unit);  // 0-1, and unmutes
    void SetMic(float unit);
    void SetBrightness(HMONITOR monitor, float unit);

private:
    struct Impl;
    Impl* impl_;

    float volume_ = 1.0f;
    float mic_ = 1.0f;
    bool volumeMuted_ = false;
    bool micMuted_ = false;
    bool hasMic_ = false;

    struct Bright {
        HMONITOR monitor = nullptr;
        int percent = -1;
    };
    Bright bright_[8]{};

    int* FindBrightness(HMONITOR monitor, bool create);
    const int* FindBrightness(HMONITOR monitor) const;
};

}  // namespace op
