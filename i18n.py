"""UI strings. language is system, zh, or en."""

from __future__ import annotations

import ctypes


def windows_ui_language() -> str:
    try:
        langid = ctypes.windll.kernel32.GetUserDefaultUILanguage()
    except (AttributeError, OSError):
        return "en"
    if (langid & 0x3FF) == 0x04:
        return "zh"
    return "en"


def resolve_language(choice: str) -> str:
    if choice in {"zh", "en"}:
        return choice
    return windows_ui_language()


STRINGS: dict[str, dict[str, str]] = {
    "zh": {
        "app_title": "Valhalla",
        "chat_hint": "基于知识库回答。走已验证的提升路径。",
        "settings": "设置",
        "back": "返回",
        "new_chat": "新对话",
        "send": "发送",
        "you": "你",
        "assistant": "Valhalla",
        "thinking": "正在回答…",
        "empty_chat": "从下面开始。知识库在设置里。",
        "need_knowledge": "知识库是空的。打开设置，在「知识库」里写上语料。",
        "need_model": "这个模型还没有权重。到设置的「模型」页下载。",
        "nav_knowledge": "知识库",
        "nav_models": "模型",
        "nav_general": "综合",
        "knowledge_title": "知识库",
        "knowledge_help": "一行一条。对话用这些句子做检索。",
        "models_title": "模型",
        "models_help": "只列出本机解码器能加载的免费 Qwen2.5 Instruct。",
        "general_title": "综合",
        "language": "语言",
        "lang_system": "跟随系统",
        "lang_zh": "中文",
        "lang_en": "English",
        "strategy": "回答策略",
        "keep_session": "多轮沿用上一轮会话",
        "keep_session_help": "打开后，后面的问题带着上一轮结构会话。",
        "installed": "已安装",
        "not_installed": "未下载",
        "download": "下载",
        "use": "使用",
        "in_use": "当前",
        "about": "约",
        "download_done": "下载完成。",
        "failed": "没有完成。",
        "binary_ok": "运行时",
        "binary_missing": "找不到 valhalla_base.exe",
        "strategy_open": "忠实检索",
        "strategy_mcq": "选择题混合解码",
        "strategy_followup": "追问会话",
        "strategy_multihop": "多跳",
        "delta_open": "RAGTruth 34/40，对照 0/40",
        "delta_mcq": "0.5B 约 +48 个百分点",
        "delta_followup": "30 轮追问 96.7%",
        "delta_multihop": "MultiHop 9/15，对照 8/15",
        "note_0_5": "默认小模型。选择题混合解码约 63%。",
        "note_1_5": "免费 Qwen2 权重，可以直接加载。",
        "note_3": "对照实验用的 3B。选择题 45/46。",
        "note_7": "免费 7B。选择题 44/46，没有再高于 3B。",
    },
    "en": {
        "app_title": "Valhalla",
        "chat_hint": "Answers use the knowledge base and the verified lift paths.",
        "settings": "Settings",
        "back": "Back",
        "new_chat": "New chat",
        "send": "Send",
        "you": "You",
        "assistant": "Valhalla",
        "thinking": "Answering…",
        "empty_chat": "Start below. The knowledge base is in Settings.",
        "need_knowledge": "The knowledge base is empty. Add lines under Settings, Knowledge.",
        "need_model": "That model has no weights yet. Download it under Settings, Models.",
        "nav_knowledge": "Knowledge",
        "nav_models": "Models",
        "nav_general": "General",
        "knowledge_title": "Knowledge",
        "knowledge_help": "One fact per line. Chat retrieves from these lines.",
        "models_title": "Models",
        "models_help": "Free Qwen2.5 Instruct checkpoints this decoder can load.",
        "general_title": "General",
        "language": "Language",
        "lang_system": "Match system",
        "lang_zh": "中文",
        "lang_en": "English",
        "strategy": "Answer strategy",
        "keep_session": "Keep the session across turns",
        "keep_session_help": "Later questions reuse the previous structure session.",
        "installed": "Installed",
        "not_installed": "Not downloaded",
        "download": "Download",
        "use": "Use",
        "in_use": "In use",
        "about": "About",
        "download_done": "Download finished.",
        "failed": "Did not finish.",
        "binary_ok": "Runtime",
        "binary_missing": "valhalla_base.exe was not found",
        "strategy_open": "Faithful retrieval",
        "strategy_mcq": "Hybrid multiple choice",
        "strategy_followup": "Follow-up session",
        "strategy_multihop": "Multi-hop",
        "delta_open": "RAGTruth 34/40 versus 0/40",
        "delta_mcq": "About +48 pp at 0.5B",
        "delta_followup": "96.7% on 30-turn follow-up",
        "delta_multihop": "MultiHop 9/15 versus 8/15",
        "note_0_5": "Default small model. Hybrid MCQ is about 63%.",
        "note_1_5": "Free Qwen2 weights this decoder can load.",
        "note_3": "The 3B used in the paired comparisons. MCQ 45/46.",
        "note_7": "Free 7B. MCQ 44/46, no further gain over 3B.",
    },
}

STRATEGY_KEYS = {
    "open": ("strategy_open", "delta_open"),
    "mcq": ("strategy_mcq", "delta_mcq"),
    "followup": ("strategy_followup", "delta_followup"),
    "multihop": ("strategy_multihop", "delta_multihop"),
}

NOTE_KEYS = {
    "Qwen/Qwen2.5-0.5B-Instruct": "note_0_5",
    "Qwen/Qwen2.5-1.5B-Instruct": "note_1_5",
    "Qwen/Qwen2.5-3B-Instruct": "note_3",
    "Qwen/Qwen2.5-7B-Instruct": "note_7",
}


class I18n:
    def __init__(self, choice: str) -> None:
        self.choice = choice
        self.code = resolve_language(choice)

    def set_choice(self, choice: str) -> None:
        self.choice = choice
        self.code = resolve_language(choice)

    def t(self, key: str) -> str:
        table = STRINGS.get(self.code) or STRINGS["en"]
        return table.get(key) or STRINGS["en"].get(key, key)
