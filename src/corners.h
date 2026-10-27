#pragma once

#include "config.h"
#include "renderer.h"

#include <memory>
#include <vector>

namespace op {

// Black rounded caps in the four corners of a monitor, in the same material as the notch.
class ScreenCorners {
public:
    void Apply(HINSTANCE instance, const std::vector<HMONITOR>& monitors, const Config& config);
    void SetHidden(HMONITOR monitor, bool hidden);
    void Clear();

private:
    struct Set {
        HMONITOR monitor = nullptr;
        HWND hwnd[4] = {};
        POINT pos[4] = {};
        Renderer renderer;
        float dpi = 96.0f;
        int sizePx = 0;
        bool hidden = false;
    };

    void Paint(Set& set);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HINSTANCE instance_ = nullptr;
    Config config_;
    // Renderer owns a DC, so a set is never moved.
    std::vector<std::unique_ptr<Set>> sets_;
};

}  // namespace op
