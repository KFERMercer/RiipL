# RiipL — AI 驱动的桌面翻译应用

RiipL 是一款基于 Qt 构建的跨平台桌面翻译应用，目标是复制 DeepL 的功能。它支持丰富的自定义选项，包括提示词工程、术语表、多种语气与风格，以及文档翻译功能。

## 项目概述

### 项目架构

```text
RiipL/
├── resources/            # app icon, .qrc bundles, i18n .ts sources
├── src/
│   ├── core/             # framework-free logic (unit-tested)
│   │   ├── config/       # Defaults.h (default configuration) + ConfigManager (fallback store)
│   │   ├── network/      # ApiClient: SSE streaming, timeouts, error mapping
│   │   ├── translation/  # PromptBuilder, TranslationEngine, languages & tones
│   │   ├── models/       # Glossary
│   │   ├── json/         # JsonUtils
│   │   └── history/      # HistoryManager
│   ├── ui/               # MainWindow, bound editor widgets, dialogs
│   └── utils/            # TextUtils, SingleInstance
└── tests/                # QTest suite for the core layer
```

### 构建与运行

```bash
# 重新生成 i18n .ts 资源文件
cmake --build build --target update_translations
```

```bash
# -DBUILD_TESTING=ON 启用测试套件
cmake -S . -B build -DBUILD_TESTING=ON
# 构建时必须使用 --clean-first 进行清洁构建
cmake --build build -j$(nproc) --clean-first
```

### 测试

```bash
ctest --test-dir build --output-on-failure
```

```bash
# 运行应用（限时 5 秒，用于快速验证启动是否正常）
timeout 5 ./build/RiipL 2>&1; echo "exit: $?"
```

> [!IMPORTANT]
> src/core 与 src/utils 的改动必须保证全部测试通过；新增核心功能需同步补充 QTest 用例。

## 代码风格

- 必须遵循 Qt 标准编码范式。
- 优先使用 Qt 标准库与 Qt 原生控件。严禁在布局设计中使用绝对长度单位；此约束可优化设计系统的弹性，提升响应式设计视觉一致性。
- **禁止造轮子**：不得自行实现 Qt 或 C++ 标准库已提供的能力。动手前先确认是否存在原生等价 API，存在则必须使用。
- 开发过程中若发现存在可重构为更符合 Qt 原生风格的实现路径，需主动提出重构方案。
- 减少注释，移除冗余注释。
- 文档与注释一律使用英文，且禁止提及中间阶段的尝试或取舍过程，只描述非显而易见的开发理由或最终行为。
- 功能开发落地后，务必分析并清理冗余代码与遗留调用，包括单次使用的包装函数、不再需要的成员变量与失效的 include。
- 声称"已修复"或"已通过"前必须有实际执行结果支撑，禁止仅凭代码阅读下结论。

## 其他事项

- 任务完成后**必须重新生成 i18n .ts 资源文件**
- 在任务结束后的总结阶段**起草此次任务的 commit message**供用户参考。提交信息格式需参考以往提交惯例。
