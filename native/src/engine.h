#pragma once

#include <string>
#include <utility>
#include <vector>

struct Settings {
    std::string language = "system";
    std::string strategy = "open";
    std::string model_repo = "Qwen/Qwen2.5-0.5B-Instruct";
    std::string knowledge =
        "ValhallaBase primary protocol is valhalla-base-v1.\n"
        "Production decode mode is hybrid with native retrieval for open QA.";
    bool keep_session = true;
    bool allow_control = false;
};

Settings load_settings(const std::wstring& path);
void save_settings(const std::wstring& path, const Settings& settings);

struct TurnResult {
    std::string answer;
    std::string decode_path;
    std::string session_json;
    std::string error;
};

std::wstring find_binary();
std::wstring model_directory(const std::string& repo_id);
bool weights_present(const std::wstring& directory);

struct TranscriptLine {
    bool user = false;
    std::string text;
    std::string path;
};

struct Transcript {
    std::vector<TranscriptLine> lines;
    std::vector<std::pair<std::string, std::string>> history;
    std::string session_json;
};

Transcript load_transcript(const std::wstring& path);
void save_transcript(const std::wstring& path, const Transcript& transcript);

TurnResult run_chat(
    const std::string& knowledge,
    const std::string& question,
    const std::string& mode,
    const std::wstring& model_dir,
    const std::string& session_json,
    const std::vector<std::pair<std::string, std::string>>& history,
    const std::string& screen,
    bool allow_control);

void stop_warm_process();
std::wstring ensure_framework(const std::wstring& model_dir);
std::wstring gpu_status();

struct CudaRuntime {
    std::wstring version;
    std::wstring root;
    std::wstring bin;
    std::wstring dll;
    std::wstring driver;
    int major = 0;
    int minor = 0;
    bool loaded = false;
    std::vector<std::wstring> others;
};

CudaRuntime cuda_runtime();
std::wstring format_cuda(bool zh);

struct FeatureReport {
    bool ok = false;
    std::wstring text;
};

FeatureReport run_feature_probe(bool zh);

using DownloadStatus = void (*)(const std::wstring& text, void* user);

bool download_model(const std::string& repo_id, DownloadStatus status, void* user, std::wstring& error);
