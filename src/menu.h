#pragma once

#include "common.h"
#include "renderer.h"

#include <functional>
#include <string>
#include <vector>

namespace op {

// One row of the context menu. `id == 0` is a separator.
struct MenuItem {
    std::wstring label;
    int id = 0;
    bool enabled = true;
    bool checked = false;
};

// A rounded black menu drawn like the notch, instead of a Windows popup menu.
class Menu {
public:
    explicit Menu(HINSTANCE instance);
    ~Menu();
    Menu(const Menu&) = delete;
    Menu& operator=(const Menu&) = delete;

    // `cursor` is in screen pixels. `owner` keeps the menu above the notch. `onSelect` runs after it closes.
    void Show(HMONITOR monitor, float dpi, POINT cursor, HWND owner, COLORREF background, int opacity,
              COLORREF accent, std::vector<MenuItem> items, std::function<void(int)> onSelect);
    void Close();
    bool Open() const { return hwnd_ != nullptr; }

private:
    struct Row {
        MenuItem item;
        D2D1_RECT_F rect{};  // window DIPs; empty for a separator
    };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void Layout();
    void Place(POINT cursor);
    void Render();
    void Tick();
    int RowAt(D2D1_POINT_2F dip) const;
    bool ContainsScreen(POINT p) const;
    void Choose(int id);

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    Renderer renderer_;
    std::function<void(int)> onSelect_;
    std::vector<Row> rows_;
    HMONITOR monitor_ = nullptr;
    float dpi_ = 96.0f;
    COLORREF background_ = 0;
    COLORREF accent_ = 0;
    int opacity_ = 100;
    float panelW_ = 0, panelH_ = 0;
    bool checks_ = false;  // a column on the left, only when some row is checked
    POINT pos_{};
    int hover_ = -1;
    float alpha_ = 1.0f;
    double openedAt_ = 0;
    double lastTick_ = 0;
    bool seenUp_ = false;   // the button that opened the menu has been released
    bool wasDown_ = false;  // previous tick's button state, so only a new press dismisses
};

}  // namespace op
