#pragma once

#include <string>

std::wstring read_other_screen();
std::wstring strip_actions(const std::wstring& answer, std::wstring& script);
bool run_actions(const std::wstring& script, bool allow_control, std::wstring& report, std::wstring& screen);
