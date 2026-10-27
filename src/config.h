#pragma once

#include "common.h"

#include <string>
#include <vector>

namespace op {

constexpr int kAllMonitors = -1;  // a notch on every monitor, each with its own windows

struct Config {
    int monitor = kAllMonitors;  // or 0 = primary, otherwise 1-based from the left (same order as ObjectFrame)
    bool autoHide = true;      // tucked away unless hovered, announcing something, or an app wants attention
    bool alwaysSmall = false;  // keep a compact top notch that still expands on hover
    bool reserveSpace = true;  // keep maximized/tiled windows below the notch when it stays shown
    bool hideTaskbar = true;   // the pulse stands in for the Windows taskbar
    int scale = 100;           // percent
    bool expandOnHover = true;
    int hoverDelay = 150;  // ms
    bool notify = true;
    int notifyDuration = 1400;  // ms
    bool notifyFocus = false;
    bool clockSeconds = false;
    bool hideOnFullscreen = true;
    bool accentFromFrame = true;  // use ObjectFrame's border_active while it is running
    COLORREF accent = RGB(0x89, 0xb4, 0xfa);
    COLORREF background = RGB(0, 0, 0);
    int opacity = 100;  // percent
    // Rounded caps in the four corners of each monitor the notch is on, in the same black as the notch.
    bool corners = false;  // screen corners use the notch's own radius (kNotchRadius)
    // What the notch shows, by the names in kCompactItems / kExpandedItems. The compact view
    // draws its items in this order; the expanded view has fixed places for them.
    std::vector<std::wstring> compactItems;
    std::vector<std::wstring> expandedItems;

    Config();
};

struct ItemInfo {
    const wchar_t* name;
    const wchar_t* label;  // for the menu
    bool byDefault;
};
// In their default order.
extern const std::vector<ItemInfo> kCompactItems;
extern const std::vector<ItemInfo> kExpandedItems;

bool HasItem(const std::vector<std::wstring>& items, const std::wstring& name);
// Turns `name` on (in its default position) or off. Returns the new list.
std::vector<std::wstring> ToggleItem(const std::vector<std::wstring>& items, const std::vector<ItemInfo>& known,
                                     const std::wstring& name);
std::wstring JoinItems(const std::vector<std::wstring>& items);

std::wstring DefaultConfigPath();
bool EnsureConfigFile(const std::wstring& path);
// Reads `path`, keeping defaults for missing or invalid values (described in `warnings`).
Config LoadConfig(const std::wstring& path, std::vector<std::wstring>& warnings);
// Sets `key = value` in the file, replacing the line of an existing key (keeping its comment) or appending.
bool SaveConfigValue(const std::wstring& path, const std::wstring& key, const std::wstring& value);

}  // namespace op
