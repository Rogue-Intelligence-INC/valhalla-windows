# Valhalla Windows

Chat-first Windows shell for the Valhalla runtime. Snapshot date: **28 October 2026**.

Proprietary. See [LICENSE](LICENSE). This repository is the desktop application, not the Valhalla research runtime.

## What is here

`Valhalla.exe` is the native shell built on this date. `启动.bat` starts it. The C++ sources are in `native/src`, and `native/build.bat` rebuilds the executable with Visual Studio 2022 Build Tools.

The Python files (`app.py`, `lift_engine.py`, `i18n.py`, `models_catalog.py`, `settings_store.py`) are the same shell’s earlier interface. They call `valhalla_base.exe`.

The shell has a chat surface and three settings panels:

- **Knowledge** — handwritten notes, one fact per line, plus an industry import. Imported files are stored separately from notes and marked pending review. Chat searches the imported passages for the question. Formats: txt, md, csv, tsv, json, jsonl, html, xml, rtf, log, docx, xlsx, pptx, and pdf. The library is `knowledge.json` beside the executable and is not committed.
- **Chats** — the left column keeps a list of conversations. New chat adds one instead of erasing the previous one. Right-click a row to delete it.
- **Screen** — Recognize captures the desktop behind the window, reads it with Windows OCR, and lists UI Automation controls with screen coordinates.
- **Computer control** — off until it is switched on. Actions the model may emit are `read_screen`, `click`, `click_text`, `move`, `type`, `key`, and `scroll`.
- **Models** — Qwen2.5 Instruct checkpoints, download and select.
- **General** — language, answer strategy, and whether later turns keep the structure session.

Answer strategies in the interface: faithful retrieval, hybrid multiple choice, follow-up session, and multi-hop.

## What is not here

- `conversation.json` and `settings.json` stay on the machine that ran the app.
- The virtual environment, logs, and object files are not included.
- Model weights are not included.
- `valhalla_base.exe`, the research runtime, lives in the licensed Valhalla checkout. It is not this repository.

The Python launcher looks for that runtime at `VALHALLA_BASE_EXE`, then at `c:\Ai\Valhalla-main\target\release\valhalla_base.exe`. Model files are read from `VALHALLA_LM_MODEL`, then from `c:\Ai\models`.

## Build

```bat
native\build.bat
```

The script expects the Visual Studio 2022 Build Tools `vcvars64.bat` at the default Build Tools path. Output is `Valhalla.exe` in this directory.

## Run

```bat
启动.bat
```

nlohmann/json in `native/third_party` is included so the native shell can build. That header keeps its own license notice. Inflating docx, xlsx, pptx, and pdf streams uses `puff` by Mark Adler (`native/third_party/puff`), kept with its own notice.

`native\build.bat` also builds `native\kb_test.exe`, which checks the importers and retrieval without starting the model.
