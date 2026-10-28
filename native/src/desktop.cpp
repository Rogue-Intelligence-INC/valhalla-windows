#include "desktop.h"
#include "ocr.h"

#include <UIAutomation.h>
#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

thread_local bool g_com = false;
HWND g_owner = nullptr;
ScreenReading g_last;
bool g_busy = false;

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

struct Reveal {
    HWND hwnd = nullptr;
    bool hidden = false;
    ~Reveal() {
        if (hidden && hwnd) {
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
        }
    }
};

const wchar_t* kind_of(CONTROLTYPEID type) {
    switch (type) {
        case UIA_ButtonControlTypeId: return L"button";
        case UIA_EditControlTypeId: return L"edit";
        case UIA_TextControlTypeId: return L"text";
        case UIA_DocumentControlTypeId: return L"document";
        case UIA_HyperlinkControlTypeId: return L"link";
        case UIA_ListItemControlTypeId: return L"item";
        case UIA_TabItemControlTypeId: return L"tab";
        case UIA_CheckBoxControlTypeId: return L"check";
        case UIA_ComboBoxControlTypeId: return L"combo";
        case UIA_MenuItemControlTypeId: return L"menu";
        case UIA_ImageControlTypeId: return L"image";
        case UIA_WindowControlTypeId: return L"window";
        default: return nullptr;
    }
}

void add_element(std::vector<ScreenElement>& elements, const wchar_t* kind, const std::wstring& name, RECT bounds) {
    if (!kind || elements.size() >= 120) return;
    int w = bounds.right - bounds.left;
    int h = bounds.bottom - bounds.top;
    if (w < 2 || h < 2 || w > 4000 || h > 3000) return;
    ScreenElement element;
    element.kind = kind;
    element.name = name.empty() ? kind : name;
    element.x = bounds.left;
    element.y = bounds.top;
    element.w = w;
    element.h = h;
    elements.push_back(std::move(element));
}

void walk(IUIAutomationTreeWalker* walker, IUIAutomationElement* element, std::vector<ScreenElement>& elements, int depth) {
    if (!element || depth > 14 || elements.size() >= 120) return;
    CONTROLTYPEID type = 0;
    element->get_CurrentControlType(&type);
    const wchar_t* kind = kind_of(type);
    RECT bounds{};
    element->get_CurrentBoundingRectangle(&bounds);
    BSTR name = nullptr;
    element->get_CurrentName(&name);
    std::wstring label = name ? name : L"";
    if (name) SysFreeString(name);
    if (kind && (!label.empty() || type == UIA_EditControlTypeId || type == UIA_DocumentControlTypeId)) {
        add_element(elements, kind, label, bounds);
    }
    IUIAutomationElement* child = nullptr;
    if (FAILED(walker->GetFirstChildElement(element, &child))) return;
    while (child && elements.size() < 120) {
        walk(walker, child, elements, depth + 1);
        IUIAutomationElement* next = nullptr;
        walker->GetNextSiblingElement(child, &next);
        child->Release();
        child = next;
    }
}

struct Capture {
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    int origin_x = 0;
    int origin_y = 0;
    int source_w = 0;
    int source_h = 0;
};

Capture capture_rect(int left, int top, int width, int height) {
    Capture shot;
    if (width < 8 || height < 8) return shot;
    int dest_w = width;
    int dest_h = height;
    const int cap = 1600;
    if (width > cap || height > cap) {
        double scale = static_cast<double>(cap) / static_cast<double>(std::max(width, height));
        dest_w = std::max(8, static_cast<int>(width * scale));
        dest_h = std::max(8, static_cast<int>(height * scale));
    }
    HDC screen = GetDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = dest_w;
    info.bmiHeader.biHeight = -dest_h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC memory = CreateCompatibleDC(screen);
    HGDIOBJ old = SelectObject(memory, dib);
    SetStretchBltMode(memory, HALFTONE);
    StretchBlt(memory, 0, 0, dest_w, dest_h, screen, left, top, width, height, SRCCOPY);
    SelectObject(memory, old);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    if (bits) {
        shot.pixels.assign(static_cast<const unsigned char*>(bits), static_cast<const unsigned char*>(bits) + static_cast<size_t>(dest_w) * dest_h * 4);
        shot.width = dest_w;
        shot.height = dest_h;
        shot.origin_x = left;
        shot.origin_y = top;
        shot.source_w = width;
        shot.source_h = height;
    }
    if (dib) DeleteObject(dib);
    return shot;
}

HWND other_window(HWND own) {
    HWND foreground = GetForegroundWindow();
    if (foreground && foreground != own && IsWindowVisible(foreground)) return foreground;
    HWND best = nullptr;
    struct Pick {
        HWND own;
        HWND best;
    } pick{own, nullptr};
    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            auto* state = reinterpret_cast<Pick*>(param);
            if (!IsWindowVisible(hwnd) || IsIconic(hwnd) || hwnd == state->own) return TRUE;
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == GetCurrentProcessId()) return TRUE;
            wchar_t title[4] = {};
            if (GetWindowTextW(hwnd, title, 4) == 0) return TRUE;
            state->best = hwnd;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&pick));
    best = pick.best;
    return best ? best : GetDesktopWindow();
}

std::wstring quote_name(const std::wstring& name) {
    std::wstring out;
    for (wchar_t ch : name) {
        if (ch == L'\n' || ch == L'\r' || ch == L'\t') out.push_back(L' ');
        else out.push_back(ch);
        if (out.size() > 80) break;
    }
    return out;
}

void append_line(std::wstring& text, const std::wstring& line) {
    if (text.size() > 7000) return;
    text += line;
    text += L"\n";
}

std::wstring fold_copy(std::wstring text) {
    for (wchar_t& ch : text) ch = static_cast<wchar_t>(towlower(ch));
    return text;
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

void map_box(ScreenElement& element, const Capture& shot) {
    if (shot.source_w <= 0 || shot.source_h <= 0 || shot.width <= 0 || shot.height <= 0) return;
    element.x = shot.origin_x + element.x * shot.source_w / shot.width;
    element.y = shot.origin_y + element.y * shot.source_h / shot.height;
    element.w = std::max(1, element.w * shot.source_w / shot.width);
    element.h = std::max(1, element.h * shot.source_h / shot.height);
}

}  // namespace

void set_desktop_owner(void* hwnd) {
    g_owner = reinterpret_cast<HWND>(hwnd);
}

bool locate_element(const std::vector<ScreenElement>& elements, const std::wstring& label, int& x, int& y) {
    std::wstring key = fold_copy(trim_line(label));
    if (key.empty()) return false;
    const ScreenElement* best = nullptr;
    size_t best_size = 0;
    for (const auto& element : elements) {
        if (element.w <= 0 || element.h <= 0) continue;
        std::wstring name = fold_copy(element.name);
        if (name.find(key) == std::wstring::npos) continue;
        if (!best || name.size() < best_size) {
            best = &element;
            best_size = name.size();
        }
    }
    if (!best) return false;
    x = best->x + best->w / 2;
    y = best->y + best->h / 2;
    return true;
}

ScreenReading last_screen() {
    return g_last;
}

ScreenReading recognize_screen(void* own_window) {
    ScreenReading reading;
    if (g_busy) {
        reading.error = L"busy";
        return g_last;
    }
    g_busy = true;
    HWND own = reinterpret_cast<HWND>(own_window);
    Reveal reveal;
    if (own && IsWindow(own) && IsWindowVisible(own)) {
        reveal.hwnd = own;
        reveal.hidden = true;
        ShowWindow(own, SW_HIDE);
        Sleep(180);
    }
    HWND target = other_window(own);
    wchar_t title[512] = {};
    GetWindowTextW(target, title, 512);
    reading.window = title[0] ? title : L"Desktop";
    RECT rect{};
    GetWindowRect(target, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    int left = rect.left;
    int top = rect.top;
    if (width < 40 || height < 40 || left < -10000) {
        left = 0;
        top = 0;
        width = GetSystemMetrics(SM_CXSCREEN);
        height = GetSystemMetrics(SM_CYSCREEN);
        reading.window = L"Desktop";
    }
    ensure_com();
    IUIAutomation* automation = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))) && automation) {
        ComRelease automation_release{automation};
        IUIAutomationElement* root = nullptr;
        if (SUCCEEDED(automation->ElementFromHandle(target, &root)) && root) {
            ComRelease root_release{root};
            IUIAutomationTreeWalker* walker = nullptr;
            if (SUCCEEDED(automation->get_ControlViewWalker(&walker)) && walker) {
                ComRelease walker_release{walker};
                walk(walker, root, reading.elements, 0);
            }
        }
    }
    Capture shot = capture_rect(left, top, width, height);
    reveal.hidden = false;
    if (reveal.hwnd) {
        ShowWindow(reveal.hwnd, SW_SHOW);
        SetForegroundWindow(reveal.hwnd);
        reveal.hwnd = nullptr;
    }
    reading.controls = static_cast<int>(reading.elements.size());
    if (!shot.pixels.empty()) {
        OcrPass ocr = recognize_bgra(shot.pixels.data(), shot.width, shot.height, shot.width * 4);
        if (!ocr.ok) reading.error = ocr.error;
        for (auto& line : ocr.lines) {
            ScreenElement element;
            element.kind = L"ocr";
            element.name = line.text;
            element.x = line.x;
            element.y = line.y;
            element.w = line.w;
            element.h = line.h;
            map_box(element, shot);
            reading.elements.push_back(element);
            ++reading.ocr_lines;
        }
        for (auto& word : ocr.words) {
            if (reading.elements.size() >= 240) break;
            ScreenElement element;
            element.kind = L"ocr";
            element.name = word.text;
            element.x = word.x;
            element.y = word.y;
            element.w = word.w;
            element.h = word.h;
            map_box(element, shot);
            reading.elements.push_back(std::move(element));
        }
    } else {
        reading.error = L"screen capture failed";
    }
    append_line(reading.text, L"Window: " + reading.window);
    append_line(reading.text, L"Controls:");
    int listed = 0;
    for (const auto& element : reading.elements) {
        if (element.kind == L"ocr") continue;
        wchar_t line[256];
        swprintf_s(line, L"%s \"%s\" @ %d,%d %dx%d", element.kind.c_str(), quote_name(element.name).c_str(), element.x, element.y, element.w, element.h);
        append_line(reading.text, line);
        if (++listed >= 80) break;
    }
    append_line(reading.text, L"OCR:");
    int ocr_listed = 0;
    for (const auto& element : reading.elements) {
        if (element.kind != L"ocr") continue;
        wchar_t line[256];
        swprintf_s(line, L"\"%s\" @ %d,%d %dx%d", quote_name(element.name).c_str(), element.x, element.y, element.w, element.h);
        append_line(reading.text, line);
        if (++ocr_listed >= 80) break;
    }
    if (!reading.error.empty()) append_line(reading.text, L"OCR note: " + reading.error);
    g_last = reading;
    g_busy = false;
    return reading;
}

std::wstring read_other_screen() {
    return recognize_screen(g_owner).text;
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
            ScreenReading reading = recognize_screen(g_owner);
            screen = reading.text;
            report += L"识别屏幕\n";
            any = true;
            continue;
        }
        if (line.rfind(L"click_text ", 0) == 0) {
            int x = 0;
            int y = 0;
            if (locate_element(g_last.elements, line.substr(11), x, y) && on_screen(x, y)) {
                click_at(x, y);
                report += line + L"\n";
                any = true;
            }
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
