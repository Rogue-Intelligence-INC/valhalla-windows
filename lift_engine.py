"""Local valhalla_base calls for the largest verified lifts."""

from __future__ import annotations

import json
import os
import subprocess
from dataclasses import dataclass
from pathlib import Path

from models_catalog import list_models

DEFAULT_BINARY = Path(r"c:\Ai\Valhalla-main\target\release\valhalla_base.exe")
CREATE_NO_WINDOW = 0x08000000

STRATEGIES: list[dict[str, str]] = [
    {
        "id": "open",
        "title": "忠实检索",
        "delta": "+85 pp",
        "blurb": "RAGTruth 34/40，整段进模型与普通 RAG 都是 0/40。",
        "context": (
            "ValhallaBase primary protocol is valhalla-base-v1.\n"
            "Production decode mode is hybrid with native retrieval for open QA."
        ),
        "question": "What decode mode does ValhallaBase use?",
    },
    {
        "id": "mcq",
        "title": "选择题混合解码",
        "delta": "+48 pp",
        "blurb": "0.5B 上混合解码约 63%，对照裸 RAG 约 15%。3B 同栈 45/46。",
        "context": (
            "The capital of France is Paris.\n"
            "The capital of Germany is Berlin.\n"
            "Hybrid decode routes MCQ to LM logprob and open questions to native retrieval."
        ),
        "question": "What is the capital of France? A) London B) Paris C) Rome D) Madrid",
    },
    {
        "id": "followup",
        "title": "追问会话",
        "delta": "96.7%",
        "blurb": "30 轮追问在册 96.7%。第二问只带着上一轮 session，不再把全文塞回模型。",
        "context": (
            "The lab badge code is 1842.\n"
            "That lab is on the third floor."
        ),
        "question": "What is the lab badge code?\n---\nWhich floor is that lab on?",
    },
    {
        "id": "multihop",
        "title": "多跳",
        "delta": "+6.7 pp",
        "blurb": "同一 3B 上 MultiHop 9/15，整段进模型与普通 RAG 是 8/15。",
        "context": (
            "Alice works in Berlin.\n"
            "Berlin is the capital of Germany."
        ),
        "question": "In which country does Alice work?",
    },
]


@dataclass
class LiftResult:
    answer: str
    decode_path: str
    score_type: str
    lift: str
    note: str


def strategy_by_id(strategy_id: str) -> dict[str, str]:
    for item in STRATEGIES:
        if item["id"] == strategy_id:
            return item
    raise KeyError(strategy_id)


def resolve_binary() -> Path:
    env = os.environ.get("VALHALLA_BASE_EXE", "").strip()
    if env and Path(env).is_file():
        return Path(env)
    if DEFAULT_BINARY.is_file():
        return DEFAULT_BINARY
    raise FileNotFoundError("找不到 valhalla_base.exe。设置 VALHALLA_BASE_EXE。")


def default_model_dir() -> Path:
    env = os.environ.get("VALHALLA_LM_MODEL", "").strip()
    if env and _has_weights(Path(env)):
        return Path(env)
    for model in list_models():
        if model.installed:
            return model.path
    raise FileNotFoundError("还没有已安装的模型。先在列表里下载一个。")


def chat_prompt(context: str, question: str, history: list[tuple[str, str]] | None = None) -> str:
    """Put the knowledge base in the prompt the decoder actually generates from."""
    lines = [line.strip() for line in context.splitlines() if line.strip()]
    corpus = "\n".join(lines)
    prior = ""
    if history:
        turns = []
        for user, assistant in history[-6:]:
            turns.append(f"User: {user}\nAssistant: {assistant}")
        prior = "\n\n".join(turns) + "\n\n"
    knowledge = f"Knowledge:\n{corpus}\n\n" if corpus else ""
    return (
        "Use the knowledge below when it answers the user. "
        "Reply in the user's language, in one or two sentences. "
        "If the knowledge does not contain the answer, say that briefly.\n\n"
        f"{knowledge}{prior}User: {question}"
    )


def context_lines(text: str) -> list[str]:
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    if not lines:
        raise ValueError("语料至少要有一行。")
    return lines


def split_followup(question: str) -> tuple[str, str]:
    if "\n---\n" in question:
        first, second = question.split("\n---\n", 1)
    else:
        parts = [line.strip() for line in question.splitlines() if line.strip()]
        if len(parts) < 2:
            raise ValueError("追问需要两问，中间用单独一行 --- 分开。")
        first, second = parts[0], parts[1]
    first, second = first.strip(), second.strip()
    if not first or not second:
        raise ValueError("追问的两问都不能空。")
    return first, second


def run_chat_turn(
    context: str,
    question: str,
    mode: str,
    model_dir: Path | None = None,
    session: dict | None = None,
    timeout: float = 180.0,
    history: list[tuple[str, str]] | None = None,
) -> tuple[LiftResult, dict | None]:
    """One chat turn. A stored session continues without resending the knowledge base."""
    question = question.strip()
    if not question:
        raise ValueError("empty question")
    spec = strategy_by_id(mode if mode in {item["id"] for item in STRATEGIES} else "open")
    model = Path(model_dir) if model_dir else default_model_dir()
    if not _has_weights(model):
        raise FileNotFoundError(str(model))
    invoke_mode = "multihop" if mode == "multihop" else "open"
    score_type = "mcq" if mode == "mcq" else "open"
    lines = [] if session else context_lines(context)
    # Chat has to come back as a sentence. Hybrid open stays a retrieval span.
    decode = "hybrid" if mode == "mcq" else "lm_patch"
    asked = question if mode == "mcq" else chat_prompt(context, question, history)
    payload = _invoke(
        lines,
        asked,
        score_type,
        model,
        session,
        timeout,
        invoke_mode,
        decode=decode,
        max_new=160,
    )
    row = _first_row(payload)
    result = LiftResult(
        answer=str(row.get("answer") or "").strip() or "(empty)",
        decode_path=str(row.get("decode_path") or ""),
        score_type=str(row.get("score_type") or score_type),
        lift=spec["title"],
        note=spec["blurb"],
    )
    next_session = payload.get("session") if isinstance(payload.get("session"), dict) else None
    return result, next_session


def run_lift(
    context: str,
    question: str,
    mode: str,
    model_dir: Path | None = None,
    timeout: float = 180.0,
) -> LiftResult:
    question = question.strip()
    if not question:
        raise ValueError("请先写问题。")
    spec = strategy_by_id(mode)
    model = Path(model_dir) if model_dir else default_model_dir()
    if not _has_weights(model):
        raise FileNotFoundError(f"模型还没下好：{model}")

    if mode == "followup":
        first_q, second_q = split_followup(question)
        first = _invoke(context_lines(context), first_q, "open", model, None, timeout, mode)
        session = first.get("session")
        if not session:
            raise RuntimeError("第一问没有返回 session，追问接不上。")
        second = _invoke([], second_q, "open", model, session, timeout, mode)
        row = _first_row(second)
        first_row = _first_row(first)
        answer = (
            f"第一问\n{first_row.get('answer', '').strip()}\n\n"
            f"第二问\n{row.get('answer', '').strip()}"
        )
        decode_path = str(row.get("decode_path") or "")
        return LiftResult(
            answer=answer,
            decode_path=decode_path,
            score_type="open",
            lift=spec["title"],
            note=spec["blurb"],
        )

    score_type = "mcq" if mode == "mcq" else "open"
    payload = _invoke(context_lines(context), question, score_type, model, None, timeout, mode)
    row = _first_row(payload)
    return LiftResult(
        answer=str(row.get("answer") or "").strip() or "(空答案)",
        decode_path=str(row.get("decode_path") or ""),
        score_type=str(row.get("score_type") or score_type),
        lift=spec["title"],
        note=spec["blurb"],
    )


def _invoke(
    lines: list[str],
    question: str,
    score_type: str,
    model: Path,
    session: dict | None,
    timeout: float,
    mode: str,
    decode: str = "hybrid",
    max_new: int | None = None,
) -> dict:
    binary = resolve_binary()
    config = {
        "decode": decode,
        "lm_model": str(model),
        "mcq_decode": "lm_patch",
        "follow_up_decode": "native_follow_up_aware",
        "fate_ingress_routing": "quad_cycle",
        "fate_qa_feedback": True,
        "proactive_enabled": False,
        "multihop_fate": mode == "multihop",
        "multihop_v2": mode == "multihop",
    }
    if max_new is not None:
        config["max_new"] = max_new
    request = {
        "context": lines,
        "questions": [{"id": "q0", "text": question, "score_type": score_type}],
        "config": config,
    }
    if session is not None:
        request["session"] = session
    env = os.environ.copy()
    env["VALHALLA_QWEN_MODEL_DIR"] = str(model)
    env["VALHALLA_LM_MODEL"] = str(model)
    env["VALHALLA_LM_F16"] = "1"
    cuda_bin = r"C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.4\bin"
    env["PATH"] = cuda_bin + ";" + env.get("PATH", "")
    completed = subprocess.run(
        [str(binary), "--json-error"],
        input=json.dumps(request, ensure_ascii=False),
        text=True,
        encoding="utf-8",
        capture_output=True,
        timeout=timeout,
        env=env,
        cwd=str(binary.parent),
        creationflags=CREATE_NO_WINDOW,
    )
    stdout = completed.stdout or ""
    stderr = completed.stderr or ""
    if completed.returncode != 0:
        detail = stderr.strip() or stdout.strip() or f"exit {completed.returncode}"
        raise RuntimeError(detail[-1200:])
    return _parse_json_payload(stdout)


def _first_row(payload: dict) -> dict:
    rows = payload.get("rows") or []
    if not rows:
        raise RuntimeError("valhalla_base 没有返回答案行。")
    return rows[0]


def _parse_json_payload(stdout: str) -> dict:
    start = stdout.find("{")
    end = stdout.rfind("}")
    if start < 0 or end < start:
        raise RuntimeError("输出里没有 JSON。\n" + stdout[-800:])
    return json.loads(stdout[start : end + 1])


def _has_weights(path: Path) -> bool:
    return (path / "model.safetensors").is_file() or (path / "model.safetensors.index.json").is_file()
