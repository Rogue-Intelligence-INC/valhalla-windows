#pragma once

#include <windows.h>

#include <string>

inline std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) return L"";
    int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) return L"";
    std::wstring out(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), count);
    return out;
}

inline std::string wide_to_utf8(const std::wstring& text) {
    if (text.empty()) return {};
    int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string out(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), count, nullptr, nullptr);
    return out;
}

inline std::wstring trim_copy(std::wstring text) {
    auto space = [](wchar_t ch) {
        return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
    };
    while (!text.empty() && space(text.front())) text.erase(text.begin());
    while (!text.empty() && space(text.back())) text.pop_back();
    return text;
}

inline std::string trim_utf8(std::string text) {
    auto space = [](char ch) {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
    };
    while (!text.empty() && space(text.front())) text.erase(text.begin());
    while (!text.empty() && space(text.back())) text.pop_back();
    return text;
}
