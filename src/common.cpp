#include "common.h"

#include <cwctype>

namespace op {

std::wstring Trim(const std::wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::iswspace(s[b])) ++b;
    while (e > b && std::iswspace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::wstring ToLower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)std::towlower(c);
    return s;
}

std::vector<std::wstring> Split(const std::wstring& s, wchar_t separator) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find(separator, start);
        if (end == std::wstring::npos) end = s.size();
        parts.push_back(s.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

}  // namespace op
