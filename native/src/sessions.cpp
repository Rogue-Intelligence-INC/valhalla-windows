#include "sessions.h"
#include "utf.h"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <algorithm>

namespace {

std::int64_t now_seconds() {
    FILETIME file{};
    GetSystemTimeAsFileTime(&file);
    ULARGE_INTEGER value{};
    value.LowPart = file.dwLowDateTime;
    value.HighPart = file.dwHighDateTime;
    return static_cast<std::int64_t>(value.QuadPart / 10000000ULL - 11644473600ULL);
}

std::string make_id() {
    static unsigned counter = 0;
    wchar_t buffer[40];
    swprintf_s(buffer, L"%llx-%u", static_cast<unsigned long long>(GetTickCount64()), ++counter);
    return wide_to_utf8(buffer);
}

bool read_text(const std::wstring& path, std::string& out) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 8 * 1024 * 1024) {
        CloseHandle(file);
        return false;
    }
    out.assign(static_cast<size_t>(size.QuadPart), '\0');
    DWORD got = 0;
    BOOL ok = out.empty() || ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &got, nullptr);
    CloseHandle(file);
    if (!ok) return false;
    out.resize(got);
    return true;
}

bool write_text(const std::wstring& path, const std::string& body) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    BOOL ok = WriteFile(file, body.data(), static_cast<DWORD>(body.size()), &wrote, nullptr);
    CloseHandle(file);
    return ok && wrote == body.size();
}

void save_index(const ConversationLibrary& library) {
    nlohmann::json items = nlohmann::json::array();
    for (const auto& item : library.items) {
        items.push_back({{"id", item.id}, {"title", item.title}, {"updated", item.updated}});
    }
    nlohmann::json root = {{"active", library.active}, {"items", items}};
    CreateDirectoryW(library.root.c_str(), nullptr);
    write_text(library.root + L"\\index.json", root.dump(2));
}

}  // namespace

std::wstring conversation_file(const ConversationLibrary& library, const std::string& id) {
    return library.root + L"\\" + utf8_to_wide(id) + L".json";
}

bool load_library(ConversationLibrary& library) {
    CreateDirectoryW(library.root.c_str(), nullptr);
    std::string raw;
    bool existed = read_text(library.root + L"\\index.json", raw);
    library.items.clear();
    library.active.clear();
    if (existed) {
        auto parsed = nlohmann::json::parse(raw, nullptr, false);
        if (!parsed.is_discarded() && parsed.contains("items") && parsed["items"].is_array()) {
            library.active = parsed.value("active", "");
            for (const auto& item : parsed["items"]) {
                if (!item.is_object()) continue;
                ConversationMeta meta;
                meta.id = item.value("id", "");
                meta.title = item.value("title", "");
                meta.updated = item.value("updated", static_cast<std::int64_t>(0));
                if (!meta.id.empty()) library.items.push_back(std::move(meta));
            }
        }
    }
    if (library.items.empty()) create_conversation(library);
    bool found = false;
    for (const auto& item : library.items) {
        if (item.id == library.active) found = true;
    }
    if (!found) {
        library.active = library.items.front().id;
        save_index(library);
    }
    return existed;
}

bool import_legacy(ConversationLibrary& library, const std::wstring& legacy_path) {
    if (GetFileAttributesW(legacy_path.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    if (library.active.empty()) return false;
    std::wstring dest = conversation_file(library, library.active);
    if (!CopyFileW(legacy_path.c_str(), dest.c_str(), FALSE)) return false;
    std::string raw;
    std::string title = "已导入的对话";
    if (read_text(dest, raw)) {
        auto parsed = nlohmann::json::parse(raw, nullptr, false);
        if (!parsed.is_discarded() && parsed.contains("lines") && parsed["lines"].is_array()) {
            for (const auto& line : parsed["lines"]) {
                if (line.is_object() && line.value("user", false)) {
                    std::string text = line.value("text", "");
                    auto cut = text.find('\n');
                    if (cut != std::string::npos) text.resize(cut);
                    if (!text.empty()) {
                        title = text.size() > 96 ? text.substr(0, 96) : text;
                        break;
                    }
                }
            }
        }
    }
    note_conversation(library, library.active, title);
    return true;
}

std::string create_conversation(ConversationLibrary& library) {
    ConversationMeta meta;
    meta.id = make_id();
    meta.updated = now_seconds();
    library.items.insert(library.items.begin(), meta);
    library.active = meta.id;
    CreateDirectoryW(library.root.c_str(), nullptr);
    write_text(conversation_file(library, meta.id), "{\"lines\":[],\"history\":[]}");
    save_index(library);
    return meta.id;
}

bool select_conversation(ConversationLibrary& library, const std::string& id) {
    for (const auto& item : library.items) {
        if (item.id != id) continue;
        library.active = id;
        save_index(library);
        return true;
    }
    return false;
}

bool delete_conversation(ConversationLibrary& library, const std::string& id) {
    auto it = std::find_if(library.items.begin(), library.items.end(), [&](const ConversationMeta& item) { return item.id == id; });
    if (it == library.items.end()) return false;
    DeleteFileW(conversation_file(library, id).c_str());
    bool active = library.active == id;
    library.items.erase(it);
    if (library.items.empty()) create_conversation(library);
    else if (active) library.active = library.items.front().id;
    save_index(library);
    return true;
}

void note_conversation(ConversationLibrary& library, const std::string& id, const std::string& title) {
    for (auto it = library.items.begin(); it != library.items.end(); ++it) {
        if (it->id != id) continue;
        ConversationMeta meta = *it;
        meta.title = title;
        meta.updated = now_seconds();
        library.items.erase(it);
        library.items.insert(library.items.begin(), meta);
        library.active = id;
        save_index(library);
        return;
    }
}
