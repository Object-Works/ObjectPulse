#include "renderer.h"

#include <shellapi.h>

#include <algorithm>
#include <cmath>

namespace op {

namespace {

D2D1_COLOR_F ToColor(COLORREF c, float alpha) {
    return D2D1::ColorF(GetRValue(c) / 255.0f, GetGValue(c) / 255.0f, GetBValue(c) / 255.0f, alpha);
}

}  // namespace

Renderer::~Renderer() {
    DropDeviceResources();
    FreeSurface();
}

bool Renderer::Init() {
    if (factory_ && dwrite_) return true;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf()))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf())))) {
        return false;
    }
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic_.GetAddressOf()));
    factory_->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                            D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                                nullptr, 0, roundCaps_.GetAddressOf());

    // Segoe UI Variable (Windows 11) where available; Japanese text falls back to the system's CJK font.
    family_ = L"Segoe UI";
    ComPtr<IDWriteFontCollection> fonts;
    if (SUCCEEDED(dwrite_->GetSystemFontCollection(fonts.GetAddressOf()))) {
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (SUCCEEDED(fonts->FindFamilyName(L"Segoe UI Variable Text", &index, &exists)) && exists) {
            family_ = L"Segoe UI Variable Text";
        }
    }
    return true;
}

void Renderer::FreeSurface() {
    if (dc_) {
        if (oldBitmap_) SelectObject(dc_, oldBitmap_);
        DeleteDC(dc_);
    }
    if (bitmap_) DeleteObject(bitmap_);
    dc_ = nullptr;
    bitmap_ = nullptr;
    oldBitmap_ = nullptr;
}

void Renderer::DropDeviceResources() {
    icons_.clear();
    fileIcons_.clear();
    brush_.Reset();
    target_.Reset();
}

bool Renderer::Resize(int widthPx, int heightPx, float dpi) {
    if (widthPx == width_ && heightPx == height_ && dpi == dpi_ && dc_) return true;
    FreeSurface();
    width_ = std::max(widthPx, 1);
    height_ = std::max(heightPx, 1);
    dpi_ = dpi;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = width_;
    bi.bmiHeader.biHeight = -height_;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    dc_ = CreateCompatibleDC(screen);
    void* bits = nullptr;
    bitmap_ = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!dc_ || !bitmap_) {
        FreeSurface();
        return false;
    }
    oldBitmap_ = SelectObject(dc_, bitmap_);
    return true;
}

bool Renderer::EnsureTarget() {
    if (!dc_ || !factory_) return false;
    auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    if (!target_) {
        if (FAILED(factory_->CreateDCRenderTarget(&props, target_.GetAddressOf()))) return false;
        target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    }
    RECT rc{0, 0, width_, height_};
    if (FAILED(target_->BindDC(dc_, &rc))) {
        target_.Reset();
        if (FAILED(factory_->CreateDCRenderTarget(&props, target_.GetAddressOf()))) return false;
        target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        if (FAILED(target_->BindDC(dc_, &rc))) return false;
    }
    target_->SetDpi(dpi_, dpi_);
    return true;
}

bool Renderer::Begin() {
    if (!EnsureTarget()) return false;
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    target_->Clear(D2D1::ColorF(0, 0, 0, 0));
    return true;
}

bool Renderer::End() {
    HRESULT hr = target_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DropDeviceResources();
        return false;
    }
    return SUCCEEDED(hr);
}

void Renderer::Present(HWND hwnd, POINT screenPos, const RECT* sourcePx) {
    if (!dc_) return;
    POINT src{0, 0};
    SIZE size{width_, height_};
    if (sourcePx) {
        src = {sourcePx->left, sourcePx->top};
        size = {std::max(1L, sourcePx->right - sourcePx->left), std::max(1L, sourcePx->bottom - sourcePx->top)};
    }
    HDC screen = GetDC(nullptr);
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd, screen, &screenPos, &size, dc_, &src, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
}

ID2D1SolidColorBrush* Renderer::Brush(COLORREF color, float alpha) {
    if (!brush_) target_->CreateSolidColorBrush(ToColor(color, alpha), brush_.GetAddressOf());
    else brush_->SetColor(ToColor(color, alpha));
    return brush_.Get();
}

void Renderer::FillRound(const D2D1_RECT_F& r, float radius, COLORREF color, float alpha) {
    if (alpha <= 0.0f) return;
    target_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), Brush(color, alpha));
}

void Renderer::StrokeRound(const D2D1_RECT_F& r, float radius, COLORREF color, float alpha, float width) {
    if (alpha <= 0.0f) return;
    float h = width / 2;
    D2D1_RECT_F inner{r.left + h, r.top + h, r.right - h, r.bottom - h};
    target_->DrawRoundedRectangle(D2D1::RoundedRect(inner, radius - h, radius - h), Brush(color, alpha), width);
}

void Renderer::Line(D2D1_POINT_2F a, D2D1_POINT_2F b, COLORREF color, float alpha, float width) {
    if (alpha <= 0.0f) return;
    target_->DrawLine(a, b, Brush(color, alpha), width, roundCaps_.Get());
}

void Renderer::FillTriangle(D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_POINT_2F c, COLORREF color, float alpha) {
    if (alpha <= 0.0f) return;
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory_->CreatePathGeometry(path.GetAddressOf())) || FAILED(path->Open(sink.GetAddressOf()))) return;
    sink->BeginFigure(a, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(b);
    sink->AddLine(c);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (SUCCEEDED(sink->Close())) target_->FillGeometry(path.Get(), Brush(color, alpha));
}

void Renderer::DrawBitmap(ID2D1Bitmap* bitmap, const D2D1_RECT_F& r, float alpha) {
    if (bitmap && alpha > 0.0f) target_->DrawBitmap(bitmap, r, alpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

ComPtr<ID2D1PathGeometry> Renderer::NotchPath(const D2D1_RECT_F& b, float radius, float ear) {
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory_->CreatePathGeometry(path.GetAddressOf())) || FAILED(path->Open(sink.GetAddressOf()))) {
        return nullptr;
    }
    const float h = b.bottom - b.top;
    radius = std::clamp(radius, 0.0f, std::min(h, b.right - b.left) / 2);
    ear = std::clamp(ear, 0.0f, h - radius);
    auto arc = [&](float x, float y, float r, D2D1_SWEEP_DIRECTION dir) {
        if (r <= 0.0f) sink->AddLine({x, y});
        else sink->AddArc(D2D1::ArcSegment({x, y}, {r, r}, 0.0f, dir, D2D1_ARC_SIZE_SMALL));
    };
    sink->BeginFigure({b.left - ear, b.top}, D2D1_FIGURE_BEGIN_FILLED);
    arc(b.left, b.top + ear, ear, D2D1_SWEEP_DIRECTION_CLOCKWISE);
    sink->AddLine({b.left, b.bottom - radius});
    arc(b.left + radius, b.bottom, radius, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE);
    sink->AddLine({b.right - radius, b.bottom});
    arc(b.right, b.bottom - radius, radius, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE);
    sink->AddLine({b.right, b.top + ear});
    arc(b.right + ear, b.top, ear, D2D1_SWEEP_DIRECTION_CLOCKWISE);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) return nullptr;
    return path;
}

IDWriteTextFormat* Renderer::Format(const TextStyle& style) {
    auto key = std::make_tuple((int)std::lround(style.size * 10), (int)style.weight, (int)style.align);
    auto it = formats_.find(key);
    if (it != formats_.end()) return it->second.Get();
    ComPtr<IDWriteTextFormat> format;
    if (FAILED(dwrite_->CreateTextFormat(family_.c_str(), nullptr, style.weight, DWRITE_FONT_STYLE_NORMAL,
                                         DWRITE_FONT_STRETCH_NORMAL, style.size, L"ja-jp",
                                         format.GetAddressOf()))) {
        return nullptr;
    }
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    format->SetTextAlignment(style.align == Align::Center  ? DWRITE_TEXT_ALIGNMENT_CENTER
                             : style.align == Align::Right ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                           : DWRITE_TEXT_ALIGNMENT_LEADING);
    if (!ellipsis_) dwrite_->CreateEllipsisTrimmingSign(format.Get(), ellipsis_.GetAddressOf());
    DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    format->SetTrimming(&trimming, ellipsis_.Get());
    formats_[key] = format;
    return format.Get();
}

float Renderer::MeasureText(const std::wstring& text, const TextStyle& style) {
    IDWriteTextFormat* format = Format(style);
    if (!format || text.empty()) return 0.0f;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(text.c_str(), (UINT32)text.size(), format, 10000.0f, 1000.0f,
                                         layout.GetAddressOf()))) {
        return 0.0f;
    }
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    return std::ceil(m.widthIncludingTrailingWhitespace);
}

void Renderer::DrawLabel(const std::wstring& text, const D2D1_RECT_F& r, const TextStyle& style, COLORREF color,
                         float alpha) {
    IDWriteTextFormat* format = Format(style);
    if (!format || text.empty() || alpha <= 0.0f || r.right <= r.left) return;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(text.c_str(), (UINT32)text.size(), format, r.right - r.left,
                                         r.bottom - r.top, layout.GetAddressOf()))) {
        return;
    }
    target_->DrawTextLayout(D2D1::Point2F(r.left, r.top), layout.Get(), Brush(color, alpha),
                            D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

ID2D1Bitmap* Renderer::WindowIcon(HWND hwnd, const std::wstring& exePath) {
    if (!hwnd || !wic_ || !target_) return nullptr;
    auto it = icons_.find(hwnd);
    if (it != icons_.end()) return it->second.Get();

    HICON icon = nullptr;
    auto ask = [&](WPARAM kind) {
        DWORD_PTR result = 0;
        if (!icon && SendMessageTimeoutW(hwnd, WM_GETICON, kind, 0, SMTO_ABORTIFHUNG, 50, &result) && result) {
            icon = reinterpret_cast<HICON>(result);
        }
    };
    ask(ICON_BIG);
    if (!icon) icon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICON));
    ask(ICON_SMALL2);
    if (!icon) icon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICONSM));
    HICON owned = nullptr;
    if (!icon && !exePath.empty()) {
        SHFILEINFOW info{};
        if (SHGetFileInfoW(exePath.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON)) {
            icon = owned = info.hIcon;
        }
    }

    ComPtr<ID2D1Bitmap> bitmap = BitmapFromIcon(icon);
    if (owned) DestroyIcon(owned);
    if (icons_.size() > 64) icons_.clear();
    icons_[hwnd] = bitmap;  // also remembers windows without an icon
    return bitmap.Get();
}

ID2D1Bitmap* Renderer::FileIcon(const std::wstring& path) {
    if (path.empty() || !wic_ || !target_) return nullptr;
    auto it = fileIcons_.find(path);
    if (it != fileIcons_.end()) return it->second.Get();
    ComPtr<ID2D1Bitmap> bitmap;
    SHFILEINFOW info{};
    if (SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON)) {
        bitmap = BitmapFromIcon(info.hIcon);
        DestroyIcon(info.hIcon);
    }
    if (fileIcons_.size() > 64) fileIcons_.clear();
    fileIcons_[path] = bitmap;
    return bitmap.Get();
}

ComPtr<ID2D1Bitmap> Renderer::BitmapFromIcon(HICON icon) {
    ComPtr<ID2D1Bitmap> bitmap;
    ComPtr<IWICBitmap> source;
    ComPtr<IWICFormatConverter> converter;
    if (icon && SUCCEEDED(wic_->CreateBitmapFromHICON(icon, source.GetAddressOf())) &&
        SUCCEEDED(wic_->CreateFormatConverter(converter.GetAddressOf())) &&
        SUCCEEDED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                        nullptr, 0.0, WICBitmapPaletteTypeMedianCut))) {
        target_->CreateBitmapFromWicBitmap(converter.Get(), nullptr, bitmap.GetAddressOf());
    }
    return bitmap;
}

}  // namespace op
