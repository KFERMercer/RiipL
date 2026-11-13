# RiipL — 快如闪电的全能桌面翻译器

RiipL 是基于 Qt 6.8 与 Qt Widgets 构建的跨平台桌面翻译应用。应用支持自定义提示词、术语表、语气风格与文档翻译等高级功能。

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
├── packaging/            # packaging script and package definition
└── tests/
    └── core/             # QTest suites for the core layer, one per unit, wired to CTest
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
timeout 5 ./build/riipl 2>&1; echo "exit: $?"
```

测试套件保存在 `tests/core/` 目录下，结构保持扁平。每个核心单元对应一个 `tst_<单元头文件名的小写形式>.cpp` 文件。公共工具存放在同目录的 `TestSupport.h` 中。新增单元时，将套件名称添加至 `tests/core/CMakeLists.txt` 的 `RIIPL_TEST_SUITES`。

> [!IMPORTANT]
> 修改 `src/core` 与 `src/utils` 时，必须确保全部测试通过。新增核心功能需同步补充 QTest 用例。

## 代码风格

- 遵循 Qt 6.8 标准编码规范，禁止使用过时 API。
- 优先使用 Qt 标准库与 Qt 原生控件。布局严禁使用绝对长度单位，确保界面在不同分辨率下自适应。
- **禁止造轮子**：不得自行实现 Qt 或 C++ 标准库已有的功能。动手前先确认是否存在原生等价 API，若存在则必须直接使用。
- 开发时若有更符合 Qt 原生风格的实现方案，主动提出重构。
- 代码注释一律使用英文。禁止产生冗余注释，禁止记录中间尝试或取舍过程，仅说明未在代码中体现的开发理由或最终行为。
- 功能开发完成后，清理无用代码和残留调用，包括单次使用的包装函数、废弃的成员变量与失效的 include 指令。
- 声称“已修复”或“已通过”前必须有实际执行结果支撑，禁止仅凭代码阅读下结论。

## 其他事项

- 任务完成后重新生成 i18n `.ts` 资源文件。
- 任务结束后的总结阶段起草本次任务的 commit message 供参考，格式参考历史提交规范（Conventional Commits）。
