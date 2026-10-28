#include "desktop.h"
#include "engine.h"
#include "knowledge.h"
#include "sessions.h"
#include "utf.h"

#include <commdlg.h>
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
#pragma comment(lib, "comdlg32.lib")
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
    ID_IMPORT = 109,
    ID_NAV0 = 110,
    ID_NAV1 = 111,
    ID_NAV2 = 112,
    ID_NAV3 = 113,
    ID_RECOGNIZE = 114,
    ID_ARM = 115,
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
    HWND nav[4] = {};
    HWND knowledge_title = nullptr;
    HWND knowledge_help = nullptr;
    HWND import_button = nullptr;
    HWND knowledge = nullptr;
    KnowledgeBase kb;
    std::wstring kb_path;
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
    HWND rail = nullptr;
    HWND desktop_title = nullptr;
    HWND desktop_help = nullptr;
    HWND recognize_button = nullptr;
    HWND arm_button = nullptr;
    HWND desktop_view = nullptr;
    ConversationLibrary library;
    ScreenReading reading;
    int rail_scroll = 0;
    bool layout_check = false;
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
        {L"screen", L"识别屏幕", L"Recognize"},
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
        {L"need_knowledge", L"知识库是空的。打开设置，写上语料，或导入行业资料。", L"The knowledge base is empty. Add notes under Settings, or import industry files."},
        {L"need_model", L"这个模型还没有权重。到设置的「模型」页下载。", L"That model has no weights yet. Download it under Settings, Models."},
        {L"nav_knowledge", L"知识库", L"Knowledge"},
        {L"nav_models", L"模型", L"Models"},
        {L"nav_general", L"综合", L"General"},
        {L"nav_desktop", L"桌面", L"Desktop"},
        {L"recognizing", L"正在识别屏幕…", L"Reading the screen…"},
        {L"rail_chats", L"对话", L"Chats"},
        {L"rail_screen", L"屏幕识别", L"Screen"},
        {L"rail_ocr", L"OCR", L"OCR"},
        {L"rail_control", L"计算机控制", L"Computer control"},
        {L"rail_off", L"关", L"Off"},
        {L"rail_on", L"开", L"On"},
        {L"rail_none", L"未识别", L"Not read"},
        {L"desktop_title", L"屏幕与控制", L"Screen and control"},
        {L"desktop_help", L"识别会暂时让出窗口，截取当前桌面，用 Windows OCR 读出文字，并列出控件坐标。计算机控制默认关闭，打开后才会执行点击、输入和按键。", L"Recognize steps aside, captures the desktop, reads it with Windows OCR, and lists control coordinates. Computer control stays off until you turn it on."},
        {L"arm_on", L"控制已打开", L"Control is on"},
        {L"arm_off", L"允许控制电脑", L"Allow computer control"},
        {L"delete_chat", L"删除这则对话？", L"Delete this conversation?"},
        {L"delete_title", L"对话", L"Conversation"},
        {L"knowledge_title", L"知识库", L"Knowledge"},
        {L"knowledge_help", L"手写语料一行一条。行业资料用下方按钮导入，支持 txt、md、csv、json、html、xml、rtf、docx、xlsx、pptx、pdf。导入内容标记为待核对。", L"Notes stay one fact per line. Import industry files below: txt, md, csv, json, html, xml, rtf, docx, xlsx, pptx, and pdf. Imported material stays pending review."},
        {L"import_btn", L"导入行业资料", L"Import industry files"},
        {L"import_title", L"行业名称", L"Industry name"},
        {L"import_hint", L"这一批资料属于哪个行业？", L"Which industry are these files from?"},
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
    if (!g.library.active.empty()) {
        std::wstring title = Tr(L"new_chat");
        for (const auto& line : g.messages) {
            std::wstring text = trim_copy(line.text);
            if (!line.user || text.empty()) continue;
            auto cut = text.find(L'\n');
            if (cut != std::wstring::npos) text.resize(cut);
            if (text.size() > 32) text.resize(32);
            title = text;
            break;
        }
        note_conversation(g.library, g.library.active, wide_to_utf8(title));
        if (g.rail) InvalidateRect(g.rail, nullptr, FALSE);
    }
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

struct IndustryPrompt {
    HWND edit = nullptr;
    std::wstring value;
    bool accepted = false;
};

LRESULT CALLBACK IndustryProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    auto* state = reinterpret_cast<IndustryPrompt*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (message) {
        case WM_CREATE: {
            auto* created = reinterpret_cast<CREATESTRUCTW*>(lp);
            state = reinterpret_cast<IndustryPrompt*>(created->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            CreateWindowExW(0, L"STATIC", Tr(L"import_hint"), WS_CHILD | WS_VISIBLE, 16, 14, 360, 22, hwnd, nullptr, g.instance, nullptr);
            state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 16, 42, 360, 26, hwnd,
                                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(1)), g.instance, nullptr);
            CreateWindowExW(0, L"BUTTON", Tr(L"import_btn"), WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 196, 82, 180, 30, hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), g.instance, nullptr);
            return 0;
        }
        case WM_COMMAND:
            if (!state) break;
            if (LOWORD(wp) == IDOK) {
                state->value = trim_copy(window_text(state->edit));
                state->accepted = true;
                DestroyWindow(hwnd);
            } else if (LOWORD(wp) == IDCANCEL) {
                DestroyWindow(hwnd);
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}

bool ask_industry(HWND owner, IndustryPrompt& prompt) {
    WNDCLASSW window{};
    window.lpfnWndProc = IndustryProc;
    window.hInstance = g.instance;
    window.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window.lpszClassName = L"ValhallaIndustry";
    RegisterClassW(&window);
    RECT parent{};
    GetWindowRect(owner, &parent);
    int width = 408;
    int height = 168;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ValhallaIndustry", Tr(L"import_title"), WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                parent.left + 80, parent.top + 80, width, height, owner, nullptr, g.instance, &prompt);
    if (!dlg) return false;
    EnableWindow(owner, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetFocus(prompt.edit);
    while (IsWindow(dlg)) {
        MSG msg;
        if (!GetMessageW(&msg, nullptr, 0, 0)) {
            PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        if (msg.message == WM_KEYDOWN && (msg.hwnd == prompt.edit || msg.hwnd == dlg)) {
            if (msg.wParam == VK_RETURN) {
                SendMessageW(dlg, WM_COMMAND, IDOK, 0);
                continue;
            }
            if (msg.wParam == VK_ESCAPE) {
                DestroyWindow(dlg);
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    return prompt.accepted;
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
    sync_notes(g.kb, text);
    if (!g.kb_path.empty()) save_knowledge(g.kb_path, g.kb);
}

void import_industry_dialog(HWND owner) {
    IndustryPrompt prompt;
    if (!ask_industry(owner, prompt)) return;
    std::wstring domain = prompt.value.empty() ? L"industry" : prompt.value;
    std::vector<wchar_t> buffer(64 * 1024, L'\0');
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Industry files\0*.txt;*.md;*.csv;*.tsv;*.json;*.jsonl;*.html;*.htm;*.xml;*.rtf;*.log;*.docx;*.xlsx;*.pptx;*.pdf\0All files\0*.*\0";
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = Tr(L"import_btn");
    if (!GetOpenFileNameW(&ofn)) return;

    std::vector<std::wstring> files;
    std::wstring directory = buffer.data();
    wchar_t* cursor = buffer.data() + directory.size() + 1;
    if (*cursor == L'\0') files.push_back(directory);
    else {
        while (*cursor) {
            files.push_back(directory + L"\\" + cursor);
            cursor += wcslen(cursor) + 1;
        }
    }
    pull_knowledge();
    ImportReport total;
    std::string domain_utf8 = wide_to_utf8(domain);
    for (const auto& file : files) {
        ImportReport one = import_industry_file(g.kb, file, domain_utf8);
        total.added += one.added;
        total.recognized += one.recognized;
        total.failed += one.failed;
    }
    save_knowledge(g.kb_path, g.kb);
    g.session_json.clear();
    sync_text();
    wchar_t message[320];
    swprintf_s(message,
               g.lang == L"zh" ? L"导入完成：新增 %d 条，库中已有 %d 条，%d 个文件没有读出。"
                               : L"Import finished: %d added, %d already stored, %d files unread.",
               total.added, total.recognized, total.failed);
    MessageBoxW(owner, message, Tr(L"import_btn"), MB_OK);
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
    SetWindowTextW(g.nav[3], Tr(L"nav_desktop"));
    SetWindowTextW(g.knowledge_title, Tr(L"knowledge_title"));
    {
        std::wstring help = Tr(L"knowledge_help");
        wchar_t extra[160];
        swprintf_s(extra, g.lang == L"zh" ? L" 已导入 %d 个行业、%d 条。" : L" Imported: %d industries, %d records.", industry_domains(g.kb),
                   industry_records(g.kb));
        help += extra;
        SetWindowTextW(g.knowledge_help, help.c_str());
    }
    SetWindowTextW(g.import_button, Tr(L"import_btn"));
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
    SetWindowTextW(g.desktop_title, Tr(L"desktop_title"));
    SetWindowTextW(g.desktop_help, Tr(L"desktop_help"));
    SetWindowTextW(g.recognize_button, Tr(L"screen"));
    SetWindowTextW(g.arm_button, g.settings.allow_control ? Tr(L"arm_on") : Tr(L"arm_off"));
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
                            g.language, g.strategy_label, g.strategy, g.session_check, g.probe_button, g.import_button,
                            g.desktop_title, g.recognize_button, g.arm_button};
    for (HWND hwnd : body_controls)
        if (hwnd) SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(body), TRUE);
    HWND small_controls[] = {g.hint, g.status, g.knowledge_help, g.models_help, g.strategy_detail, g.session_help, g.runtime, g.desktop_help};
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

int rail_width() {
    RECT client{};
    if (g.hwnd) GetClientRect(g.hwnd, &client);
    if (client.right > 0 && client.right < S(980)) return S(208);
    return S(248);
}

void layout() {
    RECT client{};
    GetClientRect(g.hwnd, &client);
    int width = client.right;
    int height = client.bottom;
    int rail = g.page == 0 ? rail_width() : 0;
    if (g.rail) {
        if (g.page == 0) MoveWindow(g.rail, 0, 0, rail, std::max(1, height), TRUE);
        else MoveWindow(g.rail, 0, 0, 0, 0, TRUE);
    }
    int margin = S(20);
    int gap = S(8);
    int area_w = std::max(10, width - rail);
    int column = std::min(S(760), std::max(S(280), area_w - margin * 2));
    int origin = rail + std::max(margin, (area_w - column) / 2);
    int settings_w = S(72);
    int new_w = S(84);
    int settings_x = std::max(origin, width - margin - settings_w);
    int new_x = std::max(origin, settings_x - gap - new_w);
    int title_w = std::max(0, new_x - gap - origin);
    MoveWindow(g.title, origin, S(14), title_w, S(28), TRUE);
    MoveWindow(g.hint, 0, 0, 0, 0, TRUE);
    MoveWindow(g.new_chat, new_x, S(12), new_w, S(32), TRUE);
    MoveWindow(g.settings_button, settings_x, S(12), settings_w, S(32), TRUE);
    int chat_top = S(56);
    int well_h = S(140);
    int status_h = S(20);
    int composer_top = height - well_h - S(36);
    if (composer_top < chat_top + S(80)) composer_top = chat_top + S(80);
    MoveWindow(g.status, origin, composer_top - status_h - S(6), column, status_h, TRUE);
    MoveWindow(g.chat, rail, chat_top, std::max(10, width - rail), std::max(10, composer_top - status_h - chat_top - S(10)), TRUE);
    int pad = S(16);
    int tool_h = S(32);
    int tool_y = composer_top + well_h - pad - tool_h;
    int edit_h = std::max(S(40), tool_y - composer_top - S(14));
    MoveWindow(g.composer, origin + pad, composer_top + S(14), std::max(10, column - pad * 2), edit_h, TRUE);
    SendMessageW(g.composer, 0x00D3, 3, MAKELPARAM(S(2), S(2)));
    int send_w = S(76);
    int send_x = origin + column - pad - send_w;
    int chip_gaps = gap * 2;
    int chip_room = send_x - gap - (origin + pad) - chip_gaps;
    int chip_w = chip_room / 3;
    if (chip_w > S(112)) chip_w = S(112);
    if (chip_w < S(48)) chip_w = S(48);
    int x = origin + pad;
    MoveWindow(g.screen_button, x, tool_y, chip_w, tool_h, TRUE);
    x += chip_w + gap;
    MoveWindow(g.control_button, x, tool_y, chip_w, tool_h, TRUE);
    x += chip_w + gap;
    MoveWindow(g.task_button, x, tool_y, chip_w, tool_h, TRUE);
    MoveWindow(g.send, send_x, tool_y, send_w, tool_h, TRUE);

    MoveWindow(g.back, S(20), S(16), S(88), S(36), TRUE);
    MoveWindow(g.header, S(124), S(16), S(320), S(36), TRUE);
    for (int i = 0; i < 4; ++i) MoveWindow(g.nav[i], S(20), S(76) + i * S(44), S(180), S(36), TRUE);
    int left = S(224);
    int content_w = std::max(10, width - left - S(24));
    MoveWindow(g.knowledge_title, left, S(76), content_w, S(28), TRUE);
    MoveWindow(g.knowledge_help, left, S(108), content_w, S(64), TRUE);
    MoveWindow(g.import_button, left, S(178), S(220), S(36), TRUE);
    MoveWindow(g.knowledge, left, S(224), content_w, std::max(10, height - S(248)), TRUE);
    MoveWindow(g.models_title, left, S(76), content_w, S(28), TRUE);
    MoveWindow(g.models_help, left, S(108), content_w, S(40), TRUE);
    int row_h = S(92);
    int models_room = height - S(176);
    if (models_room < 4 * row_h) row_h = std::max(S(68), models_room / 4);
    for (int i = 0; i < 4; ++i) {
        int top = S(160) + i * row_h;
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
    MoveWindow(g.desktop_title, left, S(76), content_w, S(28), TRUE);
    MoveWindow(g.desktop_help, left, S(108), content_w, S(56), TRUE);
    MoveWindow(g.recognize_button, left, S(172), S(140), S(36), TRUE);
    MoveWindow(g.arm_button, left + S(152), S(172), S(180), S(36), TRUE);
    MoveWindow(g.desktop_view, left, S(220), content_w, std::max(10, height - S(244)), TRUE);
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
    show_item(g.rail, chat);
    show_item(g.back, !chat);
    show_item(g.header, !chat);
    for (HWND hwnd : g.nav) show_item(hwnd, !chat);
    bool knowledge = !chat && g.section == 0;
    bool models = !chat && g.section == 1;
    bool general = !chat && g.section == 2;
    bool desktop = !chat && g.section == 3;
    show_item(g.knowledge_title, knowledge);
    show_item(g.knowledge_help, knowledge);
    show_item(g.import_button, knowledge);
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
    show_item(g.desktop_title, desktop);
    show_item(g.desktop_help, desktop);
    show_item(g.recognize_button, desktop);
    show_item(g.arm_button, desktop);
    show_item(g.desktop_view, desktop);
    layout();
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

void load_active_conversation() {
    g.transcript_path = conversation_file(g.library, g.library.active);
    g.messages.clear();
    g.history.clear();
    g.session_json.clear();
    g.scroll = 0;
    g.stick = true;
    g.chat_width = 0;
    restore_transcript();
    if (g.chat) InvalidateRect(g.chat, nullptr, FALSE);
    if (g.rail) InvalidateRect(g.rail, nullptr, FALSE);
}

void new_conversation() {
    if (!g.library.root.empty()) store_transcript();
    create_conversation(g.library);
    g.messages.clear();
    g.history.clear();
    g.session_json.clear();
    g.screen_context.clear();
    g.scroll = 0;
    g.stick = true;
    g.chat_width = 0;
    g.transcript_path = conversation_file(g.library, g.library.active);
    SetWindowTextW(g.status, g.framework_note.c_str());
    store_transcript();
    InvalidateRect(g.chat, nullptr, FALSE);
}

void open_conversation(const std::string& id) {
    if (id == g.library.active) return;
    store_transcript();
    if (!select_conversation(g.library, id)) return;
    load_active_conversation();
    SetWindowTextW(g.status, g.framework_note.c_str());
}

void delete_conversation_at(const std::string& id) {
    if (id == g.library.active) store_transcript();
    delete_conversation(g.library, id);
    load_active_conversation();
}

void go_desktop() {
    pull_knowledge();
    g.page = 1;
    g.section = 3;
    apply_page();
}

void recognize_now() {
    if (g.busy) return;
    SetWindowTextW(g.status, Tr(L"recognizing"));
    UpdateWindow(g.hwnd);
    g.reading = recognize_screen(g.hwnd);
    g.screen_context = wide_to_utf8(g.reading.text);
    if (g.desktop_view) SetWindowTextW(g.desktop_view, g.reading.text.c_str());
    wchar_t status[180];
    swprintf_s(status, g.lang == L"zh" ? L"OCR %d 行，控件 %d 个" : L"OCR %d lines, %d controls", g.reading.ocr_lines, g.reading.controls);
    if (!g.reading.error.empty() && g.reading.ocr_lines == 0) status[0] = 0;
    if (!g.reading.error.empty() && g.reading.ocr_lines == 0) {
        std::wstring note = g.lang == L"zh" ? L"识别未完成：" : L"Recognize failed: ";
        note += g.reading.error;
        SetWindowTextW(g.status, note.c_str());
    } else {
        SetWindowTextW(g.status, status);
    }
    if (g.rail) InvalidateRect(g.rail, nullptr, FALSE);
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
    if (trim_utf8(g.settings.knowledge).empty() && !has_industry(g.kb)) {
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
    std::string knowledge = compose_corpus(g.kb, g.settings.knowledge, wide_to_utf8(question));
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
            if (!screen.empty()) {
                g.screen_context = wide_to_utf8(screen);
                g.reading = last_screen();
                if (g.desktop_view) SetWindowTextW(g.desktop_view, g.reading.text.c_str());
            }
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
    bool selected = g.page == 1 && item->CtlID >= ID_NAV0 && item->CtlID <= ID_NAV3 && item->CtlID - ID_NAV0 == g.section;
    bool armed = item->CtlID == ID_SEND || item->CtlID == ID_PROBE || item->CtlID == ID_RECOGNIZE || (item->CtlID == ID_CONTROL && g.settings.allow_control) ||
                 (item->CtlID == ID_ARM && g.settings.allow_control) || (item->CtlID == ID_TASK && g.task_mode);
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

struct RailHit {
    std::string id;
    RECT rect{};
    int action = 0;
};

std::vector<RailHit> g_hits;

void paint_rail(HWND hwnd) {
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
    graphics.Clear(Color(255, 18, 18, 20));
    FontFamily& family = ui_family();
    Font title(&family, static_cast<REAL>(S(13)), FontStyleRegular, UnitPixel);
    Font row_font(&family, static_cast<REAL>(S(14)), FontStyleRegular, UnitPixel);
    Font meta(&family, static_cast<REAL>(S(12)), FontStyleRegular, UnitPixel);
    SolidBrush label(Color(255, 150, 150, 158));
    SolidBrush text(Color(255, 230, 230, 232));
    SolidBrush dim(Color(255, 140, 140, 148));
    StringFormat format;
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    graphics.DrawString(Tr(L"rail_chats"), -1, &title, RectF(static_cast<REAL>(S(16)), static_cast<REAL>(S(16)), static_cast<REAL>(width - S(24)), static_cast<REAL>(S(20))), &format, &label);
    g_hits.clear();
    int footer = S(126);
    int list_top = S(44);
    int list_bottom = std::max(list_top + S(36), height - footer);
    int row_h = S(36);
    int content = static_cast<int>(g.library.items.size()) * row_h;
    int max_scroll = std::max(0, content - (list_bottom - list_top));
    if (g.rail_scroll > max_scroll) g.rail_scroll = max_scroll;
    if (g.rail_scroll < 0) g.rail_scroll = 0;
    int y = list_top - g.rail_scroll;
    HRGN clip = CreateRectRgn(0, list_top, width, list_bottom);
    SelectClipRgn(memory, clip);
    for (const auto& item : g.library.items) {
        RECT hit{S(8), y, width - S(8), y + row_h - S(4)};
        if (hit.bottom > list_top && hit.top < list_bottom) {
            bool active = item.id == g.library.active;
            if (active) {
                GraphicsPath path;
                add_round(path, RectF(static_cast<REAL>(hit.left), static_cast<REAL>(hit.top), static_cast<REAL>(hit.right - hit.left), static_cast<REAL>(hit.bottom - hit.top)), static_cast<float>(S(8)));
                SolidBrush fill(Color(255, 42, 42, 48));
                graphics.FillPath(&fill, &path);
            }
            std::wstring title = item.title.empty() ? Tr(L"new_chat") : utf8_to_wide(item.title);
            graphics.DrawString(title.c_str(), -1, &row_font,
                                RectF(static_cast<REAL>(hit.left + S(10)), static_cast<REAL>(hit.top + S(6)), static_cast<REAL>(hit.right - hit.left - S(16)), static_cast<REAL>(row_h)),
                                &format, active ? &text : &dim);
        }
        if (hit.bottom > list_top && hit.top < list_bottom) {
            RECT clipped = hit;
            if (clipped.top < list_top) clipped.top = list_top;
            if (clipped.bottom > list_bottom) clipped.bottom = list_bottom;
            g_hits.push_back(RailHit{item.id, clipped, 1});
        }
        y += row_h;
    }
    SelectClipRgn(memory, nullptr);
    DeleteObject(clip);
    auto footer_row = [&](int index, const wchar_t* name, const std::wstring& value) {
        int top = height - footer + S(8) + index * S(36);
        RECT hit{S(8), top, width - S(8), top + S(32)};
        int value_w = hit.right - hit.left - S(108);
        if (value_w < 10) value_w = 10;
        graphics.DrawString(name, -1, &meta, RectF(static_cast<REAL>(hit.left + S(8)), static_cast<REAL>(hit.top + S(6)), static_cast<REAL>(S(92)), static_cast<REAL>(S(20))), &format, &label);
        graphics.DrawString(value.c_str(), -1, &meta,
                            RectF(static_cast<REAL>(hit.left + S(100)), static_cast<REAL>(hit.top + S(6)), static_cast<REAL>(value_w), static_cast<REAL>(S(20))),
                            &format, &text);
        g_hits.push_back(RailHit{"", hit, 2});
    };
    std::wstring screen = g.reading.window.empty() ? Tr(L"rail_none") : g.reading.window;
    wchar_t ocr_count[32];
    swprintf_s(ocr_count, g.lang == L"zh" ? L"%d 行" : L"%d lines", g.reading.ocr_lines);
    footer_row(0, Tr(L"rail_screen"), screen);
    footer_row(1, Tr(L"rail_ocr"), g.reading.ocr_lines ? ocr_count : std::wstring(L"—"));
    footer_row(2, Tr(L"rail_control"), g.settings.allow_control ? Tr(L"rail_on") : Tr(L"rail_off"));
    BitBlt(dc, 0, 0, width, height, memory, 0, 0, SRCCOPY);
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    EndPaint(hwnd, &paint);
}

LRESULT CALLBACK RailProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
        case WM_PAINT:
            paint_rail(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEWHEEL: {
            g.rail_scroll -= GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * S(36);
            if (g.rail_scroll < 0) g.rail_scroll = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP: {
            POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            for (const auto& hit : g_hits) {
                if (!PtInRect(&hit.rect, point)) continue;
                if (hit.action == 2) go_desktop();
                else if (message == WM_RBUTTONUP && !hit.id.empty()) {
                    if (MessageBoxW(g.hwnd, Tr(L"delete_chat"), Tr(L"delete_title"), MB_YESNO | MB_ICONQUESTION) == IDYES) delete_conversation_at(hit.id);
                } else if (!hit.id.empty()) open_conversation(hit.id);
                break;
            }
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
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
            g.chat = CreateWindowExW(0, L"ValhallaChat", L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 10, 10, hwnd, nullptr, g.instance, nullptr);
            WNDCLASSW rail{};
            rail.lpfnWndProc = RailProc;
            rail.hInstance = g.instance;
            rail.lpszClassName = L"ValhallaRail";
            rail.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            rail.hbrBackground = g.bg_brush;
            RegisterClassW(&rail);
            g.rail = CreateWindowExW(0, L"ValhallaRail", L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 10, 10, hwnd, nullptr, g.instance, nullptr);
            g.composer = add_edit(hwnd, true);
            g.screen_button = add_button(hwnd, ID_SCREEN);
            g.control_button = add_button(hwnd, ID_CONTROL);
            g.task_button = add_button(hwnd, ID_TASK);
            g.send = add_button(hwnd, ID_SEND);
            g.status = add_label(hwnd, false);
            g.back = add_button(hwnd, ID_BACK);
            g.header = add_label(hwnd, false);
            for (int i = 0; i < 4; ++i) g.nav[i] = add_button(hwnd, ID_NAV0 + i);
            g.knowledge_title = add_label(hwnd, false);
            g.knowledge_help = add_label(hwnd, true);
            g.import_button = add_button(hwnd, ID_IMPORT);
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
            g.desktop_title = add_label(hwnd, false);
            g.desktop_help = add_label(hwnd, true);
            g.recognize_button = add_button(hwnd, ID_RECOGNIZE);
            g.arm_button = add_button(hwnd, ID_ARM);
            g.desktop_view = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, 0, 0, 10, 10, hwnd,
                                             nullptr, g.instance, nullptr);
            SetWindowTheme(g.language, L"DarkMode_CFD", nullptr);
            SetWindowTheme(g.strategy, L"DarkMode_CFD", nullptr);
            SetWindowTheme(g.session_check, L"DarkMode_Explorer", nullptr);
            SetWindowTextW(g.knowledge, to_edit(g.settings.knowledge).c_str());
            if (g.settings.keep_session) SendMessageW(g.session_check, BM_SETCHECK, BST_CHECKED, 0);
            apply_fonts();
            sync_text();
            restore_transcript();
            apply_page();
            set_desktop_owner(hwnd);
            if (!g.layout_check) start_framework();
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
                else if (id == ID_SCREEN || id == ID_RECOGNIZE) recognize_now();
                else if (id == ID_CONTROL || id == ID_ARM) {
                    g.settings.allow_control = !g.settings.allow_control;
                    save_now();
                    SetWindowTextW(g.control_button, g.settings.allow_control ? Tr(L"control_on") : Tr(L"control"));
                    SetWindowTextW(g.arm_button, g.settings.allow_control ? Tr(L"arm_on") : Tr(L"arm_off"));
                    InvalidateRect(g.control_button, nullptr, TRUE);
                    InvalidateRect(g.arm_button, nullptr, TRUE);
                    if (g.rail) InvalidateRect(g.rail, nullptr, FALSE);
                } else if (id == ID_TASK) {
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
                } else if (id == ID_IMPORT) import_industry_dialog(hwnd);
                else if (id == ID_NEW) new_conversation();
                else if (id == ID_SETTINGS) go_settings();
                else if (id == ID_BACK) go_chat();
                else if (id >= ID_NAV0 && id <= ID_NAV3) remember_knowledge_and(id - ID_NAV0);
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

bool style_visible(HWND hwnd) {
    return hwnd && (GetWindowLongW(hwnd, GWL_STYLE) & WS_VISIBLE);
}

bool controls_overlap(HWND left, HWND right) {
    if (!style_visible(left) || !style_visible(right)) return false;
    RECT a{};
    RECT b{};
    RECT hit{};
    GetWindowRect(left, &a);
    GetWindowRect(right, &b);
    if (!IntersectRect(&hit, &a, &b)) return false;
    return hit.right - hit.left > 2 && hit.bottom - hit.top > 2;
}

bool control_inside(HWND parent, HWND child) {
    if (!style_visible(child)) return false;
    RECT rect{};
    GetWindowRect(child, &rect);
    MapWindowPoints(HWND_DESKTOP, parent, reinterpret_cast<POINT*>(&rect), 2);
    RECT client{};
    GetClientRect(parent, &client);
    return rect.left >= -2 && rect.top >= -2 && rect.right <= client.right + 2 && rect.bottom <= client.bottom + 2 && rect.right - rect.left > 8 && rect.bottom - rect.top > 8;
}

bool audit_one(HWND hwnd, int width, int height, std::wstring& log) {
    SetWindowPos(hwnd, nullptr, 30, 30, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    g.page = 0;
    g.section = 0;
    apply_page();
    auto fail = [&](const wchar_t* why) {
        wchar_t line[160];
        swprintf_s(line, L"%dx%d %s\n", width, height, why);
        log += line;
        return false;
    };
    if (!style_visible(g.rail) || !control_inside(hwnd, g.rail)) return fail(L"conversation list is missing");
    if (!control_inside(hwnd, g.chat) || !control_inside(hwnd, g.composer) || !control_inside(hwnd, g.send)) return fail(L"chat surface is outside the window");
    if (!control_inside(hwnd, g.screen_button) || !control_inside(hwnd, g.control_button) || !control_inside(hwnd, g.task_button)) return fail(L"toolbar is outside the window");
    if (!control_inside(hwnd, g.title) || !control_inside(hwnd, g.new_chat) || !control_inside(hwnd, g.settings_button)) return fail(L"header is outside the window");
    RECT rail{};
    RECT chat{};
    RECT title{};
    RECT newer{};
    RECT composer{};
    RECT tools{};
    GetWindowRect(g.rail, &rail);
    GetWindowRect(g.chat, &chat);
    GetWindowRect(g.title, &title);
    GetWindowRect(g.new_chat, &newer);
    GetWindowRect(g.composer, &composer);
    GetWindowRect(g.screen_button, &tools);
    if (chat.left + 1 < rail.right) return fail(L"chat overlaps the conversation list");
    if (title.left + 1 < rail.right) return fail(L"title overlaps the conversation list");
    if (title.right > newer.left + 1) return fail(L"title overlaps new chat");
    if (composer.bottom > tools.top + 1) return fail(L"composer overlaps the toolbar");
    if (controls_overlap(g.screen_button, g.control_button) || controls_overlap(g.control_button, g.task_button) || controls_overlap(g.task_button, g.send) ||
        controls_overlap(g.screen_button, g.send) || controls_overlap(g.control_button, g.send)) {
        return fail(L"toolbar buttons overlap");
    }
    g.page = 1;
    g.section = 3;
    apply_page();
    if (style_visible(g.rail)) return fail(L"conversation list stays open over settings");
    if (!control_inside(hwnd, g.desktop_view) || !control_inside(hwnd, g.recognize_button) || !control_inside(hwnd, g.arm_button)) return fail(L"screen module is outside the window");
    if (controls_overlap(g.recognize_button, g.arm_button)) return fail(L"screen module buttons overlap");
    for (int i = 0; i < 4; ++i) {
        if (!control_inside(hwnd, g.nav[i])) return fail(L"settings navigation is outside the window");
        for (int j = i + 1; j < 4; ++j) {
            if (controls_overlap(g.nav[i], g.nav[j])) return fail(L"settings navigation overlaps");
        }
    }
    return true;
}

bool audit_layout(HWND hwnd, std::wstring& log) {
    bool wide = audit_one(hwnd, 1280, 800, log);
    bool narrow = audit_one(hwnd, 1100, 720, log);
    if (wide && narrow) log += L"OK\n";
    return wide && narrow;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool check = false;
    bool memory_check = false;
    bool feature_check = false;
    bool layout_check = false;
    for (int i = 0; i < argc; ++i) {
        if (wcscmp(argv[i], L"--check") == 0) check = true;
        if (wcscmp(argv[i], L"--memory-check") == 0) memory_check = true;
        if (wcscmp(argv[i], L"--feature-check") == 0) feature_check = true;
        if (wcscmp(argv[i], L"--layout-check") == 0) layout_check = true;
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
    g.library.root = exe_dir() + L"\\conversations";
    bool library_existed = load_library(g.library);
    if (!library_existed) import_legacy(g.library, exe_dir() + L"\\conversation.json");
    g.transcript_path = conversation_file(g.library, g.library.active);
    g.kb_path = exe_dir() + L"\\knowledge.json";
    g.settings = load_settings(g.settings_path);
    load_knowledge(g.kb_path, g.kb);
    sync_notes(g.kb, g.settings.knowledge);
    save_knowledge(g.kb_path, g.kb);
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
    g.layout_check = layout_check;
    HWND hwnd = CreateWindowExW(0, L"ValhallaWindow", L"Valhalla", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, width, height,
                                nullptr, nullptr, instance, nullptr);
    if (layout_check) {
        std::wstring log;
        bool ok = hwnd && audit_layout(hwnd, log);
        if (!ok && log.empty()) log = L"window was not created\n";
        std::string body = wide_to_utf8(log);
        HANDLE file = CreateFileW((exe_dir() + L"\\native-layout.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD wrote = 0;
            WriteFile(file, body.data(), static_cast<DWORD>(body.size()), &wrote, nullptr);
            CloseHandle(file);
        }
        if (hwnd) DestroyWindow(hwnd);
        GdiplusShutdown(g.gdi);
        return ok ? 0 : 1;
    }
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
