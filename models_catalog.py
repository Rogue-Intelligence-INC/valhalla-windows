"""Free Qwen2 checkpoints this Windows runtime can actually load."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

MODELS_ROOT = Path(r"c:\Ai\models")

# Candle patch-lm is Qwen2 only. These are the public Instruct checkpoints.
FREE_MODELS: list[dict[str, str]] = [
    {
        "id": "Qwen/Qwen2.5-0.5B-Instruct",
        "label": "Qwen2.5 0.5B",
        "size": "1 GB",
        "note": "默认。选择题混合解码在这一档约 63%（对照裸 RAG 约 15%）。",
    },
    {
        "id": "Qwen/Qwen2.5-1.5B-Instruct",
        "label": "Qwen2.5 1.5B",
        "size": "3 GB",
        "note": "免费 Qwen2 权重，本机解码器可以直接加载。",
    },
    {
        "id": "Qwen/Qwen2.5-3B-Instruct",
        "label": "Qwen2.5 3B",
        "size": "6 GB",
        "note": "对照实验用的骨干。同栈选择题 45/46，RAGTruth 对照也是这一档。",
    },
    {
        "id": "Qwen/Qwen2.5-7B-Instruct",
        "label": "Qwen2.5 7B",
        "size": "15 GB",
        "note": "免费。同栈选择题 44/46，比 3B 没有再抬上去。",
    },
]


@dataclass
class LocalModel:
    repo_id: str
    label: str
    size: str
    note: str
    path: Path
    installed: bool

    @property
    def menu_label(self) -> str:
        mark = "已安装" if self.installed else "未下载"
        return f"{self.label}  ·  {self.size}  ·  {mark}"


def model_dir(repo_id: str) -> Path:
    return MODELS_ROOT / repo_id.split("/")[-1]


def has_weights(path: Path) -> bool:
    return (path / "model.safetensors").is_file() or (path / "model.safetensors.index.json").is_file()


def list_models() -> list[LocalModel]:
    found: list[LocalModel] = []
    for item in FREE_MODELS:
        path = model_dir(item["id"])
        found.append(
            LocalModel(
                repo_id=item["id"],
                label=item["label"],
                size=item["size"],
                note=item["note"],
                path=path,
                installed=has_weights(path),
            )
        )
    return found


def find_model(menu_label: str) -> LocalModel:
    for model in list_models():
        if model.menu_label == menu_label or model.label == menu_label or model.repo_id == menu_label:
            return model
    raise KeyError(menu_label)


def download_model(repo_id: str, on_status) -> Path:
    dest = model_dir(repo_id)
    dest.mkdir(parents=True, exist_ok=True)
    on_status(f"正在下载 {repo_id} → {dest}")
    from huggingface_hub import snapshot_download

    snapshot_download(
        repo_id,
        local_dir=str(dest),
        allow_patterns=["*.safetensors", "*.json", "tokenizer*", "*.jinja"],
    )
    if not has_weights(dest):
        raise RuntimeError(f"下载结束，但 {dest} 里没有 model.safetensors。")
    on_status(f"已就绪 {dest}")
    return dest
