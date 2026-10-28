#pragma once

#include <string>
#include <vector>

// Desktop library shaped after the S-WIND knowledge base:
// separate collections, JSON on disk, and newly imported items left pending
// rather than treated as verified. This module does not implement S-WIND discovery.

struct KbRecord {
    std::string id;
    std::string kind;
    std::string domain;
    std::string title;
    std::string body;
    std::string source;
    std::string format;
    std::string status;
};

struct KnowledgeBase {
    std::vector<KbRecord> records;
};

struct ImportReport {
    int added = 0;
    int recognized = 0;
    int failed = 0;
    std::string detail;
};

bool load_knowledge(const std::wstring& path, KnowledgeBase& kb);
bool save_knowledge(const std::wstring& path, const KnowledgeBase& kb);
void sync_notes(KnowledgeBase& kb, const std::string& notes);
ImportReport import_industry_file(KnowledgeBase& kb, const std::wstring& path, const std::string& domain);
std::string retrieve_industry(const KnowledgeBase& kb, const std::string& question, int limit);
std::string compose_corpus(const KnowledgeBase& kb, const std::string& notes, const std::string& question);
int industry_domains(const KnowledgeBase& kb);
int industry_records(const KnowledgeBase& kb);
bool has_industry(const KnowledgeBase& kb);
