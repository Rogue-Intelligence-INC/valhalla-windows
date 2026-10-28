"""Persisted desktop preferences."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

STORE = Path(__file__).resolve().parent / "settings.json"

DEFAULT_KNOWLEDGE = (
    "ValhallaBase primary protocol is valhalla-base-v1.\n"
    "Production decode mode is hybrid with native retrieval for open QA."
)


@dataclass
class Settings:
    language: str = "system"
    strategy: str = "open"
    model_repo: str = "Qwen/Qwen2.5-0.5B-Instruct"
    knowledge: str = DEFAULT_KNOWLEDGE
    keep_session: bool = True

    def save(self) -> None:
        STORE.write_text(
            json.dumps(
                {
                    "language": self.language,
                    "strategy": self.strategy,
                    "model_repo": self.model_repo,
                    "knowledge": self.knowledge,
                    "keep_session": self.keep_session,
                },
                ensure_ascii=False,
                indent=2,
            ),
            encoding="utf-8",
        )


def load_settings() -> Settings:
    if not STORE.is_file():
        settings = Settings()
        settings.save()
        return settings
    raw = json.loads(STORE.read_text(encoding="utf-8"))
    return Settings(
        language=str(raw.get("language") or "system"),
        strategy=str(raw.get("strategy") or "open"),
        model_repo=str(raw.get("model_repo") or "Qwen/Qwen2.5-0.5B-Instruct"),
        knowledge=str(raw.get("knowledge") if raw.get("knowledge") is not None else DEFAULT_KNOWLEDGE),
        keep_session=bool(raw.get("keep_session", True)),
    )
