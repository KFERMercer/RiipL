# RiipL — AI 驱动的桌面翻译应用

RiipL 是一款基于 Qt 构建的跨平台桌面翻译应用，目标是复制 DeepL 的功能。它支持丰富的自定义选项，包括提示词工程、术语表、多种语气与风格，以及文档翻译功能。

## 项目概述

### 项目架构

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

### 构建与运行

```bash
# 构建时必须使用 --clean-first 进行清洁构建
cmake -S . -B build
cmake --build build -j$(nproc) --clean-first
```

```bash
# 运行应用（限时 5 秒，用于快速验证启动是否正常）
timeout 5 ./build/RiipL 2>&1; echo "exit: $?"
```

## 代码风格

- 必须遵循 Qt 标准编码范式。
- 优先使用 Qt 标准库，并使用 Qt 原生控件。
- 开发过程中若发现存在可重构为更符合 Qt 原生风格的实现路径，需主动提出重构方案。
- 文档与注释一律使用英文。
- 文档与注释禁止提及中间阶段的尝试或取舍过程，只描述非显而易见的开发理由或最终行为。
- 功能开发落地后，务必分析并清理冗余代码与遗留调用。

## Agent 行为规范

- **只读（Plan）模式下严禁修改项目文件**，包括但不限于：编辑器写入、终端写入、脚本写入、重定向输出、创建/删除/移动文件等任何形式。
- **严禁读取、写入或查找项目目录以外的文件。**
- **必须使用相对路径**操作项目根目录下的文件。
- 执行破坏性操作（如删除文件、覆盖关键配置等）前**必须先行确认**，除非任务本身已明确要求且范围清晰。
- 当内置工具与命令行功能等价时，**必须优先调用内置工具，禁止使用 shell 命令**。此优先级可保证可靠性、跨平台安全性，并规避 shell 注入风险。仅当所需操作无法通过工具完成，或组合多个工具调用会显著降低效率时，才允许回退至 shell 命令，且须附加简短注释说明回退原因。
