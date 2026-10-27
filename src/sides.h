#pragma once

#include "common.h"

#include <memory>
#include <vector>

namespace op {

class App;

// A brightness notch sits on the left edge of each monitor and a volume/microphone notch on the
// right, both at the vertical center, whether or not the top notch stays small. They stay hidden
// until the pointer reaches that edge, then grow out in the same notch shape as the top.
class SideIslands {
public:
    SideIslands();
    ~SideIslands();
    SideIslands(const SideIslands&) = delete;
    SideIslands& operator=(const SideIslands&) = delete;

    void Apply(App& app, const std::vector<HMONITOR>& monitors);
    void SetHidden(HMONITOR monitor, bool hidden);
    void CheckHover();
    void Refresh();
    void Clear();

private:
    struct Island;

    void Paint(Island& island);
    void Animate(Island& island);
    void Hide(Island& island);
    static bool Hot(const Island& island, POINT cursor);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    App* app_ = nullptr;
    std::vector<std::unique_ptr<Island>> islands_;
};

}  // namespace op
