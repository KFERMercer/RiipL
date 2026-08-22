<div align="center">

<img src="resources/icons/app.svg" width="128" height="128" alt="RiipL logo"/>

# RiipL

**Local DeepL Rip-off.**

![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-cce5ff)
![Qt](https://img.shields.io/badge/Qt-6.2%2B-41CD52?logo=qt&logoColor=white)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-3.21%2B-064F8C?logo=cmake&logoColor=white)

**English** | [中文](README.zh-CN.md)

[Introduction](#introduction) · [Features](#features) · [Build](#build--run) · [Tips](#usage-tips) · [Architecture](#architecture) · [Limitations](#limitations)

![APP Screenshot](img/APP.png)

</div>

## Introduction

RiipL is a cross-platform desktop translation app. It plugs into **any OpenAI-compatible API** to serve as its translation engine, relying on no cloud translation service.

RiipL openly, shamelessly rips off and reimplements [DeepL](https://www.deepl.com)'s most features: translation notes, glossaries, wording alternatives, long-document translation and more. No more paying for an [expensive DeepL subscription](https://www.deepl.com/pro).

RiipL is built with C++ and Qt 6, and the compiled binary weighs in at only about **1MB**. Linux, macOS and Windows are supported.

The AI Agent under human supervision wrote the majority of RiipL's code.

## Features

What DeepL reserves for paying members, RiipL gives you completely free:

- 🔌 **Bring your own model** — works with OpenAI, Ollama, LM Studio, llama.cpp and any compatible gateway.
- 🧩 **Prompt template pipeline** — multiple customizable prompt templates: glossary, tone, style, background knowledge, personalization and more. Placeholder injection and live preview supported.
- 📖 **Glossary** — custom translations for your domain terms, with JSON import/export.
- 🗣 **Tones, styles & background** — 13 preset tones plus custom ones, with free-form style and background info.
- ⌨️ **Global hotkey** — summon the hidden window and translate the clipboard from anywhere.
- 📋 **Clipboard monitoring** — clipboard text is translated automatically, with copy-back of results.
- 🕘 **Translation history** — search and reuse past translations.

## Build & Run

### Prerequisites

| Dependency | Version |
| :- | :- |
| Qt 6 (Widgets, Network, LinguistTools) | 6.2+ |
| CMake | 3.21+ |
| C++ compiler | C++17 capable (GCC, Clang, MSVC) |

Debian / Ubuntu:

```bash
sudo apt install build-essential cmake qt6-base-dev qt6-tools-dev qt6-l10n-tools
```

Windows and macOS need a regular Qt 6 installation (online installer or `brew install qt cmake`); deploy with `windeployqt` / `macdeployqt` respectively.

### Compile

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --clean-first -j$(nproc)
```

After a successful build the executable lives at `./build/RiipL`.

### Test

```bash
cmake --build build --target riip_tests
cd build && QT_QPA_PLATFORM=offscreen ./riip_tests
```

## Usage Tips

### Connecting a custom model

Where `config.json` lives:

| Platform | Location |
| :- | :- |
| Linux | `~/.config/RiipL/config.json` |
| Windows | `%APPDATA%\\RiipL\\config.json` |
| macOS | `~/Library/Preferences/RiipL/config.json` |

Taking Ollama as an example:

```json
{
  "api": {
    "base_url": "http://localhost:11434/v1",
    "api_key": "",
    "model": "hf.co/unsloth/Hy-MT2-7B-GGUF:UD-Q8_K_XL",
    "temperature": 0,
    "extra_body": "{\"top_p\": 0.6, \"top_k\": 20, \"repetition_penalty\": 1.05}"
  }
}
```

Anything inside `extra_body` is merged straight into the request body, so vendor-specific sampling parameters work out of the box.

> [!NOTE]
> The API key is stored in plain text next to your other settings. Keep the file private
> yourself if that matters to you.

## Architecture

```text
RiipL/
├── resources/            # app icon, .qrc bundles, .ts/.qm translations
├── src/
│   ├── core/             # framework-free logic (unit-tested)
│   │   ├── config/       # Defaults.h (source of truth) + ConfigManager (fallback store)
│   │   ├── network/      # ApiClient: SSE streaming, timeouts, error mapping
│   │   ├── translation/  # PromptBuilder, TranslationEngine, languages & tones
│   │   ├── models/       # Glossary
│   │   └── history/      # HistoryManager
│   ├── platform/         # GlobalHotkey (Win / macOS Carbon / Linux X11)
│   ├── ui/               # MainWindow, bound editor widgets, dialogs
│   └── utils/            # JsonUtils, TextUtils, SingleInstance
└── tests/                # QTest suite for the core layer
```

## Limitations

- Global hotkeys on Linux require an X11 session; Wayland is not supported yet.
- Alternative-wording and tokenization quality depend on the connected model.
