#pragma once

#include <string>
#include <vector>

struct ScreenElement {
    std::wstring kind;
    std::wstring name;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

struct ScreenReading {
    std::wstring window;
    std::wstring text;
    std::wstring error;
    int ocr_lines = 0;
    int controls = 0;
    std::vector<ScreenElement> elements;
};

void set_desktop_owner(void* hwnd);
ScreenReading recognize_screen(void* own_window);
ScreenReading last_screen();
bool locate_element(const std::vector<ScreenElement>& elements, const std::wstring& label, int& x, int& y);
std::wstring read_other_screen();
std::wstring strip_actions(const std::wstring& answer, std::wstring& script);
bool run_actions(const std::wstring& script, bool allow_control, std::wstring& report, std::wstring& screen);
