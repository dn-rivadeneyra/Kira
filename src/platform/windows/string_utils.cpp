#include "src/platform/windows/string_utils.hpp"

namespace kira {

std::wstring string_to_wstring(std::string_view str) {
    if (str.empty()) return L"";
    int size_needed = MultiByteToWideChar(
        CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0
    );
    if (size_needed <= 0) return L"";
    std::wstring wstr(static_cast<std::size_t>(size_needed), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, str.data(), static_cast<int>(str.size()), &wstr[0], size_needed
    );
    return wstr;
}

std::string wstring_to_string(std::wstring_view wstr) {
    if (wstr.empty()) return "";
    int size_needed = WideCharToMultiByte(
        CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr
    );
    if (size_needed <= 0) return "";
    std::string str(static_cast<std::size_t>(size_needed), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), &str[0], size_needed, nullptr, nullptr
    );
    return str;
}

} // namespace kira
