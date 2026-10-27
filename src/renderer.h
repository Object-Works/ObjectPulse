#pragma once

#include "common.h"

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <map>
#include <string>
#include <tuple>
#include <unordered_map>

namespace op {

using Microsoft::WRL::ComPtr;

enum class Align { Left, Center, Right };

struct TextStyle {
    float size = 12.0f;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    Align align = Align::Left;
};

// Draws with Direct2D into a premultiplied 32-bit DIB and pushes it to a layered window.
// Drawing coordinates are DIPs; the DPI given to Resize() is applied by the render target.
class Renderer {
public:
    Renderer() = default;
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool Init();
    bool Resize(int widthPx, int heightPx, float dpi);
    bool Begin();
    // Finishes the frame. Returns false if the device was lost; resources are recreated on the next Begin().
    bool End();
    // `sourcePx` shows only that part of the bitmap, so the window can shrink to the notch.
    void Present(HWND hwnd, POINT screenPos, const RECT* sourcePx = nullptr);

    ID2D1RenderTarget* Target() const { return target_.Get(); }
    ID2D1Factory* Factory() const { return factory_.Get(); }
    ID2D1StrokeStyle* RoundStroke() const { return roundCaps_.Get(); }
    int WidthPx() const { return width_; }
    int HeightPx() const { return height_; }

    ID2D1SolidColorBrush* Brush(COLORREF color, float alpha);
    void FillRound(const D2D1_RECT_F& r, float radius, COLORREF color, float alpha);
    void StrokeRound(const D2D1_RECT_F& r, float radius, COLORREF color, float alpha, float width = 1.0f);
    void Line(D2D1_POINT_2F a, D2D1_POINT_2F b, COLORREF color, float alpha, float width);  // round caps
    void FillTriangle(D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_POINT_2F c, COLORREF color, float alpha);
    void DrawBitmap(ID2D1Bitmap* bitmap, const D2D1_RECT_F& r, float alpha);
    // A notch hanging from the top of `body`: flat top, rounded bottom corners, and concave `ear`
    // fillets that blend the top corners into the screen edge outside `body`.
    ComPtr<ID2D1PathGeometry> NotchPath(const D2D1_RECT_F& body, float radius, float ear);
    float MeasureText(const std::wstring& text, const TextStyle& style);
    // Single line, vertically centered in `r`, cut with an ellipsis when it doesn't fit.
    void DrawLabel(const std::wstring& text, const D2D1_RECT_F& r, const TextStyle& style, COLORREF color,
                   float alpha);
    // The window's icon (large if available), else the icon of `exePath`; cached per window.
    ID2D1Bitmap* WindowIcon(HWND hwnd, const std::wstring& exePath = {});
    // The icon of a file, for pinned apps that have no window; cached per path.
    ID2D1Bitmap* FileIcon(const std::wstring& path);

private:
    IDWriteTextFormat* Format(const TextStyle& style);
    ComPtr<ID2D1Bitmap> BitmapFromIcon(HICON icon);
    bool EnsureTarget();
    void DropDeviceResources();
    void FreeSurface();

    ComPtr<ID2D1Factory> factory_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<IWICImagingFactory> wic_;
    ComPtr<IDWriteInlineObject> ellipsis_;
    ComPtr<ID2D1StrokeStyle> roundCaps_;
    ComPtr<ID2D1DCRenderTarget> target_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    std::map<std::tuple<int, int, int>, ComPtr<IDWriteTextFormat>> formats_;
    std::unordered_map<HWND, ComPtr<ID2D1Bitmap>> icons_;
    std::map<std::wstring, ComPtr<ID2D1Bitmap>> fileIcons_;
    std::wstring family_;

    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ oldBitmap_ = nullptr;
    int width_ = 0, height_ = 0;
    float dpi_ = 96.0f;
};

}  // namespace op
