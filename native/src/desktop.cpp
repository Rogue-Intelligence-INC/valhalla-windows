#include "desktop.h"

#include <UIAutomation.h>
#include <windows.h>

#include <string>
#include <vector>

namespace {

thread_local bool g_com = false;

void ensure_com() {
    if (g_com) return;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g_com = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
}

struct ComRelease {
    IUnknown* pointer = nullptr;
    ~ComRelease() {
        if (pointer) pointer->Release();
    }
};

void append_limited(std::wstring& out, const std::wstring& line) {
    if (line.empty() || out.size() > 5000) return;
    if (out.find(line) != std::wstring::npos) return;
    out += line;
    out += L"\n";
}

void walk(IUIAutomationTreeWalker* walker, IUIAutomationElement* element, std::wstring& out, int depth) {
    if (!element || depth > 8 || out.size() > 5000) return;
    VARIANT name;
    VariantInit(&name);
    if (SUCCEEDED(element->GetCurrentPropertyValue(UIA_NamePropertyId, &name)) && name.vt == VT_BSTR && name.bstrVal &&
        name.bstrVal[0]) {
        append_limited(out, name.bstrVal);
    }
    VariantClear(&name);
    IUIAutomationElement* child = nullptr;
    if (FAILED(walker->GetFirstChildElement(element, &child))) return;
    while (child) {
        walk(walker, child, out, depth + 1);
        IUIAutomationElement* next = nullptr;
        walker->GetNextSiblingElement(child, &next);
        child->Release();
        child = next;
    }
}

HWND target_window() {
    HWND foreground = GetForegroundWindow();
    DWORD foreground_pid = 0;
    GetWindowThreadProcessId(foreground, &foreground_pid);
    if (foreground_pid != GetCurrentProcessId()) return foreground;
    HWND best = nullptr;
    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == GetCurrentProcessId()) return TRUE;
            wchar_t title[8] = {};
            if (GetWindowTextW(hwnd, title, 8) == 0) return TRUE;
            *reinterpret_cast<HWND*>(param) = hwnd;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&best));
    return best ? best : foreground;
}

bool on_screen(int x, int y) {
    int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return x >= left && y >= top && x < left + width && y < top + height;
}

void click_at(int x, int y) {
    SetCursorPos(x, y);
    INPUT inputs[2]{};
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, inputs, sizeof(INPUT));
}

void type_text(const std::wstring& text) {
    std::vector<INPUT> inputs;
    inputs.reserve(text.size() * 2);
    for (wchar_t ch : text) {
        if (inputs.size() > 800) break;
        INPUT down{};
        down.type = INPUT_KEYBOARD;
        down.ki.wScan = ch;
        down.ki.dwFlags = KEYEVENTF_UNICODE;
        INPUT up = down;
        up.ki.dwFlags |= KEYEVENTF_KEYUP;
        inputs.push_back(down);
        inputs.push_back(up);
    }
    if (!inputs.empty()) SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

void tap_vk(WORD key) {
    INPUT inputs[2]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = key;
    inputs[1] = inputs[0];
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

bool named_key(const std::wstring& name, WORD& key) {
    if (name == L"enter") key = VK_RETURN;
    else if (name == L"tab") key = VK_TAB;
    else if (name == L"esc" || name == L"escape") key = VK_ESCAPE;
    else if (name == L"backspace") key = VK_BACK;
    else if (name == L"delete") key = VK_DELETE;
    else if (name == L"up") key = VK_UP;
    else if (name == L"down") key = VK_DOWN;
    else if (name == L"left") key = VK_LEFT;
    else if (name == L"right") key = VK_RIGHT;
    else if (name == L"home") key = VK_HOME;
    else if (name == L"end") key = VK_END;
    else if (name == L"space") key = VK_SPACE;
    else return false;
    return true;
}

std::wstring trim_line(std::wstring text) {
    while (!text.empty() && (text.front() == L' ' || text.front() == L'\t' || text.front() == L'\r')) text.erase(text.begin());
    while (!text.empty() && (text.back() == L' ' || text.back() == L'\t' || text.back() == L'\r')) text.pop_back();
    return text;
}

}  // namespace

std::wstring read_other_screen() {
    ensure_com();
    HWND hwnd = target_window();
    wchar_t title[512] = {};
    GetWindowTextW(hwnd, title, 512);
    std::wstring out = L"Window: ";
    out += title;
    out += L"\n";
    IUIAutomation* automation = nullptr;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))) || !automation) {
        return out;
    }
    ComRelease automation_release{automation};
    IUIAutomationElement* root = nullptr;
    if (FAILED(automation->ElementFromHandle(hwnd, &root)) || !root) return out;
    ComRelease root_release{root};
    IUIAutomationTreeWalker* walker = nullptr;
    if (FAILED(automation->get_ControlViewWalker(&walker)) || !walker) return out;
    ComRelease walker_release{walker};
    walk(walker, root, out, 0);
    return out;
}

std::wstring strip_actions(const std::wstring& answer, std::wstring& script) {
    script.clear();
    auto marker = answer.find(L"[[action]]");
    if (marker == std::wstring::npos) return answer;
    script = answer.substr(marker + 10);
    std::wstring visible = answer.substr(0, marker);
    while (!visible.empty() && (visible.back() == L'\n' || visible.back() == L'\r' || visible.back() == L' ')) visible.pop_back();
    return visible;
}

bool run_actions(const std::wstring& script, bool allow_control, std::wstring& report, std::wstring& screen) {
    report.clear();
    if (!allow_control) return false;
    bool any = false;
    size_t cursor = 0;
    while (cursor < script.size()) {
        auto end = script.find(L'\n', cursor);
        std::wstring line = trim_line(script.substr(cursor, end == std::wstring::npos ? std::wstring::npos : end - cursor));
        cursor = end == std::wstring::npos ? script.size() : end + 1;
        if (line.empty() || line == L"[[action]]") continue;
        if (line == L"read_screen") {
            screen = read_other_screen();
            report += L"读取屏幕\n";
            any = true;
            continue;
        }
        if (line.rfind(L"click ", 0) == 0 || line.rfind(L"move ", 0) == 0) {
            int x = 0;
            int y = 0;
            const wchar_t* numbers = line.c_str() + (line[0] == L'c' ? 6 : 5);
            if (swscanf_s(numbers, L"%d %d", &x, &y) == 2 && on_screen(x, y)) {
                if (line[0] == L'c') click_at(x, y);
                else SetCursorPos(x, y);
                report += line + L"\n";
                any = true;
            }
            continue;
        }
        if (line.rfind(L"type ", 0) == 0) {
            type_text(line.substr(5));
            report += L"输入文本\n";
            any = true;
            continue;
        }
        if (line.rfind(L"key ", 0) == 0) {
            WORD key = 0;
            if (named_key(trim_line(line.substr(4)), key)) {
                tap_vk(key);
                report += line + L"\n";
                any = true;
            }
            continue;
        }
        if (line.rfind(L"scroll ", 0) == 0) {
            int amount = 0;
            if (swscanf_s(line.c_str() + 7, L"%d", &amount) == 1) {
                amount = amount > 5 ? 5 : amount < -5 ? -5 : amount;
                INPUT input{};
                input.type = INPUT_MOUSE;
                input.mi.dwFlags = MOUSEEVENTF_WHEEL;
                input.mi.mouseData = static_cast<DWORD>(amount * WHEEL_DELTA);
                SendInput(1, &input, sizeof(INPUT));
                report += line + L"\n";
                any = true;
            }
        }
    }
    return any;
}
