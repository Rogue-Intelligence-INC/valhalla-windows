# Valhalla Windows

Chat-first Windows shell for the Valhalla runtime. Snapshot date: **28 October 2026**.

Proprietary. See [LICENSE](LICENSE). This repository is the desktop application, not the Valhalla research runtime.

## What is here

`Valhalla.exe` is the native shell built on this date. `启动.bat` starts it. The C++ sources are in `native/src`, and `native/build.bat` rebuilds the executable with Visual Studio 2022 Build Tools.

The Python files (`app.py`, `lift_engine.py`, `i18n.py`, `models_catalog.py`, `settings_store.py`) are the same shell’s earlier interface. They call `valhalla_base.exe`.

The shell has a chat surface and three settings panels:

- **Knowledge** — one fact per line. Chat retrieves from those lines.
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

nlohmann/json in `native/third_party` is included so the native shell can build. That header keeps its own license notice.
