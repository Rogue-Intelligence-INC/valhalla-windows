#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ConversationMeta {
    std::string id;
    std::string title;
    std::int64_t updated = 0;
};

struct ConversationLibrary {
    std::wstring root;
    std::string active;
    std::vector<ConversationMeta> items;
};

bool load_library(ConversationLibrary& library);
bool import_legacy(ConversationLibrary& library, const std::wstring& legacy_path);
std::string create_conversation(ConversationLibrary& library);
bool select_conversation(ConversationLibrary& library, const std::string& id);
bool delete_conversation(ConversationLibrary& library, const std::string& id);
void note_conversation(ConversationLibrary& library, const std::string& id, const std::string& title);
std::wstring conversation_file(const ConversationLibrary& library, const std::string& id);
