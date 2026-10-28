"""Chat-first Windows shell. Settings has Knowledge, Models, and General."""

from __future__ import annotations

import queue
import sys
import threading
import tkinter as tk

import customtkinter as ctk

from i18n import I18n, NOTE_KEYS, STRATEGY_KEYS
from lift_engine import STRATEGIES, resolve_binary, run_chat_turn, run_lift
from models_catalog import download_model, list_models, model_dir
from settings_store import load_settings

ctk.set_appearance_mode("dark")
ctk.set_default_color_theme("blue")

BG = "#0f1218"
PANEL = "#171c27"
CARD = "#1e2533"
USER = "#2a4570"
ASSIST = "#1e2533"
TEXT = "#eceae4"
MUTED = "#9aa3b2"
LINE = "#2a3142"


def self_test() -> int:
    binary = resolve_binary()
    print(f"binary {binary}")
    for spec in STRATEGIES:
        result = run_lift(spec["context"], spec["question"], spec["id"])
        print(f"[{spec['id']}] {result.lift}")
        print(f"  path   {result.decode_path}")
        print(f"  answer {result.answer.replace(chr(10), ' / ')}")
        if spec["id"] == "open" and "native" not in result.decode_path:
            print("open path was not native", file=sys.stderr)
            return 1
        if spec["id"] == "mcq" and "lm_patch" not in result.decode_path:
            print("mcq path was not lm_patch", file=sys.stderr)
            return 1
        if spec["id"] == "followup" and "第二问" not in result.answer:
            print("followup missing second turn", file=sys.stderr)
            return 1
    print("SELF_TEST_OK")
    return 0


class LiftApp(ctk.CTk):
    def __init__(self) -> None:
        super().__init__()
        self.settings = load_settings()
        self.i18n = I18n(self.settings.language)
        self.title("Valhalla")
        self.geometry("1080x740")
        self.minsize(900, 620)
        self.configure(fg_color=BG)
        self._busy = False
        self._inbox: queue.Queue = queue.Queue()
        self.session: dict | None = None
        self.history: list[tuple[str, str]] = []

        self.grid_rowconfigure(0, weight=1)
        self.grid_columnconfigure(0, weight=1)
        self.chat = ChatPage(self)
        self.settings_page = SettingsPage(self)
        self.chat.grid(row=0, column=0, sticky="nsew")
        self.apply_language()
        self.after(80, self._drain)

    def post(self, fn) -> None:
        self._inbox.put(fn)

    def _drain(self) -> None:
        try:
            while True:
                fn = self._inbox.get_nowait()
                try:
                    fn()
                except Exception as exc:  # noqa: BLE001
                    self._busy = False
                    self.chat.status.configure(text=str(exc)[:400])
        except queue.Empty:
            pass
        self.after(80, self._drain)

    def show_chat(self) -> None:
        self.settings_page.save_knowledge()
        self.settings_page.grid_remove()
        self.chat.grid(row=0, column=0, sticky="nsew")

    def show_settings(self) -> None:
        self.chat.grid_remove()
        self.settings_page.grid(row=0, column=0, sticky="nsew")
        self.settings_page.show_section("knowledge")

    def apply_language(self) -> None:
        self.i18n.set_choice(self.settings.language)
        self.title(self.i18n.t("app_title"))
        self.chat.refresh()
        self.settings_page.refresh()

    def tr(self, key: str) -> str:
        return self.i18n.t(key)


class ChatPage(ctk.CTkFrame):
    def __init__(self, app: LiftApp) -> None:
        super().__init__(app, fg_color=BG)
        self.app = app
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(1, weight=1)

        header = ctk.CTkFrame(self, fg_color=BG, height=64)
        header.grid(row=0, column=0, sticky="ew", padx=20, pady=(12, 0))
        header.grid_columnconfigure(1, weight=1)
        self.title = ctk.CTkLabel(header, text="Valhalla", font=ctk.CTkFont(size=22, weight="bold"), text_color=TEXT)
        self.title.grid(row=0, column=0, sticky="w")
        self.hint = ctk.CTkLabel(header, text="", text_color=MUTED, font=ctk.CTkFont(size=12))
        self.hint.grid(row=1, column=0, sticky="w")
        actions = ctk.CTkFrame(header, fg_color=BG)
        actions.grid(row=0, column=2, rowspan=2, sticky="e")
        self.new_button = ctk.CTkButton(actions, text="", width=110, fg_color=CARD, hover_color=LINE, command=self.clear)
        self.new_button.pack(side="left", padx=(0, 8))
        self.settings_button = ctk.CTkButton(actions, text="", width=96, command=app.show_settings)
        self.settings_button.pack(side="left")

        self.thread = ctk.CTkScrollableFrame(self, fg_color=BG)
        self.thread.grid(row=1, column=0, sticky="nsew", padx=12, pady=8)
        self.empty = ctk.CTkLabel(self.thread, text="", text_color=MUTED, font=ctk.CTkFont(size=14))
        self.empty.pack(pady=48)

        composer = ctk.CTkFrame(self, fg_color=PANEL, corner_radius=16)
        composer.grid(row=2, column=0, sticky="ew", padx=20, pady=(0, 16))
        composer.grid_columnconfigure(0, weight=1)
        self.composer = ctk.CTkTextbox(composer, height=64, font=ctk.CTkFont(size=15), fg_color=PANEL, text_color=TEXT)
        self.composer.grid(row=0, column=0, sticky="ew", padx=(12, 8), pady=12)
        self.composer.bind("<Return>", self._on_return)
        self.send_button = ctk.CTkButton(composer, text="", width=88, command=self.send)
        self.send_button.grid(row=0, column=1, padx=(0, 12), pady=12)
        self.status = ctk.CTkLabel(self, text="", text_color=MUTED, anchor="w")
        self.status.grid(row=3, column=0, sticky="ew", padx=24, pady=(0, 10))

    def refresh(self) -> None:
        self.title.configure(text=self.app.tr("app_title"))
        self.hint.configure(text=self.app.tr("chat_hint"))
        self.new_button.configure(text=self.app.tr("new_chat"))
        self.settings_button.configure(text=self.app.tr("settings"))
        self.send_button.configure(text=self.app.tr("send"))
        self.empty.configure(text=self.app.tr("empty_chat"))

    def clear(self) -> None:
        self.app.session = None
        self.app.history.clear()
        for child in self.thread.winfo_children():
            if child is not self.empty:
                child.destroy()
        self.empty.pack(pady=48)
        self.status.configure(text="")

    def _on_return(self, event):
        if event.state & 0x0001:
            return None
        self.send()
        return "break"

    def send(self) -> None:
        if self.app._busy:
            return
        question = self.composer.get("1.0", "end").strip()
        if not question:
            return
        knowledge = self.app.settings.knowledge.strip()
        if not knowledge:
            self._bubble("assistant", self.app.tr("need_knowledge"), "")
            return
        folder = model_dir(self.app.settings.model_repo)
        if not ((folder / "model.safetensors").is_file() or (folder / "model.safetensors.index.json").is_file()):
            self._bubble("assistant", self.app.tr("need_model"), "")
            return
        self.composer.delete("1.0", "end")
        self._bubble("user", question, "")
        self.app._busy = True
        self.send_button.configure(state=tk.DISABLED)
        self.status.configure(text=self.app.tr("thinking"))
        mode = self.app.settings.strategy
        use_session = self.app.session if (self.app.settings.keep_session or mode == "followup") else None
        history = list(self.app.history)

        def worker() -> None:
            try:
                result, session = run_chat_turn(
                    knowledge,
                    question,
                    mode,
                    model_dir=folder,
                    session=use_session,
                    history=history,
                )
                self.app.post(lambda: self._done(question, result.answer, result.decode_path, session))
            except Exception as exc:  # noqa: BLE001
                message = str(exc)
                self.app.post(lambda: self._done(question, message, "", None))

        threading.Thread(target=worker, daemon=True).start()

    def _done(self, question: str, answer: str, path: str, session: dict | None) -> None:
        self.app._busy = False
        self.send_button.configure(state=tk.NORMAL)
        if self.app.settings.keep_session or self.app.settings.strategy == "followup":
            self.app.session = session
        else:
            self.app.session = None
        if path:
            self.app.history.append((question, answer))
        self._bubble("assistant", answer, path)
        self.status.configure(text="")

    def _bubble(self, role: str, text: str, path: str) -> None:
        self.empty.pack_forget()
        wrap = ctk.CTkFrame(self.thread, fg_color=BG)
        wrap.pack(fill="x", pady=6)
        bubble = ctk.CTkFrame(wrap, fg_color=USER if role == "user" else ASSIST, corner_radius=16)
        bubble.pack(anchor="e" if role == "user" else "w", padx=12)
        who = self.app.tr("you") if role == "user" else self.app.tr("assistant")
        ctk.CTkLabel(bubble, text=who, text_color=MUTED, font=ctk.CTkFont(size=11)).pack(anchor="w", padx=14, pady=(10, 0))
        ctk.CTkLabel(
            bubble,
            text=text,
            text_color=TEXT,
            font=ctk.CTkFont(size=15),
            wraplength=560,
            justify="left",
        ).pack(anchor="w", padx=14, pady=(2, 12 if not path else 2))
        if path:
            ctk.CTkLabel(bubble, text=path, text_color=MUTED, font=ctk.CTkFont(size=11), wraplength=560, justify="left").pack(
                anchor="w", padx=14, pady=(0, 10)
            )
        self.thread.update_idletasks()
        self.thread._parent_canvas.yview_moveto(1.0)


class SettingsPage(ctk.CTkFrame):
    def __init__(self, app: LiftApp) -> None:
        super().__init__(app, fg_color=BG)
        self.app = app
        self.grid_columnconfigure(1, weight=1)
        self.grid_rowconfigure(1, weight=1)
        self._section = "knowledge"

        header = ctk.CTkFrame(self, fg_color=BG)
        header.grid(row=0, column=0, columnspan=2, sticky="ew", padx=16, pady=(12, 8))
        self.back_button = ctk.CTkButton(header, text="", width=88, fg_color=CARD, hover_color=LINE, command=app.show_chat)
        self.back_button.pack(side="left")
        self.header_title = ctk.CTkLabel(header, text="", font=ctk.CTkFont(size=20, weight="bold"), text_color=TEXT)
        self.header_title.pack(side="left", padx=16)

        nav = ctk.CTkFrame(self, fg_color=BG, width=200)
        nav.grid(row=1, column=0, sticky="nsw", padx=(16, 8), pady=(0, 16))
        nav.grid_propagate(False)
        self.nav_buttons: dict[str, ctk.CTkButton] = {}
        for key in ("knowledge", "models", "general"):
            button = ctk.CTkButton(
                nav,
                text="",
                anchor="w",
                height=44,
                fg_color=CARD,
                hover_color=LINE,
                command=lambda name=key: self.show_section(name),
            )
            button.pack(fill="x", pady=4)
            self.nav_buttons[key] = button

        body = ctk.CTkFrame(self, fg_color=BG)
        body.grid(row=1, column=1, sticky="nsew", padx=(8, 16), pady=(0, 16))
        body.grid_columnconfigure(0, weight=1)
        body.grid_rowconfigure(0, weight=1)
        self.knowledge_frame = self._build_knowledge(body)
        self.models_frame = ctk.CTkScrollableFrame(body, fg_color=BG)
        self.general_frame = ctk.CTkFrame(body, fg_color=BG)
        self._build_general()
        for frame in (self.knowledge_frame, self.models_frame, self.general_frame):
            frame.grid(row=0, column=0, sticky="nsew")

    def _build_knowledge(self, parent) -> ctk.CTkFrame:
        frame = ctk.CTkFrame(parent, fg_color=BG)
        frame.grid_columnconfigure(0, weight=1)
        frame.grid_rowconfigure(2, weight=1)
        self.knowledge_title = ctk.CTkLabel(frame, text="", font=ctk.CTkFont(size=18, weight="bold"), text_color=TEXT)
        self.knowledge_title.grid(row=0, column=0, sticky="w")
        self.knowledge_help = ctk.CTkLabel(frame, text="", text_color=MUTED, wraplength=680, justify="left")
        self.knowledge_help.grid(row=1, column=0, sticky="w", pady=(4, 8))
        self.knowledge_box = ctk.CTkTextbox(frame, font=ctk.CTkFont(size=14), fg_color=CARD, text_color=TEXT)
        self.knowledge_box.grid(row=2, column=0, sticky="nsew")
        self.knowledge_box.insert("1.0", self.app.settings.knowledge)
        return frame

    def _build_general(self) -> None:
        frame = self.general_frame
        frame.grid_columnconfigure(0, weight=1)
        self.general_title = ctk.CTkLabel(frame, text="", font=ctk.CTkFont(size=18, weight="bold"), text_color=TEXT)
        self.general_title.grid(row=0, column=0, sticky="w")
        self.language_label = ctk.CTkLabel(frame, text="", text_color=MUTED)
        self.language_label.grid(row=1, column=0, sticky="w", pady=(16, 4))
        self.language_menu = ctk.CTkOptionMenu(frame, values=["system"], command=self._on_language, width=240)
        self.language_menu.grid(row=2, column=0, sticky="w")
        self.strategy_label = ctk.CTkLabel(frame, text="", text_color=MUTED)
        self.strategy_label.grid(row=3, column=0, sticky="w", pady=(18, 4))
        self.strategy_menu = ctk.CTkOptionMenu(frame, values=["open"], command=self._on_strategy, width=360)
        self.strategy_menu.grid(row=4, column=0, sticky="w")
        self.strategy_detail = ctk.CTkLabel(frame, text="", text_color=TEXT, wraplength=640, justify="left")
        self.strategy_detail.grid(row=5, column=0, sticky="w", pady=(8, 0))
        self.session_switch = ctk.CTkSwitch(frame, text="", command=self._on_session)
        self.session_switch.grid(row=6, column=0, sticky="w", pady=(18, 4))
        if self.app.settings.keep_session:
            self.session_switch.select()
        self.session_help = ctk.CTkLabel(frame, text="", text_color=MUTED, wraplength=640, justify="left")
        self.session_help.grid(row=7, column=0, sticky="w")
        self.runtime = ctk.CTkLabel(frame, text="", text_color=MUTED, wraplength=640, justify="left")
        self.runtime.grid(row=8, column=0, sticky="w", pady=(24, 0))

    def refresh(self) -> None:
        self.back_button.configure(text=self.app.tr("back"))
        self.header_title.configure(text=self.app.tr("settings"))
        for key, button in self.nav_buttons.items():
            button.configure(text=self.app.tr(f"nav_{key}"))
        self.knowledge_title.configure(text=self.app.tr("knowledge_title"))
        self.knowledge_help.configure(text=self.app.tr("knowledge_help"))
        self.general_title.configure(text=self.app.tr("general_title"))
        self.language_label.configure(text=self.app.tr("language"))
        self.strategy_label.configure(text=self.app.tr("strategy"))
        self.session_switch.configure(text=self.app.tr("keep_session"))
        self.session_help.configure(text=self.app.tr("keep_session_help"))
        self._fill_language_menu()
        self._fill_strategy_menu()
        self._paint_nav()
        self._fill_models()
        try:
            binary = resolve_binary()
            self.runtime.configure(text=f"{self.app.tr('binary_ok')}   {binary}")
        except FileNotFoundError:
            self.runtime.configure(text=self.app.tr("binary_missing"))

    def show_section(self, name: str) -> None:
        self.save_knowledge()
        self._section = name
        self.knowledge_frame.grid_remove()
        self.models_frame.grid_remove()
        self.general_frame.grid_remove()
        {"knowledge": self.knowledge_frame, "models": self.models_frame, "general": self.general_frame}[name].grid(
            row=0, column=0, sticky="nsew"
        )
        self._paint_nav()
        if name == "models":
            self._fill_models()

    def _paint_nav(self) -> None:
        for key, button in self.nav_buttons.items():
            button.configure(fg_color=LINE if key == self._section else CARD)

    def save_knowledge(self) -> None:
        text = self.knowledge_box.get("1.0", "end").strip()
        if text != self.app.settings.knowledge:
            self.app.settings.knowledge = text
            self.app.settings.save()
            self.app.session = None
            self.app.history.clear()

    def _lang_pairs(self) -> list[tuple[str, str]]:
        return [
            ("system", self.app.tr("lang_system")),
            ("zh", self.app.tr("lang_zh")),
            ("en", self.app.tr("lang_en")),
        ]

    def _fill_language_menu(self) -> None:
        pairs = self._lang_pairs()
        labels = [label for _code, label in pairs]
        self.language_menu.configure(values=labels)
        current = dict(pairs).get(self.app.settings.language, labels[0])
        self.language_menu.set(current)

    def _on_language(self, label: str) -> None:
        code = next(item for item, text in self._lang_pairs() if text == label)
        self.app.settings.language = code
        self.app.settings.save()
        self.app.apply_language()

    def _strategy_pairs(self) -> list[tuple[str, str]]:
        pairs = []
        for spec in STRATEGIES:
            title_key, _delta = STRATEGY_KEYS[spec["id"]]
            pairs.append((spec["id"], self.app.tr(title_key)))
        return pairs

    def _fill_strategy_menu(self) -> None:
        pairs = self._strategy_pairs()
        labels = [label for _code, label in pairs]
        self.strategy_menu.configure(values=labels)
        current = dict(pairs).get(self.app.settings.strategy, labels[0])
        self.strategy_menu.set(current)
        self._show_strategy_detail(self.app.settings.strategy)

    def _on_strategy(self, label: str) -> None:
        code = next(item for item, text in self._strategy_pairs() if text == label)
        self.app.settings.strategy = code
        if code == "followup":
            self.app.settings.keep_session = True
            self.session_switch.select()
        self.app.settings.save()
        self.app.session = None
        self._show_strategy_detail(code)

    def _show_strategy_detail(self, code: str) -> None:
        _title, delta = STRATEGY_KEYS.get(code, ("strategy_open", "delta_open"))
        self.strategy_detail.configure(text=self.app.tr(delta))

    def _on_session(self) -> None:
        self.app.settings.keep_session = bool(self.session_switch.get())
        self.app.settings.save()
        if not self.app.settings.keep_session:
            self.app.session = None

    def _fill_models(self) -> None:
        for child in self.models_frame.winfo_children():
            child.destroy()
        ctk.CTkLabel(
            self.models_frame,
            text=self.app.tr("models_title"),
            font=ctk.CTkFont(size=18, weight="bold"),
            text_color=TEXT,
        ).pack(anchor="w")
        ctk.CTkLabel(
            self.models_frame,
            text=self.app.tr("models_help"),
            text_color=MUTED,
            wraplength=680,
            justify="left",
        ).pack(anchor="w", pady=(4, 12))
        for model in list_models():
            self._model_card(model)

    def _model_card(self, model) -> None:
        card = ctk.CTkFrame(self.models_frame, fg_color=CARD, corner_radius=14)
        card.pack(fill="x", pady=6)
        note_key = NOTE_KEYS.get(model.repo_id, "note_0_5")
        state = self.app.tr("installed") if model.installed else self.app.tr("not_installed")
        current = model.repo_id == self.app.settings.model_repo
        title = f"{model.label}    {self.app.tr('about')} {model.size}    {state}"
        if current:
            title = f"{title}    {self.app.tr('in_use')}"
        ctk.CTkLabel(card, text=title, text_color=TEXT, font=ctk.CTkFont(size=15, weight="bold")).pack(anchor="w", padx=14, pady=(12, 2))
        ctk.CTkLabel(card, text=self.app.tr(note_key), text_color=MUTED, wraplength=640, justify="left").pack(anchor="w", padx=14)
        row = ctk.CTkFrame(card, fg_color=CARD)
        row.pack(anchor="w", padx=14, pady=(8, 12))
        if model.installed and not current:
            ctk.CTkButton(row, text=self.app.tr("use"), width=88, command=lambda repo=model.repo_id: self._use_model(repo)).pack(side="left")
        if not model.installed:
            ctk.CTkButton(row, text=self.app.tr("download"), width=100, command=lambda repo=model.repo_id: self._download(repo)).pack(side="left")

    def _use_model(self, repo_id: str) -> None:
        self.app.settings.model_repo = repo_id
        self.app.settings.save()
        self.app.session = None
        self._fill_models()

    def _download(self, repo_id: str) -> None:
        if self.app._busy:
            return
        self.app._busy = True

        def worker() -> None:
            try:
                download_model(repo_id, lambda _text: None)
                self.app.post(lambda: self._download_finished(repo_id, ""))
            except Exception as exc:  # noqa: BLE001
                message = str(exc)
                self.app.post(lambda: self._download_finished(repo_id, message))

        threading.Thread(target=worker, daemon=True).start()

    def _download_finished(self, repo_id: str, error: str) -> None:
        self.app._busy = False
        if not error:
            self.app.settings.model_repo = repo_id
            self.app.settings.save()
        self._fill_models()


def main() -> int:
    if "--self-test" in sys.argv:
        return self_test()
    app = LiftApp()
    app.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
