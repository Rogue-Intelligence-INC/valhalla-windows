#include "desktop.h"
#include "ocr.h"
#include "sessions.h"
#include "utf.h"

#include <windows.h>

#include <iostream>
#include <string>

namespace {

int g_failed = 0;

void expect(bool ok, const char* name) {
    if (ok) {
        std::cout << "ok  " << name << "\n";
        return;
    }
    std::cout << "FAIL " << name << "\n";
    ++g_failed;
}

std::wstring scratch() {
    wchar_t buffer[MAX_PATH];
    GetTempPathW(MAX_PATH, buffer);
    std::wstring dir = std::wstring(buffer) + L"valhalla-agent-" + std::to_wstring(GetTickCount64());
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

void write_file(const std::wstring& path, const std::string& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD wrote = 0;
    if (file != INVALID_HANDLE_VALUE) {
        WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &wrote, nullptr);
        CloseHandle(file);
    }
}

bool file_exists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

}  // namespace

int main() {
    ConversationLibrary library;
    library.root = scratch();
    expect(!load_library(library), "new library has no index yet");
    expect(library.items.size() == 1 && !library.active.empty(), "an empty conversation is created");
    std::string first = library.active;
    std::string second = create_conversation(library);
    expect(second != first && library.active == second && library.items.size() == 2, "new conversation becomes current");
    expect(select_conversation(library, first) && library.active == first, "switch conversation");
    note_conversation(library, first, "steel slab");
    expect(library.items.front().id == first && library.items.front().title == "steel slab", "renamed conversation moves to the top");
    std::wstring first_file = conversation_file(library, first);
    expect(delete_conversation(library, second) && !file_exists(conversation_file(library, second)), "delete removes the file");
    expect(library.active == first && file_exists(first_file), "the other conversation stays");

    ConversationLibrary imported;
    imported.root = scratch();
    load_library(imported);
    std::wstring legacy = imported.root + L"\\legacy.json";
    write_file(legacy, "{\"lines\":[{\"user\":true,\"text\":\"Export coil 1500\",\"path\":\"\"}],\"history\":[]}");
    expect(import_legacy(imported, legacy), "import an older transcript");
    expect(imported.items.front().title.find("coil") != std::string::npos, "imported title comes from the first question");

    std::vector<ScreenElement> elements = {
        {L"button", L"Save", 10, 20, 40, 16},
        {L"ocr", L"Save the whole document", 0, 0, 220, 40},
    };
    int x = 0;
    int y = 0;
    expect(locate_element(elements, L"save", x, y) && x == 30 && y == 28, "control match is preferred over a longer OCR line");
    expect(!locate_element(elements, L"missing", x, y), "unknown label is not a click");

    const int width = 640;
    const int height = 220;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC memory = CreateCompatibleDC(screen);
    HGDIOBJ old = SelectObject(memory, bitmap);
    RECT box{0, 0, width, height};
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(memory, &box, white);
    DeleteObject(white);
    HFONT font = CreateFontW(-120, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Arial");
    HGDIOBJ old_font = SelectObject(memory, font);
    SetBkMode(memory, TRANSPARENT);
    SetTextColor(memory, RGB(0, 0, 0));
    DrawTextW(memory, L"1500", -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(memory, old_font);
    SelectObject(memory, old);
    DeleteObject(font);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    OcrPass pass = recognize_bgra(static_cast<const std::uint8_t*>(bits), width, height, width * 4);
    DeleteObject(bitmap);
    std::wstring heard;
    for (const auto& line : pass.lines) heard += line.text;
    bool digits = heard.find(L"1500") != std::wstring::npos;
    if (!pass.ok || !digits) {
        std::wcout << L"OCR text: " << heard << L"\n";
        if (!pass.error.empty()) std::wcout << L"OCR error: " << pass.error << L"\n";
    }
    expect(pass.ok && digits, "Windows OCR reads 1500 from a captured bitmap");

    std::cout << (g_failed ? "FAILED\n" : "ALL PASSED\n");
    return g_failed ? 1 : 0;
}
