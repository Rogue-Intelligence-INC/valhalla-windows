#include "desktop.h"
#include "engine.h"
#include "utf.h"

#include <objbase.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <cwctype>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using namespace Gdiplus;

namespace {

enum {
    ID_SEND = 101,
    ID_NEW = 102,
    ID_SETTINGS = 103,
    ID_BACK = 104,
    ID_SCREEN = 105,
    ID_CONTROL = 106,
    ID_TASK = 107,
    ID_PROBE = 108,
    ID_NAV0 = 110,
    ID_NAV1 = 111,
    ID_NAV2 = 112,
    ID_SESSION = 120,
    ID_LANG = 130,
    ID_STRATEGY = 131,
    ID_MODEL = 200,
    WM_CHAT_DONE = WM_APP + 1,
    WM_MODEL_NOTE = WM_APP + 2,
    WM_MODEL_DONE = WM_APP + 3,
    WM_PROBE_DONE = WM_APP + 4,
    WM_FRAMEWORK = WM_APP + 5,
};

struct ModelInfo {
    const char* repo;
    const wchar_t* label;
    const wchar_t* size;
    const wchar_t* note;
};

constexpr ModelInfo kModels[] = {
    {"Qwen/Qwen2.5-0.5B-Instruct", L"Qwen2.5 0.5B", L"1 GB", L"note_0_5"},
    {"Qwen/Qwen2.5-1.5B-Instruct", L"Qwen2.5 1.5B", L"3 GB", L"note_1_5"},
    {"Qwen/Qwen2.5-3B-Instruct", L"Qwen2.5 3B", L"6 GB", L"note_3"},
    {"Qwen/Qwen2.5-7B-Instruct", L"Qwen2.5 7B", L"15 GB", L"note_7"},
};

struct ChatLine {
    bool user = false;
    std::wstring text;
    std::wstring path;
};

struct Bubble {
    bool user = false;
    std::wstring who;
    std::wstring text;
    std::wstring path;
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
};

struct ModelNote {
    int index = 0;
    std::wstring text;
};

struct ModelDone {
    int index = 0;
    bool ok = false;
    std::wstring error;
};

struct App {
    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
    HWND title = nullptr;
    HWND hint = nullptr;
    HWND new_chat = nullptr;
    HWND settings_button = nullptr;
    HWND screen_button = nullptr;
    HWND control_button = nullptr;
    HWND task_button = nullptr;
    bool task_mode = false;
    HWND chat = nullptr;
    HWND composer = nullptr;
    HWND send = nullptr;
    HWND status = nullptr;
    HWND back = nullptr;
    HWND header = nullptr;
    HWND nav[3] = {};
    HWND knowledge_title = nullptr;
    HWND knowledge_help = nullptr;
    HWND knowledge = nullptr;
    HWND models_title = nullptr;
    HWND models_help = nullptr;
    HWND model_title[4] = {};
    HWND model_note[4] = {};
    HWND model_button[4] = {};
    HWND general_title = nullptr;
    HWND language_label = nullptr;
    HWND language = nullptr;
    HWND strategy_label = nullptr;
    HWND strategy = nullptr;
    HWND strategy_detail = nullptr;
    HWND session_check = nullptr;
    HWND session_help = nullptr;
    HWND runtime = nullptr;
    HWND probe_button = nullptr;
    bool mica = false;
    std::wstring probe_text;
    HFONT font = nullptr;
    HFONT font_body = nullptr;
    HFONT font_small = nullptr;
    HBRUSH bg_brush = nullptr;
    HBRUSH card_brush = nullptr;
    WNDPROC button_proc = nullptr;
    WNDPROC edit_proc = nullptr;
    ULONG_PTR gdi = 0;
    COLORREF accent = RGB(0, 120, 212);
    Settings settings;
    std::wstring lang = L"zh";
    std::wstring settings_path;
    int page = 0;
    int section = 0;
    bool busy = false;
    bool filling = false;
    bool stick = true;
    int scroll = 0;
    int content_h = 0;
    int chat_width = 0;
    std::vector<ChatLine> messages;
    std::vector<Bubble> bubbles;
    std::vector<std::pair<std::string, std::string>> history;
    std::string session_json;
    std::string screen_context;
    std::wstring transcript_path;
    std::wstring framework_note;
};

App g;

int Dpi() { return g.hwnd ? static_cast<int>(GetDpiForWindow(g.hwnd)) : 96; }
int S(int px) { return MulDiv(px, Dpi(), 96); }

const wchar_t* Tr(const wchar_t* key) {
    struct Row {
        const wchar_t* key;
        const wchar_t* zh;
        const wchar_t* en;
    };
    static const Row rows[] = {
        {L"chat_hint", L"先走当前策略，再写成完整回答。", L"The strategy runs first, then the reply is written out."},
        {L"empty_title", L"开始对话", L"Start a conversation"},
        {L"badge_retrieved", L"检索后作答", L"Retrieved, then written"},
        {L"badge_talk", L"续写", L"Continued"},
        {L"badge_mcq", L"选择题混合", L"Hybrid choice"},
        {L"badge_multi", L"多跳后作答", L"Multi-hop, then written"},
        {L"screen", L"读取屏幕", L"Read screen"},
        {L"control", L"控制电脑", L"Control"},
        {L"control_on", L"控制中", L"Control on"},
        {L"screen_done", L"已带上屏幕", L"Screen attached"},
        {L"task", L"任务布置", L"Task"},
        {L"task_on", L"任务中", L"Task on"},
        {L"cue", L"输入消息", L"Message"},
        {L"settings", L"设置", L"Settings"},
        {L"back", L"返回", L"Back"},
        {L"new_chat", L"新对话", L"New chat"},
        {L"send", L"发送", L"Send"},
        {L"you", L"你", L"You"},
        {L"assistant", L"Valhalla", L"Valhalla"},
        {L"thinking", L"正在回答…", L"Answering…"},
        {L"framework_loading", L"正在启动 Valhalla，模型只载入这一次…", L"Starting Valhalla. The model loads once…"},
        {L"framework_ready", L"模型已常驻，后续对话不再重新加载。", L"The model is resident. Later turns will not load it again."},
        {L"empty_chat", L"问一件事，或直接布置任务。", L"Ask something, or give it a task."},
        {L"need_knowledge", L"知识库是空的。打开设置，在「知识库」里写上语料。", L"The knowledge base is empty. Add lines under Settings, Knowledge."},
        {L"need_model", L"这个模型还没有权重。到设置的「模型」页下载。", L"That model has no weights yet. Download it under Settings, Models."},
        {L"nav_knowledge", L"知识库", L"Knowledge"},
        {L"nav_models", L"模型", L"Models"},
        {L"nav_general", L"综合", L"General"},
        {L"knowledge_title", L"知识库", L"Knowledge"},
        {L"knowledge_help", L"一行一条。命中的句子会作为回答依据，不会只把原文碎片贴出来。", L"One fact per line. A hit becomes the basis of the reply, not a pasted fragment."},
        {L"models_title", L"模型", L"Models"},
        {L"models_help", L"只列出本机解码器能加载的免费 Qwen2.5 Instruct。", L"Free Qwen2.5 Instruct checkpoints this decoder can load."},
        {L"general_title", L"综合", L"General"},
        {L"language", L"语言", L"Language"},
        {L"lang_system", L"跟随系统", L"Match system"},
        {L"lang_zh", L"中文", L"中文"},
        {L"lang_en", L"English", L"English"},
        {L"strategy", L"回答策略", L"Answer strategy"},
        {L"keep_session", L"多轮沿用上一轮会话", L"Keep the session across turns"},
        {L"keep_session_help", L"打开后，后面的问题带着上一轮结构会话。", L"Later questions reuse the previous structure session."},
        {L"installed", L"已安装", L"Installed"},
        {L"not_installed", L"未下载", L"Not downloaded"},
        {L"download", L"下载", L"Download"},
        {L"use", L"使用", L"Use"},
        {L"in_use", L"当前", L"In use"},
        {L"about", L"约", L"About"},
        {L"download_done", L"下载完成。", L"Download finished."},
        {L"failed", L"没有完成。", L"Did not finish."},
        {L"binary_ok", L"运行时", L"Runtime"},
        {L"binary_missing", L"找不到 valhalla_base.exe", L"valhalla_base.exe was not found"},
        {L"probe", L"检查特性", L"Check features"},
        {L"probing", L"正在检查特性…", L"Checking features…"},
        {L"strategy_open", L"忠实检索", L"Faithful retrieval"},
        {L"strategy_mcq", L"选择题混合解码", L"Hybrid multiple choice"},
        {L"strategy_followup", L"追问会话", L"Follow-up session"},
        {L"strategy_multihop", L"多跳", L"Multi-hop"},
        {L"delta_open", L"RAGTruth 34/40，对照 0/40", L"RAGTruth 34/40 versus 0/40"},
        {L"delta_mcq", L"0.5B 约 +48 个百分点", L"About +48 pp at 0.5B"},
        {L"delta_followup", L"30 轮追问 96.7%", L"96.7% on 30-turn follow-up"},
        {L"delta_multihop", L"MultiHop 9/15，对照 8/15", L"MultiHop 9/15 versus 8/15"},
        {L"note_0_5", L"默认小模型。选择题混合解码约 63%。", L"Default small model. Hybrid MCQ is about 63%."},
        {L"note_1_5", L"免费 Qwen2 权重，可以直接加载。", L"Free Qwen2 weights this decoder can load."},
        {L"note_3", L"对照实验用的 3B。选择题 45/46。", L"The 3B used in the paired comparisons. MCQ 45/46."},
        {L"note_7", L"免费 7B。选择题 44/46，没有再高于 3B。", L"Free 7B. MCQ 44/46, no further gain over 3B."},
    };
    bool zh = g.lang == L"zh";
    for (const auto& row : rows) {
        if (wcscmp(row.key, key) == 0) return zh ? row.zh : row.en;
    }
    return key;
}

std::wstring resolved_language(const std::string& choice) {
    if (choice == "zh" || choice == "en") return utf8_to_wide(choice);
    return (GetUserDefaultUILanguage() & 0x3FF) == 0x04 ? L"zh" : L"en";
}

std::wstring window_text(HWND hwnd) {
    int count = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<size_t>(count) + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), count + 1);
    text.resize(static_cast<size_t>(count));
    return text;
}

std::wstring to_edit(const std::string& utf8) {
    std::wstring text = utf8_to_wide(utf8);
    std::wstring out;
    for (wchar_t ch : text) {
        if (ch == L'\n') out += L"\r\n";
        else if (ch != L'\r') out.push_back(ch);
    }
    return out;
}

std::string from_edit(const std::wstring& text) {
    std::wstring out;
    for (wchar_t ch : text)
        if (ch != L'\r') out.push_back(ch);
    return wide_to_utf8(out);
}

void save_now() { save_settings(g.settings_path, g.settings); }

void store_transcript() {
    if (g.transcript_path.empty()) return;
    Transcript transcript;
    transcript.lines.reserve(g.messages.size());
    for (const auto& line : g.messages) {
        transcript.lines.push_back(TranscriptLine{line.user, wide_to_utf8(line.text), wide_to_utf8(line.path)});
    }
    transcript.history = g.history;
    transcript.session_json = g.session_json;
    save_transcript(g.transcript_path, transcript);
}

void restore_transcript() {
    Transcript transcript = load_transcript(g.transcript_path);
    for (const auto& line : transcript.lines) {
        g.messages.push_back(ChatLine{line.user, utf8_to_wide(line.text), utf8_to_wide(line.path)});
    }
    g.history = std::move(transcript.history);
    g.session_json = std::move(transcript.session_json);
    g.stick = true;
    g.chat_width = 0;
}

void read_accent() {
    g.accent = RGB(0, 120, 212);
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegQueryValueExW(key, L"AccentColor", nullptr, nullptr, reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS) {
        g.accent = RGB(value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF);
    }
    RegCloseKey(key);
}

void enable_dark(HWND hwnd) {
    HMODULE theme = GetModuleHandleW(L"uxtheme.dll");
    if (!theme) theme = LoadLibraryW(L"uxtheme.dll");
    auto allow = reinterpret_cast<void(WINAPI*)(HWND, BOOL)>(GetProcAddress(theme, MAKEINTRESOURCEA(133)));
    if (allow) allow(hwnd, TRUE);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    int round = 2;
    DwmSetWindowAttribute(hwnd, 33, &round, sizeof(round));
    int backdrop = 4;
    DwmSetWindowAttribute(hwnd, 38, &backdrop, sizeof(backdrop));
    g.mica = false;
    COLORREF text = RGB(255, 255, 255);
    DwmSetWindowAttribute(hwnd, 36, &text, sizeof(text));
    COLORREF caption = RGB(24, 24, 26);
    DwmSetWindowAttribute(hwnd, 34, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
}

bool font_installed(const wchar_t* name) {
    HDC dc = GetDC(nullptr);
    LOGFONTW metric{};
    wcscpy_s(metric.lfFaceName, name);
    metric.lfCharSet = DEFAULT_CHARSET;
    bool found = false;
    EnumFontFamiliesExW(dc, &metric, [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM param) -> int {
        *reinterpret_cast<bool*>(param) = true;
        return 0;
    }, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

const wchar_t* ui_face() {
    static const wchar_t* face = font_installed(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
    return face;
}

const wchar_t* title_face() {
    static const wchar_t* face = font_installed(L"Segoe UI Variable Display") ? L"Segoe UI Variable Display" : ui_face();
    return face;
}

FontFamily& ui_family() {
    static FontFamily variable(L"Segoe UI Variable Text");
    static FontFamily segoe(L"Segoe UI");
    if (variable.IsAvailable()) return variable;
    return segoe;
}

HFONT make_font(int px, int weight, bool title) {
    return CreateFontW(-S(px), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, title ? title_face() : ui_face());
}

void add_round(GraphicsPath& path, const RectF& rect, float radius) {
    float d = std::min(radius * 2.f, std::min(rect.Width, rect.Height));
    float r = d / 2.f;
    path.AddArc(rect.X, rect.Y, d, d, 180, 90);
    path.AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270, 90);
    path.AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0, 90);
    path.AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90, 90);
    path.CloseFigure();
    (void)r;
}

void show_item(HWND hwnd, bool visible) {
    if (hwnd) ShowWindow(hwnd, visible ? SW_SHOW : SW_HIDE);
}

void layout();
void apply_page();
void refresh_models();
void sync_text();
void append_line(bool user, const std::wstring& text, const std::wstring& path);

const char* strategy_id(int index) {
    static const char* ids[] = {"open", "mcq", "followup", "multihop"};
    if (index < 0 || index > 3) return "open";
    return ids[index];
}

int strategy_index(const std::string& id) {
    if (id == "mcq") return 1;
    if (id == "followup") return 2;
    if (id == "multihop") return 3;
    return 0;
}

const wchar_t* strategy_title_key(int index) {
    static const wchar_t* keys[] = {L"strategy_open", L"strategy_mcq", L"strategy_followup", L"strategy_multihop"};
    return keys[index];
}

const wchar_t* strategy_delta_key(int index) {
    static const wchar_t* keys[] = {L"delta_open", L"delta_mcq", L"delta_followup", L"delta_multihop"};
    return keys[index];
}

void pull_knowledge() {
    std::string text = trim_utf8(from_edit(window_text(g.knowledge)));
    if (text != g.settings.knowledge) {
        g.settings.knowledge = text;
        save_now();
        g.session_json.clear();
        g.history.clear();
        store_transcript();
    }
}

void fill_choices() {
    g.filling = true;
    SendMessageW(g.language, CB_RESETCONTENT, 0, 0);
    SendMessageW(g.language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Tr(L"lang_system")));
    SendMessageW(g.language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Tr(L"lang_zh")));
    SendMessageW(g.language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Tr(L"lang_en")));
    int language = 0;
    if (g.settings.language == "zh") language = 1;
    else if (g.settings.language == "en") language = 2;
    SendMessageW(g.language, CB_SETCURSEL, language, 0);

    SendMessageW(g.strategy, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < 4; ++i) SendMessageW(g.strategy, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Tr(strategy_title_key(i))));
    SendMessageW(g.strategy, CB_SETCURSEL, strategy_index(g.settings.strategy), 0);
    SetWindowTextW(g.strategy_detail, Tr(strategy_delta_key(strategy_index(g.settings.strategy))));
    g.filling = false;
}

void refresh_models() {
    for (int i = 0; i < 4; ++i) {
        bool installed = weights_present(model_directory(kModels[i].repo));
        bool current = g.settings.model_repo == kModels[i].repo;
        std::wstring title = std::wstring(kModels[i].label) + L"    " + Tr(L"about") + L" " + kModels[i].size + L"    " +
                             (installed ? Tr(L"installed") : Tr(L"not_installed"));
        if (current) title += std::wstring(L"    ") + Tr(L"in_use");
        SetWindowTextW(g.model_title[i], title.c_str());
        SetWindowTextW(g.model_note[i], Tr(kModels[i].note));
        if (installed && !current) {
            SetWindowTextW(g.model_button[i], Tr(L"use"));
            show_item(g.model_button[i], g.page == 1 && g.section == 1);
            EnableWindow(g.model_button[i], !g.busy);
        } else if (!installed) {
            SetWindowTextW(g.model_button[i], Tr(L"download"));
            show_item(g.model_button[i], g.page == 1 && g.section == 1);
            EnableWindow(g.model_button[i], !g.busy);
        } else {
            show_item(g.model_button[i], FALSE);
        }
    }
}

std::wstring hint_line() {
    std::wstring text = std::wstring(Tr(strategy_title_key(strategy_index(g.settings.strategy)))) + L"    " + Tr(L"chat_hint");
    std::wstring gpu = gpu_status();
    if (!gpu.empty()) text += L"    " + gpu;
    return text;
}

std::wstring badge_for(const std::wstring& path) {
    if (path.empty()) return L"";
    if (path.find(L'/') == std::wstring::npos && path.find(L"valhalla") == std::wstring::npos) return path;
    std::wstring folded = path;
    for (wchar_t& ch : folded) ch = towlower(ch);
    if (folded.find(L"mcq") != std::wstring::npos || folded.find(L"logprob") != std::wstring::npos) return Tr(L"badge_mcq");
    if (folded.find(L"multihop") != std::wstring::npos) return Tr(L"badge_multi");
    if (folded.find(L"phrase") != std::wstring::npos || folded.find(L"native") != std::wstring::npos ||
        folded.find(L"retrieval") != std::wstring::npos) {
        return Tr(L"badge_retrieved");
    }
    return Tr(L"badge_talk");
}

void sync_text() {
    g.lang = resolved_language(g.settings.language);
    SetWindowTextW(g.hwnd, L"Valhalla");
    SetWindowTextW(g.title, L"Valhalla");
    SetWindowTextW(g.hint, hint_line().c_str());
    SetWindowTextW(g.new_chat, Tr(L"new_chat"));
    SetWindowTextW(g.settings_button, Tr(L"settings"));
    SetWindowTextW(g.screen_button, Tr(L"screen"));
    SetWindowTextW(g.control_button, g.settings.allow_control ? Tr(L"control_on") : Tr(L"control"));
    SetWindowTextW(g.task_button, g.task_mode ? Tr(L"task_on") : Tr(L"task"));
    SendMessageW(g.composer, 0x1501, TRUE, reinterpret_cast<LPARAM>(Tr(L"cue")));
    SetWindowTextW(g.send, Tr(L"send"));
    SetWindowTextW(g.back, Tr(L"back"));
    SetWindowTextW(g.header, Tr(L"settings"));
    SetWindowTextW(g.nav[0], Tr(L"nav_knowledge"));
    SetWindowTextW(g.nav[1], Tr(L"nav_models"));
    SetWindowTextW(g.nav[2], Tr(L"nav_general"));
    SetWindowTextW(g.knowledge_title, Tr(L"knowledge_title"));
    SetWindowTextW(g.knowledge_help, Tr(L"knowledge_help"));
    SetWindowTextW(g.models_title, Tr(L"models_title"));
    SetWindowTextW(g.models_help, Tr(L"models_help"));
    SetWindowTextW(g.general_title, Tr(L"general_title"));
    SetWindowTextW(g.language_label, Tr(L"language"));
    SetWindowTextW(g.strategy_label, Tr(L"strategy"));
    SetWindowTextW(g.session_check, Tr(L"keep_session"));
    SetWindowTextW(g.session_help, Tr(L"keep_session_help"));
    std::wstring binary = find_binary();
    std::wstring runtime = g.probe_text.empty() ? format_cuda(g.lang == L"zh") : g.probe_text;
    runtime += L"\n";
    runtime += binary.empty() ? Tr(L"binary_missing") : std::wstring(Tr(L"binary_ok")) + L"  " + binary;
    SetWindowTextW(g.runtime, runtime.c_str());
    SetWindowTextW(g.probe_button, Tr(L"probe"));
    fill_choices();
    refresh_models();
    InvalidateRect(g.hwnd, nullptr, TRUE);
    InvalidateRect(g.chat, nullptr, FALSE);
}

void apply_fonts() {
    HFONT title = make_font(22, FW_SEMIBOLD, true);
    HFONT body = make_font(15, FW_NORMAL, false);
    HFONT small_font = make_font(12, FW_NORMAL, false);
    SendMessageW(g.title, WM_SETFONT, reinterpret_cast<WPARAM>(title), TRUE);
    SendMessageW(g.header, WM_SETFONT, reinterpret_cast<WPARAM>(title), TRUE);
    HWND body_controls[] = {g.composer, g.knowledge, g.knowledge_title, g.models_title, g.general_title, g.language_label,
                            g.language, g.strategy_label, g.strategy, g.session_check, g.probe_button};
    for (HWND hwnd : body_controls)
        if (hwnd) SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(body), TRUE);
    HWND small_controls[] = {g.hint, g.status, g.knowledge_help, g.models_help, g.strategy_detail, g.session_help, g.runtime};
    for (HWND hwnd : small_controls)
        if (hwnd) SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(small_font), TRUE);
    for (int i = 0; i < 4; ++i) {
        SendMessageW(g.model_title[i], WM_SETFONT, reinterpret_cast<WPARAM>(body), TRUE);
        SendMessageW(g.model_note[i], WM_SETFONT, reinterpret_cast<WPARAM>(small_font), TRUE);
    }
    if (g.font) DeleteObject(g.font);
    if (g.font_body) DeleteObject(g.font_body);
    if (g.font_small) DeleteObject(g.font_small);
    g.font = title;
    g.font_body = body;
    g.font_small = small_font;
}

void layout() {
    RECT client{};
    GetClientRect(g.hwnd, &client);
    int width = client.right;
    int height = client.bottom;
    int margin = S(28);
    int gap = S(8);
    int column = std::min(S(760), std::max(S(420), width - margin * 2));
    int origin = std::max(margin, (width - column) / 2);
    int settings_w = S(72);
    int new_w = S(84);
    int settings_x = std::max(margin, width - margin - settings_w);
    int new_x = std::max(margin, settings_x - gap - new_w);
    int title_w = std::max(S(80), new_x - gap - margin);
    MoveWindow(g.title, margin, S(14), title_w, S(28), TRUE);
    MoveWindow(g.hint, 0, 0, 0, 0, TRUE);
    MoveWindow(g.new_chat, new_x, S(12), new_w, S(32), TRUE);
    MoveWindow(g.settings_button, settings_x, S(12), settings_w, S(32), TRUE);
    int chat_top = S(56);
    int well_h = S(140);
    int status_h = S(20);
    int composer_top = height - well_h - S(36);
    if (composer_top < chat_top + S(80)) composer_top = chat_top + S(80);
    MoveWindow(g.status, origin, composer_top - status_h - S(6), column, status_h, TRUE);
    MoveWindow(g.chat, 0, chat_top, std::max(10, width), std::max(10, composer_top - status_h - chat_top - S(10)), TRUE);
    int pad = S(16);
    int tool_h = S(32);
    int tool_y = composer_top + well_h - pad - tool_h;
    int edit_h = std::max(S(40), tool_y - composer_top - S(14));
    MoveWindow(g.composer, origin + pad, composer_top + S(14), std::max(10, column - pad * 2), edit_h, TRUE);
    SendMessageW(g.composer, 0x00D3, 3, MAKELPARAM(S(2), S(2)));
    int chip_w = S(96);
    int x = origin + pad;
    MoveWindow(g.screen_button, x, tool_y, chip_w, tool_h, TRUE);
    x += chip_w + gap;
    MoveWindow(g.control_button, x, tool_y, chip_w, tool_h, TRUE);
    x += chip_w + gap;
    MoveWindow(g.task_button, x, tool_y, chip_w, tool_h, TRUE);
    int send_w = S(76);
    int send_x = origin + column - pad - send_w;
    MoveWindow(g.send, send_x, tool_y, send_w, tool_h, TRUE);

    MoveWindow(g.back, S(20), S(16), S(88), S(36), TRUE);
    MoveWindow(g.header, S(124), S(16), S(320), S(36), TRUE);
    for (int i = 0; i < 3; ++i) MoveWindow(g.nav[i], S(20), S(76) + i * S(48), S(180), S(40), TRUE);
    int left = S(224);
    int content_w = std::max(10, width - left - S(24));
    MoveWindow(g.knowledge_title, left, S(76), content_w, S(28), TRUE);
    MoveWindow(g.knowledge_help, left, S(108), content_w, S(40), TRUE);
    MoveWindow(g.knowledge, left, S(156), content_w, std::max(10, height - S(180)), TRUE);
    MoveWindow(g.models_title, left, S(76), content_w, S(28), TRUE);
    MoveWindow(g.models_help, left, S(108), content_w, S(40), TRUE);
    for (int i = 0; i < 4; ++i) {
        int top = S(160) + i * S(92);
        MoveWindow(g.model_title[i], left, top, content_w - S(120), S(24), TRUE);
        MoveWindow(g.model_note[i], left, top + S(26), content_w - S(120), S(36), TRUE);
        MoveWindow(g.model_button[i], left + content_w - S(108), top, S(100), S(36), TRUE);
    }
    MoveWindow(g.general_title, left, S(76), content_w, S(28), TRUE);
    MoveWindow(g.language_label, left, S(120), content_w, S(20), TRUE);
    MoveWindow(g.language, left, S(144), S(240), S(240), TRUE);
    MoveWindow(g.strategy_label, left, S(196), content_w, S(20), TRUE);
    MoveWindow(g.strategy, left, S(220), S(360), S(240), TRUE);
    MoveWindow(g.strategy_detail, left, S(256), content_w, S(24), TRUE);
    MoveWindow(g.session_check, left, S(300), content_w, S(28), TRUE);
    MoveWindow(g.session_help, left, S(332), content_w, S(36), TRUE);
    MoveWindow(g.runtime, left, S(372), content_w, S(96), TRUE);
    MoveWindow(g.probe_button, left, S(476), S(148), S(36), TRUE);
    g.chat_width = 0;
}

void apply_page() {
    bool chat = g.page == 0;
    show_item(g.title, chat);
    show_item(g.hint, false);
    show_item(g.new_chat, chat);
    show_item(g.settings_button, chat);
    show_item(g.screen_button, chat);
    show_item(g.control_button, chat);
    show_item(g.task_button, chat);
    show_item(g.chat, chat);
    show_item(g.composer, chat);
    show_item(g.send, chat);
    show_item(g.status, chat);
    show_item(g.back, !chat);
    show_item(g.header, !chat);
    for (HWND hwnd : g.nav) show_item(hwnd, !chat);
    bool knowledge = !chat && g.section == 0;
    bool models = !chat && g.section == 1;
    bool general = !chat && g.section == 2;
    show_item(g.knowledge_title, knowledge);
    show_item(g.knowledge_help, knowledge);
    show_item(g.knowledge, knowledge);
    show_item(g.models_title, models);
    show_item(g.models_help, models);
    for (int i = 0; i < 4; ++i) {
        show_item(g.model_title[i], models);
        show_item(g.model_note[i], models);
    }
    show_item(g.general_title, general);
    show_item(g.language_label, general);
    show_item(g.language, general);
    show_item(g.strategy_label, general);
    show_item(g.strategy, general);
    show_item(g.strategy_detail, general);
    show_item(g.session_check, general);
    show_item(g.session_help, general);
    show_item(g.runtime, general);
    show_item(g.probe_button, general);
    refresh_models();
    for (HWND hwnd : g.nav) InvalidateRect(hwnd, nullptr, TRUE);
    InvalidateRect(g.hwnd, nullptr, TRUE);
}

void remember_knowledge_and(int section) {
    pull_knowledge();
    g.section = section;
    apply_page();
}

void go_settings() {
    g.page = 1;
    g.section = 0;
    apply_page();
}

void go_chat() {
    pull_knowledge();
    g.page = 0;
    apply_page();
}

void clear_chat() {
    g.messages.clear();
    g.history.clear();
    g.session_json.clear();
    g.screen_context.clear();
    g.scroll = 0;
    g.stick = true;
    g.chat_width = 0;
    SetWindowTextW(g.status, g.framework_note.c_str());
    store_transcript();
    InvalidateRect(g.chat, nullptr, FALSE);
}

void append_line(bool user, const std::wstring& text, const std::wstring& path) {
    g.messages.push_back({user, text, path});
    g.stick = true;
    g.chat_width = 0;
    InvalidateRect(g.chat, nullptr, FALSE);
}

struct ChatDone {
    std::wstring question;
    TurnResult result;
};

void send_chat() {
    if (g.busy) return;
    std::wstring question = trim_copy(window_text(g.composer));
    if (question.empty()) return;
    if (trim_utf8(g.settings.knowledge).empty()) {
        append_line(false, Tr(L"need_knowledge"), L"");
        return;
    }
    std::wstring folder = model_directory(g.settings.model_repo);
    if (!weights_present(folder)) {
        append_line(false, Tr(L"need_model"), L"");
        return;
    }
    SetWindowTextW(g.composer, L"");
    append_line(true, question, L"");
    g.busy = true;
    EnableWindow(g.send, FALSE);
    SetWindowTextW(g.status, L"");
    g.chat_width = 0;
    InvalidateRect(g.chat, nullptr, FALSE);
    auto* job = new ChatDone{};
    job->question = question;
    std::string knowledge = g.settings.knowledge;
    std::string mode = g.settings.strategy;
    std::string session = (g.settings.keep_session || mode == "followup") ? g.session_json : std::string();
    auto history = g.history;
    std::string screen = g.screen_context;
    bool allow_control = g.settings.allow_control;
    std::wstring asked = question;
    if (g.task_mode) asked = L"这是一项要完成的任务。直接做完，并给出结果。\n" + question;
    HWND hwnd = g.hwnd;
    std::thread([job, knowledge, mode, folder, session, history, screen, allow_control, asked, hwnd]() {
        job->result = run_chat(knowledge, wide_to_utf8(asked), mode, folder, session, history, screen, allow_control);
        if (!PostMessageW(hwnd, WM_CHAT_DONE, 0, reinterpret_cast<LPARAM>(job))) delete job;
    }).detach();
}

void finish_chat(ChatDone* done) {
    g.busy = false;
    EnableWindow(g.send, TRUE);
    if (!done->result.error.empty()) {
        append_line(false, utf8_to_wide(done->result.error), L"");
    } else {
        if (g.settings.keep_session || g.settings.strategy == "followup") g.session_json = done->result.session_json;
        else g.session_json.clear();
        std::wstring script;
        std::wstring visible = strip_actions(utf8_to_wide(done->result.answer), script);
        std::wstring report;
        std::wstring screen;
        if (!script.empty()) {
            run_actions(script, g.settings.allow_control, report, screen);
            if (!screen.empty()) g.screen_context = wide_to_utf8(screen);
            if (!report.empty()) {
                if (!visible.empty()) visible += L"\n";
                visible += report;
            }
        }
        if (visible.empty()) visible = utf8_to_wide(done->result.answer);
        if (!done->result.decode_path.empty()) {
            g.history.emplace_back(wide_to_utf8(done->question), wide_to_utf8(visible));
        }
        append_line(false, visible, L"");
    }
    SetWindowTextW(g.status, g.framework_note.c_str());
    store_transcript();
    delete done;
}

void start_download(int index) {
    if (g.busy) return;
    g.busy = true;
    refresh_models();
    SetWindowTextW(g.model_note[index], Tr(L"thinking"));
    auto* note_user = new int(index);
    HWND hwnd = g.hwnd;
    std::string repo = kModels[index].repo;
    std::thread([index, repo, hwnd, note_user]() {
        auto status = [](const std::wstring& text, void* user) {
            int* slot = static_cast<int*>(user);
            auto* note = new ModelNote{*slot, text};
            if (!PostMessageW(g.hwnd, WM_MODEL_NOTE, 0, reinterpret_cast<LPARAM>(note))) delete note;
        };
        std::wstring error;
        bool ok = download_model(repo, status, note_user, error);
        delete note_user;
        auto* done = new ModelDone{index, ok, error};
        if (!PostMessageW(hwnd, WM_MODEL_DONE, 0, reinterpret_cast<LPARAM>(done))) delete done;
    }).detach();
}

void start_framework() {
    std::wstring model = model_directory(g.settings.model_repo);
    if (!weights_present(model)) return;
    g.framework_note = Tr(L"framework_loading");
    if (!g.busy) SetWindowTextW(g.status, g.framework_note.c_str());
    HWND hwnd = g.hwnd;
    std::thread([hwnd, model]() {
        std::wstring error = ensure_framework(model);
        auto* note = new std::wstring(error.empty() ? std::wstring() : error);
        if (!PostMessageW(hwnd, WM_FRAMEWORK, 0, reinterpret_cast<LPARAM>(note))) delete note;
    }).detach();
}

void use_model(int index) {
    g.settings.model_repo = kModels[index].repo;
    save_now();
    g.session_json.clear();
    stop_warm_process();
    refresh_models();
    start_framework();
}

void model_click(int index) {
    if (weights_present(model_directory(kModels[index].repo))) use_model(index);
    else start_download(index);
}

void relayout_chat(Graphics& graphics, int width, int height) {
    g.bubbles.clear();
    if (width < 20) return;
    int column = std::min(S(760), std::max(S(420), width - S(56)));
    int origin = std::max(S(20), (width - column) / 2);
    float user_max = static_cast<float>(std::min(S(520), column - S(48)));
    float assistant_w = static_cast<float>(column);
    graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    FontFamily& family = ui_family();
    Font body_font(&family, static_cast<REAL>(S(16)), FontStyleRegular, UnitPixel);
    StringFormat format;
    StringFormat single;
    single.SetFormatFlags(StringFormatFlagsNoWrap);
    float y = static_cast<float>(S(8));
    auto place = [&](bool user, const std::wstring& text) {
        if (user) {
            RectF wide(0, 0, 4000, 800);
            RectF natural(0, 0, 0, 0);
            graphics.MeasureString(text.c_str(), -1, &body_font, wide, &single, &natural);
            float inner = std::min(user_max - S(28), std::max(natural.Width, static_cast<float>(S(36))));
            RectF box(0, 0, inner, 4000);
            RectF measured(0, 0, 0, 0);
            graphics.MeasureString(text.c_str(), -1, &body_font, box, &format, &measured);
            Bubble bubble;
            bubble.user = true;
            bubble.text = text;
            bubble.w = inner + S(28);
            bubble.h = measured.Height + S(26);
            bubble.y = y;
            bubble.x = static_cast<float>(origin + column) - bubble.w;
            g.bubbles.push_back(bubble);
            y += bubble.h + S(18);
        } else {
            float inner = assistant_w;
            RectF box(0, 0, inner, 4000);
            RectF measured(0, 0, 0, 0);
            graphics.MeasureString(text.c_str(), -1, &body_font, box, &format, &measured);
            Bubble bubble;
            bubble.user = false;
            bubble.who = Tr(L"assistant");
            bubble.text = text;
            bubble.w = inner;
            bubble.h = measured.Height + S(36);
            bubble.y = y;
            bubble.x = static_cast<float>(origin);
            g.bubbles.push_back(bubble);
            y += bubble.h + S(16);
        }
    };
    for (const auto& message : g.messages) place(message.user, message.text);
    if (g.busy) place(false, Tr(L"thinking"));
    g.content_h = static_cast<int>(y + S(8));
    int max_scroll = std::max(0, g.content_h - height);
    if (g.stick) g.scroll = max_scroll;
    if (g.scroll > max_scroll) g.scroll = max_scroll;
    SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS, 0, std::max(height, g.content_h), static_cast<UINT>(height), g.scroll, 0};
    SetScrollInfo(g.chat, SB_VERT, &info, TRUE);
    ShowScrollBar(g.chat, SB_VERT, FALSE);
    g.chat_width = width;
}

void paint_chat(HWND hwnd) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    RECT client{};
    GetClientRect(hwnd, &client);
    int width = client.right;
    int height = client.bottom;
    HDC memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, std::max(1, width), std::max(1, height));
    HGDIOBJ old = SelectObject(memory, bitmap);
    Graphics graphics(memory);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    if (width != g.chat_width) relayout_chat(graphics, width, height);
    graphics.Clear(Color(255, 24, 24, 26));
    FontFamily& family = ui_family();
    if (g.messages.empty() && !g.busy) {
        Font title(&family, static_cast<REAL>(S(28)), FontStyleRegular, UnitPixel);
        Font font(&family, static_cast<REAL>(S(15)), FontStyleRegular, UnitPixel);
        SolidBrush title_brush(Color(255, 236, 236, 238));
        SolidBrush brush(Color(255, 140, 140, 148));
        StringFormat format;
        format.SetAlignment(StringAlignmentCenter);
        float mid = static_cast<float>(height) * 0.38f;
        graphics.DrawString(L"Valhalla", -1, &title, RectF(0, mid, static_cast<REAL>(width), static_cast<REAL>(S(40))), &format, &title_brush);
        graphics.DrawString(Tr(L"empty_chat"), -1, &font, RectF(0, mid + S(44), static_cast<REAL>(width), static_cast<REAL>(S(28))), &format, &brush);
    }
    Font body_font(&family, static_cast<REAL>(S(16)), FontStyleRegular, UnitPixel);
    Font name_font(&family, static_cast<REAL>(S(12)), FontStyleRegular, UnitPixel);
    StringFormat format;
    format.SetTrimming(StringTrimmingWord);
    for (const auto& bubble : g.bubbles) {
        float top = bubble.y - g.scroll;
        if (top > height || top + bubble.h < 0) continue;
        if (bubble.user) {
            COLORREF fill = g.accent;
            fill = RGB((GetRValue(fill) * 42 + 28) / 100, (GetGValue(fill) * 42 + 30) / 100, (GetBValue(fill) * 42 + 36) / 100);
            GraphicsPath path;
            add_round(path, RectF(bubble.x, top, bubble.w, bubble.h), static_cast<float>(S(18)));
            SolidBrush brush(Color(255, GetRValue(fill), GetGValue(fill), GetBValue(fill)));
            graphics.FillPath(&brush, &path);
            SolidBrush text(Color(255, 248, 248, 248));
            RectF text_rect(bubble.x + S(14), top + S(8), bubble.w - S(28), bubble.h - S(12));
            graphics.DrawString(bubble.text.c_str(), -1, &body_font, text_rect, &format, &text);
        } else {
            bool pending = g.busy && &bubble == &g.bubbles.back() && bubble.text == Tr(L"thinking");
            SolidBrush name(Color(255, 168, 168, 176));
            graphics.DrawString(bubble.who.c_str(), -1, &name_font, RectF(bubble.x, top, bubble.w, static_cast<REAL>(S(18))), &format, &name);
            SolidBrush text(pending ? Color(255, 150, 150, 158) : Color(255, 232, 232, 234));
            RectF text_rect(bubble.x, top + S(22), bubble.w, bubble.h - S(24));
            graphics.DrawString(bubble.text.c_str(), -1, &body_font, text_rect, &format, &text);
        }
    }
    BitBlt(dc, 0, 0, width, height, memory, 0, 0, SRCCOPY);
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    EndPaint(hwnd, &paint);
}

void scroll_chat(int delta) {
    RECT client{};
    GetClientRect(g.chat, &client);
    int max_scroll = std::max(0, g.content_h - static_cast<int>(client.bottom));
    int next = g.scroll + delta;
    if (next < 0) next = 0;
    if (next > max_scroll) next = max_scroll;
    g.scroll = next;
    g.stick = next >= max_scroll - 2;
    SCROLLINFO info{sizeof(info), SIF_POS, 0, 0, 0, g.scroll, 0};
    SetScrollInfo(g.chat, SB_VERT, &info, TRUE);
    ShowScrollBar(g.chat, SB_VERT, FALSE);
    InvalidateRect(g.chat, nullptr, FALSE);
}

LRESULT CALLBACK ChatProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
        case WM_PAINT:
            paint_chat(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            g.chat_width = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_MOUSEWHEEL:
            scroll_chat(-GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * S(48));
            return 0;
        case WM_VSCROLL: {
            RECT client{};
            GetClientRect(hwnd, &client);
            int step = S(32);
            switch (LOWORD(wp)) {
                case SB_LINEUP: scroll_chat(-step); break;
                case SB_LINEDOWN: scroll_chat(step); break;
                case SB_PAGEUP: scroll_chat(-client.bottom); break;
                case SB_PAGEDOWN: scroll_chat(client.bottom); break;
                case SB_THUMBTRACK: {
                    SCROLLINFO info{sizeof(info), SIF_TRACKPOS};
                    GetScrollInfo(hwnd, SB_VERT, &info);
                    int max_scroll = std::max(0, g.content_h - static_cast<int>(client.bottom));
                    g.scroll = std::min(max_scroll, static_cast<int>(info.nTrackPos));
                    g.stick = g.scroll >= max_scroll - 2;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    break;
                }
                default: break;
            }
            return 0;
        }
        default: break;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}

LRESULT CALLBACK ButtonProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_ERASEBKGND) {
        RECT rect{};
        GetClientRect(hwnd, &rect);
        FillRect(reinterpret_cast<HDC>(wp), &rect, g.bg_brush);
        return 1;
    }
    return CallWindowProcW(g.button_proc, hwnd, message, wp, lp);
}

LRESULT CALLBACK ComposerProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    if ((message == WM_KEYDOWN && wp == VK_RETURN) || (message == WM_CHAR && wp == L'\r')) {
        if (GetKeyState(VK_SHIFT) & 0x8000) return CallWindowProcW(g.edit_proc, hwnd, message, wp, lp);
        if (message == WM_KEYDOWN) send_chat();
        return 0;
    }
    return CallWindowProcW(g.edit_proc, hwnd, message, wp, lp);
}

void paint_button(const DRAWITEMSTRUCT* item) {
    bool chip = item->CtlID == ID_SCREEN || item->CtlID == ID_CONTROL || item->CtlID == ID_TASK;
    bool quiet = item->CtlID == ID_NEW || item->CtlID == ID_SETTINGS || item->CtlID == ID_BACK;
    COLORREF fill = chip ? RGB(39, 39, 44) : quiet ? RGB(32, 32, 36) : RGB(46, 46, 52);
    bool selected = g.page == 1 && item->CtlID >= ID_NAV0 && item->CtlID <= ID_NAV2 && item->CtlID - ID_NAV0 == g.section;
    bool armed = item->CtlID == ID_SEND || item->CtlID == ID_PROBE || (item->CtlID == ID_CONTROL && g.settings.allow_control) ||
                 (item->CtlID == ID_TASK && g.task_mode);
    if (armed) fill = g.accent;
    else if (selected) fill = RGB(62, 62, 70);
    if (item->itemState & ODS_DISABLED) fill = RGB(38, 38, 42);
    RECT rect = item->rcItem;
    Graphics graphics(item->hDC);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(PixelOffsetModeHalf);
    GraphicsPath path;
    float radius = item->CtlID == ID_SEND ? static_cast<float>(rect.bottom) / 2.f : static_cast<float>(S(chip ? 10 : 12));
    add_round(path, RectF(0.5f, 0.5f, static_cast<REAL>(rect.right - 1), static_cast<REAL>(rect.bottom - 1)), radius);
    SolidBrush brush(Color(255, GetRValue(fill), GetGValue(fill), GetBValue(fill)));
    graphics.FillPath(&brush, &path);
    wchar_t text[128];
    GetWindowTextW(item->hwndItem, text, 128);
    FontFamily& family = ui_family();
    int control = item->CtlID;
    bool chip_font = control == ID_SCREEN || control == ID_CONTROL || control == ID_TASK || control == ID_NEW || control == ID_SETTINGS;
    Font font(&family, static_cast<REAL>(chip_font ? S(12) : S(14)), FontStyleRegular, UnitPixel);
    SolidBrush white(Color(255, 255, 255, 255));
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);
    graphics.DrawString(text, -1, &font, RectF(0, 0, static_cast<REAL>(rect.right), static_cast<REAL>(rect.bottom)), &format, &white);
}

HWND add_button(HWND parent, int id) {
    HWND hwnd = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | BS_OWNERDRAW, 0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                g.instance, nullptr);
    if (!g.button_proc) g.button_proc = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ButtonProc));
    return hwnd;
}

HWND add_label(HWND parent, bool wrap) {
    DWORD style = WS_CHILD | SS_LEFT | SS_NOPREFIX;
    if (wrap) style |= SS_EDITCONTROL;
    return CreateWindowExW(0, L"STATIC", L"", style, 0, 0, 10, 10, parent, nullptr, g.instance, nullptr);
}

HWND add_edit(HWND parent, bool composer) {
    DWORD style = WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN;
    if (!composer) style |= WS_VSCROLL;
    HWND hwnd = CreateWindowExW(0, L"EDIT", L"", style, 0, 0, 10, 10,
                                parent, nullptr, g.instance, nullptr);
    if (composer) {
        LONG frame = GetWindowLongW(hwnd, GWL_STYLE);
        LONG extra = GetWindowLongW(hwnd, GWL_EXSTYLE);
        SetWindowLongW(hwnd, GWL_STYLE, frame & ~WS_BORDER);
        SetWindowLongW(hwnd, GWL_EXSTYLE, extra & ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
    if (composer) {
        g.edit_proc = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ComposerProc));
    }
    return hwnd;
}

void paint_shell(HWND hwnd) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    RECT client{};
    GetClientRect(hwnd, &client);
    FillRect(dc, &client, g.bg_brush);
    Graphics graphics(dc);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(PixelOffsetModeHalf);
    if (g.page == 0 && g.composer && g.send) {
        RECT edit{};
        RECT send{};
        GetWindowRect(g.composer, &edit);
        GetWindowRect(g.send, &send);
        MapWindowPoints(HWND_DESKTOP, hwnd, reinterpret_cast<POINT*>(&edit), 2);
        MapWindowPoints(HWND_DESKTOP, hwnd, reinterpret_cast<POINT*>(&send), 2);
        int left = std::min(edit.left, send.left) - S(14);
        int top = edit.top - S(16);
        int right = std::max(edit.right, send.right) + S(14);
        int bottom = std::max(edit.bottom, send.bottom) + S(12);
        RectF well(static_cast<REAL>(left), static_cast<REAL>(top), static_cast<REAL>(right - left), static_cast<REAL>(bottom - top));
        for (int layer = 3; layer >= 1; --layer) {
            RectF shadow = well;
            shadow.Inflate(static_cast<REAL>(layer * 2), static_cast<REAL>(layer * 2));
            GraphicsPath shade;
            add_round(shade, shadow, static_cast<float>(S(18) + layer * 2));
            SolidBrush shade_brush(Color(static_cast<BYTE>(18 * layer), 0, 0, 0));
            graphics.FillPath(&shade_brush, &shade);
        }
        GraphicsPath path;
        add_round(path, well, static_cast<float>(S(18)));
        SolidBrush fill(Color(255, 34, 34, 38));
        Pen edge(Color(180, GetRValue(g.accent), GetGValue(g.accent), GetBValue(g.accent)), 1.f);
        graphics.FillPath(&fill, &path);
        graphics.DrawPath(&edge, &path);
    } else if (g.page == 1) {
        RectF rail(static_cast<REAL>(S(12)), static_cast<REAL>(S(68)), static_cast<REAL>(S(196)),
                   static_cast<REAL>(std::max(10, static_cast<int>(client.bottom) - S(88))));
        GraphicsPath path;
        add_round(path, rail, static_cast<float>(S(14)));
        SolidBrush fill(Color(255, 32, 32, 36));
        graphics.FillPath(&fill, &path);
    }
    EndPaint(hwnd, &paint);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
        case WM_CREATE: {
            g.hwnd = hwnd;
            enable_dark(hwnd);
            g.title = add_label(hwnd, false);
            g.hint = add_label(hwnd, false);
            g.new_chat = add_button(hwnd, ID_NEW);
            g.settings_button = add_button(hwnd, ID_SETTINGS);
            g.chat = CreateWindowExW(0, L"ValhallaChat", L"", WS_CHILD, 0, 0, 10, 10, hwnd, nullptr, g.instance, nullptr);
            g.composer = add_edit(hwnd, true);
            g.screen_button = add_button(hwnd, ID_SCREEN);
            g.control_button = add_button(hwnd, ID_CONTROL);
            g.task_button = add_button(hwnd, ID_TASK);
            g.send = add_button(hwnd, ID_SEND);
            g.status = add_label(hwnd, false);
            g.back = add_button(hwnd, ID_BACK);
            g.header = add_label(hwnd, false);
            for (int i = 0; i < 3; ++i) g.nav[i] = add_button(hwnd, ID_NAV0 + i);
            g.knowledge_title = add_label(hwnd, false);
            g.knowledge_help = add_label(hwnd, true);
            g.knowledge = add_edit(hwnd, false);
            g.models_title = add_label(hwnd, false);
            g.models_help = add_label(hwnd, true);
            for (int i = 0; i < 4; ++i) {
                g.model_title[i] = add_label(hwnd, false);
                g.model_note[i] = add_label(hwnd, true);
                g.model_button[i] = add_button(hwnd, ID_MODEL + i);
            }
            g.general_title = add_label(hwnd, false);
            g.language_label = add_label(hwnd, false);
            g.language = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 10, 200, hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LANG)), g.instance, nullptr);
            g.strategy_label = add_label(hwnd, false);
            g.strategy = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 10, 200, hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STRATEGY)), g.instance, nullptr);
            g.strategy_detail = add_label(hwnd, false);
            g.session_check = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | BS_AUTOCHECKBOX, 0, 0, 10, 10, hwnd,
                                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SESSION)), g.instance, nullptr);
            g.session_help = add_label(hwnd, true);
            g.runtime = add_label(hwnd, true);
            g.probe_button = add_button(hwnd, ID_PROBE);
            SetWindowTheme(g.language, L"DarkMode_CFD", nullptr);
            SetWindowTheme(g.strategy, L"DarkMode_CFD", nullptr);
            SetWindowTheme(g.session_check, L"DarkMode_Explorer", nullptr);
            SetWindowTextW(g.knowledge, to_edit(g.settings.knowledge).c_str());
            if (g.settings.keep_session) SendMessageW(g.session_check, BM_SETCHECK, BST_CHECKED, 0);
            apply_fonts();
            sync_text();
            restore_transcript();
            apply_page();
            start_framework();
            return 0;
        }
        case WM_SIZE:
            layout();
            return 0;
        case WM_DPICHANGED: {
            RECT* rect = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
            apply_fonts();
            layout();
            return 0;
        }
        case WM_PAINT:
            paint_shell(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, RGB(255, 255, 255));
            bool card = reinterpret_cast<HWND>(lp) == g.composer || reinterpret_cast<HWND>(lp) == g.knowledge;
            if (reinterpret_cast<HWND>(lp) == g.status) SetTextColor(dc, RGB(150, 150, 158));
            if (card) {
                SetBkColor(dc, RGB(34, 34, 38));
                return reinterpret_cast<LRESULT>(g.card_brush);
            }
            if (g.mica) {
                SetBkMode(dc, TRANSPARENT);
                return reinterpret_cast<LRESULT>(GetStockObject(HOLLOW_BRUSH));
            }
            SetBkColor(dc, RGB(24, 24, 26));
            return reinterpret_cast<LRESULT>(g.bg_brush);
        }
        case WM_DRAWITEM:
            paint_button(reinterpret_cast<DRAWITEMSTRUCT*>(lp));
            return TRUE;
        case WM_COMMAND: {
            int id = LOWORD(wp);
            int code = HIWORD(wp);
            if (code == BN_CLICKED) {
                if (id == ID_SEND) send_chat();
                else if (id == ID_SCREEN) {
                    g.screen_context = wide_to_utf8(read_other_screen());
                    SetWindowTextW(g.status, Tr(L"screen_done"));
                } else if (id == ID_CONTROL) {
                    g.settings.allow_control = !g.settings.allow_control;
                    save_now();
                    SetWindowTextW(g.control_button, g.settings.allow_control ? Tr(L"control_on") : Tr(L"control"));
                    InvalidateRect(g.control_button, nullptr, TRUE);
                }                 else if (id == ID_TASK) {
                    g.task_mode = !g.task_mode;
                    SetWindowTextW(g.task_button, g.task_mode ? Tr(L"task_on") : Tr(L"task"));
                    InvalidateRect(g.task_button, nullptr, TRUE);
                } else if (id == ID_PROBE) {
                    if (!g.busy) {
                        g.busy = true;
                        EnableWindow(g.probe_button, FALSE);
                        g.probe_text = Tr(L"probing");
                        sync_text();
                        bool zh = g.lang == L"zh";
                        HWND owner = hwnd;
                        std::thread([owner, zh]() {
                            auto* report = new FeatureReport(run_feature_probe(zh));
                            if (!PostMessageW(owner, WM_PROBE_DONE, 0, reinterpret_cast<LPARAM>(report))) delete report;
                        }).detach();
                    }
                } else if (id == ID_NEW) clear_chat();
                else if (id == ID_SETTINGS) go_settings();
                else if (id == ID_BACK) go_chat();
                else if (id >= ID_NAV0 && id <= ID_NAV2) remember_knowledge_and(id - ID_NAV0);
                else if (id == ID_SESSION) {
                    g.settings.keep_session = SendMessageW(g.session_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
                    save_now();
                    if (!g.settings.keep_session) g.session_json.clear();
                } else if (id >= ID_MODEL && id < ID_MODEL + 4) {
                    model_click(id - ID_MODEL);
                }
            } else if (code == CBN_SELCHANGE && !g.filling) {
                if (id == ID_LANG) {
                    int selected = static_cast<int>(SendMessageW(g.language, CB_GETCURSEL, 0, 0));
                    g.settings.language = selected == 1 ? "zh" : selected == 2 ? "en" : "system";
                    save_now();
                    sync_text();
                } else if (id == ID_STRATEGY) {
                    int selected = static_cast<int>(SendMessageW(g.strategy, CB_GETCURSEL, 0, 0));
                    g.settings.strategy = strategy_id(selected);
                    if (g.settings.strategy == "followup") {
                        g.settings.keep_session = true;
                        SendMessageW(g.session_check, BM_SETCHECK, BST_CHECKED, 0);
                    }
                    save_now();
                    g.session_json.clear();
                    SetWindowTextW(g.strategy_detail, Tr(strategy_delta_key(selected)));
                    SetWindowTextW(g.hint, hint_line().c_str());
                }
            }
            return 0;
        }
        case WM_CHAT_DONE:
            finish_chat(reinterpret_cast<ChatDone*>(lp));
            return 0;
        case WM_MODEL_NOTE: {
            auto* note = reinterpret_cast<ModelNote*>(lp);
            if (note->index >= 0 && note->index < 4 && !note->text.empty()) SetWindowTextW(g.model_note[note->index], note->text.c_str());
            delete note;
            return 0;
        }
        case WM_PROBE_DONE: {
            auto* report = reinterpret_cast<FeatureReport*>(lp);
            g.busy = false;
            EnableWindow(g.probe_button, TRUE);
            g.probe_text = report->text;
            delete report;
            sync_text();
            return 0;
        }
        case WM_FRAMEWORK: {
            auto* note = reinterpret_cast<std::wstring*>(lp);
            g.framework_note = *note;
            delete note;
            if (!g.busy) SetWindowTextW(g.status, g.framework_note.c_str());
            return 0;
        }
        case WM_MODEL_DONE: {
            auto* done = reinterpret_cast<ModelDone*>(lp);
            g.busy = false;
            if (done->ok) {
                g.settings.model_repo = kModels[done->index].repo;
                save_now();
                g.session_json.clear();
                stop_warm_process();
                start_framework();
            } else {
                std::wstring text = done->error.empty() ? Tr(L"failed") : done->error;
                SetWindowTextW(g.model_note[done->index], text.c_str());
            }
            refresh_models();
            delete done;
            return 0;
        }
        case WM_GETMINMAXINFO: {
            MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lp);
            info->ptMinTrackSize.x = S(900);
            info->ptMinTrackSize.y = S(620);
            return 0;
        }
        case WM_DESTROY:
            stop_warm_process();
            PostQuitMessage(0);
            return 0;
        default: break;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}

std::wstring exe_dir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    PathRemoveFileSpecW(path);
    return path;
}

int run_check() {
    std::wstring dir = exe_dir();
    Settings settings = load_settings(dir + L"\\settings.json");
    TurnResult result = run_chat(settings.knowledge, "用一句话说明 ValhallaBase 的解码模式。", "open", model_directory(settings.model_repo), "", {}, "", false);
    std::string body = result.error.empty() ? result.answer + "\n" + result.decode_path : result.error;
    HANDLE file = CreateFileW((dir + L"\\native-check.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD wrote = 0;
        WriteFile(file, body.data(), static_cast<DWORD>(body.size()), &wrote, nullptr);
        CloseHandle(file);
    }
    return result.error.empty() ? 0 : 1;
}

int run_memory_check() {
    std::wstring dir = exe_dir();
    Settings settings = load_settings(dir + L"\\settings.json");
    std::wstring model = model_directory(settings.model_repo);
    TurnResult first = run_chat(settings.knowledge, "请记住这个口令：青鸟。之后只在我问起时再说出来。", "open", model, "", {}, "", false);
    std::vector<std::pair<std::string, std::string>> history;
    if (first.error.empty()) history.emplace_back("请记住这个口令：青鸟。之后只在我问起时再说出来。", first.answer);
    TurnResult second = run_chat(settings.knowledge, "我刚才让你记住的口令是什么？", "open", model, first.session_json, history, "", false);
    std::string body = first.error.empty() ? first.answer : first.error;
    body += "\n---\n";
    body += second.error.empty() ? second.answer : second.error;
    HANDLE file = CreateFileW((dir + L"\\native-check.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD wrote = 0;
        WriteFile(file, body.data(), static_cast<DWORD>(body.size()), &wrote, nullptr);
        CloseHandle(file);
    }
    bool remembered = second.answer.find("青鸟") != std::string::npos;
    return first.error.empty() && second.error.empty() && remembered ? 0 : 1;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool check = false;
    bool memory_check = false;
    bool feature_check = false;
    for (int i = 0; i < argc; ++i) {
        if (wcscmp(argv[i], L"--check") == 0) check = true;
        if (wcscmp(argv[i], L"--memory-check") == 0) memory_check = true;
        if (wcscmp(argv[i], L"--feature-check") == 0) feature_check = true;
    }
    if (argv) LocalFree(argv);
    if (feature_check) {
        bool zh = (GetUserDefaultUILanguage() & 0x3FF) == 0x04;
        FeatureReport report = run_feature_probe(zh);
        std::wstring dir = exe_dir();
        std::string body = wide_to_utf8(report.text);
        HANDLE file = CreateFileW((dir + L"\\native-check.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD wrote = 0;
            WriteFile(file, body.data(), static_cast<DWORD>(body.size()), &wrote, nullptr);
            CloseHandle(file);
        }
        return report.ok ? 0 : 1;
    }
    if (memory_check) return run_memory_check();
    if (check) return run_check();

    g.instance = instance;
    g.settings_path = exe_dir() + L"\\settings.json";
    g.transcript_path = exe_dir() + L"\\conversation.json";
    g.settings = load_settings(g.settings_path);
    g.lang = resolved_language(g.settings.language);
    read_accent();
    g.bg_brush = CreateSolidBrush(RGB(24, 24, 26));
    g.card_brush = CreateSolidBrush(RGB(34, 34, 38));
    HMODULE theme = LoadLibraryW(L"uxtheme.dll");
    auto preferred = reinterpret_cast<int(WINAPI*)(int)>(GetProcAddress(theme, MAKEINTRESOURCEA(135)));
    if (preferred) preferred(2);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    GdiplusStartupInput input;
    GdiplusStartup(&g.gdi, &input, nullptr);

    WNDCLASSW chat{};
    chat.lpfnWndProc = ChatProc;
    chat.hInstance = instance;
    chat.lpszClassName = L"ValhallaChat";
    chat.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    chat.hbrBackground = g.bg_brush;
    RegisterClassW(&chat);

    WNDCLASSW window{};
    window.lpfnWndProc = WndProc;
    window.hInstance = instance;
    window.lpszClassName = L"ValhallaWindow";
    window.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window.hbrBackground = g.bg_brush;
    window.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassW(&window);

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int dpi = static_cast<int>(GetDpiForSystem());
    int width = MulDiv(1180, dpi, 96);
    int height = MulDiv(820, dpi, 96);
    int work_w = std::max(640, static_cast<int>(work.right - work.left) - 48);
    int work_h = std::max(480, static_cast<int>(work.bottom - work.top) - 48);
    width = std::min(width, work_w);
    height = std::min(height, work_h);
    int x = work.left + std::max(0, (static_cast<int>(work.right - work.left) - width) / 2);
    int y = work.top + std::max(0, (static_cast<int>(work.bottom - work.top) - height) / 2);
    HWND hwnd = CreateWindowExW(0, L"ValhallaWindow", L"Valhalla", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, width, height,
                                nullptr, nullptr, instance, nullptr);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    GdiplusShutdown(g.gdi);
    return static_cast<int>(msg.wParam);
}
