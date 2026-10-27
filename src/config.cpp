#include "config.h"

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <filesystem>

namespace op {

namespace {

const wchar_t kDefaultConfig[] = LR"CFG(# ObjectPulse 設定ファイル
# 書式: key = value     '#' 以降はコメント
# 編集後はノッチを右クリック →「ObjectPulse の設定を再読み込み」で反映されます。

# ---- 位置 ----
monitor = all             # all = すべてのモニターに出し、それぞれのモニターで開いているアプリを表示
                          # primary | 1, 2, ... = 1 台だけに出す (番号は ObjectFrame と同じく左から)
auto_hide = true          # 何もないときは隠れ、画面上端の中央にマウスを当てると開く
always_small = false      # 常に小さな上部ノッチを出す。ウィンドウはノッチの大きさで、ホバーすると展開する
                          # 左右の明るさ・音量・マイクは、この設定に関係なく、端にホバーしたときだけ出る
reserve_space = true      # ノッチを出しっぱなしのとき (always_small、または auto_hide = false)、上部に領域を確保する
scale = 100               # 大きさ (%, 50-200)

# ---- タスクバー ----
hide_taskbar = true       # Windows のタスクバーを隠し、ノッチから起動中のアプリ・通知などを操作する
                          # 隠したままになったときは ObjectPulse.exe restore-taskbar で戻せます

# ---- 動作 ----
expand_on_hover = true    # マウスを乗せると広がって詳細とボタンを表示 (false ならクリックで開く。auto_hide 中は常に有効)
hover_delay = 150         # 広がるまでの時間 (ms)
notify = true             # ワークスペースやレイアウトが変わったときに一時的に広げて知らせる
notify_duration = 1400    # 知らせを表示する時間 (ms)
notify_focus = false      # フォーカスしたウィンドウが変わったときも知らせる
clock_seconds = false     # 時計に秒を表示
hide_on_fullscreen = true # 全画面のアプリや ObjectFrame の全画面表示中は隠す

# ---- 表示内容 ----
# none で何も表示しません。編集したら右クリック →「設定を再読み込み」。
# 通常時のノッチ (書いた順に並びます): battery, attention (注意を引いているアプリ), paused (一時停止),
#                                      workspaces, clock, date
compact_items = battery, attention, paused, workspaces, clock
# 展開時: pins (ピン留めしたアプリ), tasks (その他の起動中のアプリ), battery, clock (時計と日付),
#         workspaces, layouts, pause (一時停止ボタン)
expanded_items = pins, tasks, battery, clock, workspaces, layouts, pause
# ピン留めはアプリのアイコンを右クリック →「ピン留めする」。同じフォルダーの pins.txt に保存されます。

# ---- 見た目 ----
accent = auto             # auto = ObjectFrame の border_active の色 / 0xRRGGBB
background = 0x000000     # ノッチと四隅の色
opacity = 100             # 背景の不透明度 (%)
corners = false           # ノッチを出しているモニターの四隅を、ノッチと同じ半径の角丸で覆う
)CFG";

bool ParseBool(const std::wstring& v, bool& out) {
    auto l = ToLower(v);
    if (l == L"true" || l == L"yes" || l == L"on" || l == L"1") out = true;
    else if (l == L"false" || l == L"no" || l == L"off" || l == L"0") out = false;
    else return false;
    return true;
}

bool ParseColor(const std::wstring& v, COLORREF& out) {
    std::wstring s = v;
    if (s.rfind(L"0x", 0) == 0 || s.rfind(L"0X", 0) == 0) s = s.substr(2);
    else if (!s.empty() && s[0] == L'#') s = s.substr(1);
    if (s.size() != 6 || s.find_first_not_of(L"0123456789abcdefABCDEF") != std::wstring::npos) return false;
    unsigned long rgb = std::wcstoul(s.c_str(), nullptr, 16);
    out = RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    return true;
}

bool ParseInt(const std::wstring& v, int lo, int hi, int& out) {
    if (v.empty() || v.find_first_not_of(L"-0123456789") != std::wstring::npos) return false;
    out = std::clamp(_wtoi(v.c_str()), lo, hi);
    return true;
}

bool ParseItems(const std::wstring& v, const std::vector<ItemInfo>& known, std::vector<std::wstring>& out) {
    std::vector<std::wstring> items;
    bool ok = true;
    if (ToLower(v) != L"none") {
        for (const std::wstring& part : Split(v, L',')) {
            std::wstring name = ToLower(Trim(part));
            if (name.empty() || HasItem(items, name)) continue;
            if (std::none_of(known.begin(), known.end(), [&](const ItemInfo& i) { return name == i.name; })) {
                ok = false;
                continue;
            }
            items.push_back(std::move(name));
        }
    }
    out = std::move(items);
    return ok;
}

std::vector<std::wstring> Defaults(const std::vector<ItemInfo>& known) {
    std::vector<std::wstring> list;
    for (const ItemInfo& i : known) {
        if (i.byDefault) list.push_back(i.name);
    }
    return list;
}

std::wstring LineKey(const std::wstring& line) {
    std::wstring s = line;
    if (size_t hash = s.find(L'#'); hash != std::wstring::npos) s.resize(hash);
    size_t eq = s.find(L'=');
    return eq == std::wstring::npos ? std::wstring() : ToLower(Trim(s.substr(0, eq)));
}

}  // namespace

const std::vector<ItemInfo> kCompactItems = {
    {L"battery", L"バッテリー", true},       {L"attention", L"注意を引いているアプリ", true},
    {L"paused", L"一時停止中の表示", true},  {L"workspaces", L"ワークスペース", true},
    {L"clock", L"時計", true},               {L"date", L"日付", false},
};

const std::vector<ItemInfo> kExpandedItems = {
    {L"pins", L"ピン留めしたアプリ", true},  {L"tasks", L"その他の起動中のアプリ", true},
    {L"quick", L"クイック設定", false},      {L"notifications", L"通知", false},
    {L"battery", L"バッテリー", true},       {L"clock", L"時計と日付", true},
    {L"workspaces", L"ワークスペース", true}, {L"layouts", L"レイアウト", true},
    {L"pause", L"一時停止ボタン", true},
};

Config::Config() : compactItems(Defaults(kCompactItems)), expandedItems(Defaults(kExpandedItems)) {}

bool HasItem(const std::vector<std::wstring>& items, const std::wstring& name) {
    return std::find(items.begin(), items.end(), name) != items.end();
}

std::vector<std::wstring> ToggleItem(const std::vector<std::wstring>& items, const std::vector<ItemInfo>& known,
                                     const std::wstring& name) {
    std::vector<std::wstring> next = items;
    if (auto it = std::find(next.begin(), next.end(), name); it != next.end()) {
        next.erase(it);
        return next;
    }
    // Before the first enabled item that comes after it in the default order.
    auto rank = [&](const std::wstring& n) {
        for (size_t i = 0; i < known.size(); ++i) {
            if (n == known[i].name) return i;
        }
        return known.size();
    };
    auto pos = std::find_if(next.begin(), next.end(), [&](const std::wstring& n) { return rank(n) > rank(name); });
    next.insert(pos, name);
    return next;
}

std::wstring JoinItems(const std::vector<std::wstring>& items) {
    std::wstring out;
    for (const std::wstring& i : items) out += (out.empty() ? L"" : L", ") + i;
    return out.empty() ? L"none" : out;
}

std::wstring DefaultConfigPath() {
    wchar_t home[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
    std::wstring base = (n > 0 && n < MAX_PATH) ? home : L".";
    return base + L"\\.config\\objectpulse\\pulse.conf";
}

bool EnsureConfigFile(const std::wstring& path) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) return true;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"w, ccs=UTF-8") != 0 || !f) return false;
    fputws(kDefaultConfig, f);
    fclose(f);
    return true;
}

Config LoadConfig(const std::wstring& path, std::vector<std::wstring>& warnings) {
    Config cfg;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r, ccs=UTF-8") != 0 || !f) return cfg;
    wchar_t buf[1024];
    int lineNo = 0;
    while (fgetws(buf, 1024, f)) {
        ++lineNo;
        std::wstring line = buf;
        if (size_t hash = line.find(L'#'); hash != std::wstring::npos) line.resize(hash);
        line = Trim(line);
        if (line.empty()) continue;
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) {
            warnings.push_back(L"line " + std::to_wstring(lineNo) + L": expected key = value");
            continue;
        }
        std::wstring k = ToLower(Trim(line.substr(0, eq)));
        std::wstring v = Trim(line.substr(eq + 1));
        bool ok = true;
        if (k == L"monitor") {
            if (ToLower(v) == L"all") cfg.monitor = kAllMonitors;
            else if (ToLower(v) == L"primary") cfg.monitor = 0;
            else ok = ParseInt(v, 1, 16, cfg.monitor);
        } else if (k == L"auto_hide") ok = ParseBool(v, cfg.autoHide);
        else if (k == L"always_small") ok = ParseBool(v, cfg.alwaysSmall);
        else if (k == L"reserve_space") ok = ParseBool(v, cfg.reserveSpace);
        else if (k == L"hide_taskbar") ok = ParseBool(v, cfg.hideTaskbar);
        else if (k == L"scale") ok = ParseInt(v, 50, 200, cfg.scale);
        else if (k == L"expand_on_hover") ok = ParseBool(v, cfg.expandOnHover);
        else if (k == L"hover_delay") ok = ParseInt(v, 0, 2000, cfg.hoverDelay);
        else if (k == L"notify") ok = ParseBool(v, cfg.notify);
        else if (k == L"notify_duration") ok = ParseInt(v, 300, 10000, cfg.notifyDuration);
        else if (k == L"notify_focus") ok = ParseBool(v, cfg.notifyFocus);
        else if (k == L"clock_seconds") ok = ParseBool(v, cfg.clockSeconds);
        else if (k == L"hide_on_fullscreen") ok = ParseBool(v, cfg.hideOnFullscreen);
        else if (k == L"accent") {
            cfg.accentFromFrame = ToLower(v) == L"auto";
            if (!cfg.accentFromFrame) ok = ParseColor(v, cfg.accent);
        } else if (k == L"background") ok = ParseColor(v, cfg.background);
        else if (k == L"opacity") ok = ParseInt(v, 20, 100, cfg.opacity);
        else if (k == L"corners") ok = ParseBool(v, cfg.corners);
        else if (k == L"corner_radius") ok = true;  // older files; the radius now follows the notch
        else if (k == L"compact_items") ok = ParseItems(v, kCompactItems, cfg.compactItems);
        else if (k == L"expanded_items") ok = ParseItems(v, kExpandedItems, cfg.expandedItems);
        else {
            warnings.push_back(L"line " + std::to_wstring(lineNo) + L": unknown key '" + k + L"'");
            continue;
        }
        if (!ok) warnings.push_back(L"line " + std::to_wstring(lineNo) + L": invalid value for " + k);
    }
    fclose(f);
    return cfg;
}

bool SaveConfigValue(const std::wstring& path, const std::wstring& key, const std::wstring& value) {
    std::vector<std::wstring> lines;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r, ccs=UTF-8") == 0 && f) {
        wchar_t buf[1024];
        while (fgetws(buf, 1024, f)) {
            std::wstring line = buf;
            while (!line.empty() && (line.back() == L'\n' || line.back() == L'\r')) line.pop_back();
            lines.push_back(std::move(line));
        }
        fclose(f);
    }
    const std::wstring entry = key + L" = " + value;
    bool replaced = false;
    for (std::wstring& line : lines) {
        if (replaced || LineKey(line) != key) continue;
        std::wstring next = entry;
        if (size_t hash = line.find(L'#'); hash != std::wstring::npos) {
            // Keep the comment in its column when the new value leaves room for it.
            next += std::wstring(hash > next.size() ? hash - next.size() : 1, L' ') + line.substr(hash);
        }
        line = std::move(next);
        replaced = true;
    }
    if (!replaced) lines.push_back(entry);

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    if (_wfopen_s(&f, path.c_str(), L"w, ccs=UTF-8") != 0 || !f) return false;
    for (const std::wstring& line : lines) {
        fputws(line.c_str(), f);
        fputws(L"\n", f);
    }
    fclose(f);
    return true;
}

}  // namespace op
