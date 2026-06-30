# RiipL — Qt 翻译软件实现计划

> 本文档为提供给 coding agent 的执行 prompt。按里程碑顺序实现，每个里程碑完成后应能独立编译运行。

## 一、项目概述

构建一个类似 DeepL 的跨平台桌面翻译软件，连接 OpenAI 兼容 API 完成翻译。项目名 `RiipL`。

### 技术栈

| 项 | 选择 |
|---|---|
| 语言 | C++17 |
| GUI 框架 | Qt 6 (Widgets) |
| 构建系统 | CMake ≥ 3.21 |
| 网络层 | Qt6::Network (QNetworkAccessManager) |
| JSON | QJsonDocument / QJsonObject |
| UI 国际化 | Qt Linguist (.ts → .qm) |
| 配置/历史存储 | JSON 文件 |
| 全局热键 | 平台原生 API 条件编译封装 |
| 第三方依赖 | 无（仅 Qt6） |

### 跨平台目标

Windows 10+、macOS 11+、Linux（X11/Wayland）。除全局热键外全部使用 Qt 标准库。

---

## 二、目录结构

```
RiipL/
├── CMakeLists.txt
├── resources/
│   ├── riipL.qrc
│   ├── i18n/
│   │   ├── riip_en.ts / riip_zh.ts
│   │   └── riip_en.qm / riip_zh.qm
│   └── icons/app.svg
├── src/
│   ├── main.cpp
│   ├── core/
│   │   ├── config/      ConfigManager, Defaults, Paths
│   │   ├── network/     ApiClient, ApiResponse
│   │   ├── translation/ TranslationEngine, PromptBuilder, Language, Tone
│   │   ├── models/      Glossary, GlossaryEntry, TranslationRecord, ModelConfig
│   │   └── history/     HistoryManager
│   ├── platform/        GlobalHotkey (条件编译)
│   ├── ui/
│   │   ├── mainwindow/  MainWindow
│   │   ├── widgets/     TranslationEdit, CandidatePopup
│   │   └── dialogs/     SettingsDialog, GlossaryDialog, ToneDialog, HistoryDialog
│   └── utils/           JsonUtils
└── tests/
```

---

## 三、配置系统设计（核心机制）

### 3.1 默认值回落机制（关键约束）

这是整个配置系统的核心设计，**必须严格遵守**：

1. **硬编码默认值**：在 `src/core/config/Defaults.h` 中以 `constexpr` / `inline` 定义所有配置项的默认值。这是"真理之源"。
2. **配置文件只存非默认值**：用户配置文件 `config.json`（位于 `QStandardPaths::AppConfigLocation`）只存储用户**实际修改过**的项。未修改的项不出现在文件中。
3. **读取逻辑**：`ConfigManager::value(key)` 先查 `config.json`；若不存在，返回 `Defaults.h` 中的硬编码默认值。
4. **写入逻辑**：用户修改某项时写入 `config.json`；若新值等于默认值，则从 `config.json` **删除该键**（保持文件只含非默认项）。
5. **"恢复默认" = 删除键**：UI 中恢复默认的具体操作是从 `config.json` 删除对应键，使其回落到硬编码默认值。**绝不**把默认值写回文件。

### 3.2 UI 交互规范

每个可配置输入控件遵循以下交互：

- **输入框 `placeholderText` 显示默认值**：形如 `默认：formal`。当输入框为空时，placeholder 显示，实际生效值为默认值。
- **输入框背景色提示**：当当前值与默认值**不同**时，输入框背景设为浅黄色（`#FFFDE7`）；与默认值相同时为常规背景色。这是"底色显示默认值"的视觉反馈。
- **"恢复默认"按钮隐藏**：每个配置项旁有一个小型重置按钮（图标为 ↺），**默认隐藏**（`visible = false`）。仅当该项当前值与默认值不同时才显示。点击后清空输入框并从 `config.json` 删除该键。
- **实时生效**：输入框 `textEdited` 信号触发配置写入与默认值比对，动态切换背景色与重置按钮可见性。

### 3.3 `Defaults.h` 结构示意

```cpp
namespace Defaults {
    // API
    inline const QString apiBase = "https://api.openai.com/v1";
    inline const QString model = "gpt-4o-mini";
    inline const double temperature = 0.3;
    inline const int maxTokens = 4096;
    inline const double topP = 1.0;
    inline const bool stream = true;

    // UI
    inline const QString language = "en";       // 界面语言: en/zh
    inline const bool autoTranslate = false;
    inline const int autoTranslateDelay = 800;  // ms 防抖

    // 翻译
    inline const QString sourceLang = "auto";
    inline const QString targetLang = "English";
    inline const QString tone = "neutral";

    // Prompt 模板（见第七节）
    inline const QString promptDefault = R"(...)";
    inline const QString promptGlossary = R"(...)";
    // ... 每个 prompt 片段一个默认值
}
```

### 3.4 `config.json` 结构

```json
{
  "api": {
    "base_url": "https://api.openai.com/v1",
    "api_key": "sk-...",
    "model": "gpt-4o-mini",
    "temperature": 0.3,
    "max_tokens": 4096,
    "top_p": 1.0,
    "stream": true,
    "extra_body": {}
  },
  "ui": {
    "language": "en",
    "auto_translate": false,
    "auto_translate_delay": 800,
    "window_geometry": "...",
    "always_on_top": false
  },
  "translation": {
    "source_lang": "auto",
    "target_lang": "English",
    "tone": "neutral",
    "custom_tones": [],
    "style": "",
    "background": ""
  },
  "glossary": {
    "enabled": true,
    "entries": [
      {"source": "苹果", "target": "Apple"},
      {"source": "专有名词", "target": ""}
    ]
  },
  "prompts": {
    "default": "...",
    "glossary": "...",
    "tone": "...",
    "style": "...",
    "background": "...",
    "personalization": "...",
    "delimiters": "...",
    "structured": "...",
    "candidate": "..."
  },
  "hotkey": {
    "enabled": false,
    "sequence": "Ctrl+Alt+T"
  },
  "clipboard": {
    "monitor": false,
    "delay_ms": 500
  },
  "history": {
    "enabled": true,
    "max_records": 500
  }
}
```

**注意**：`prompts` 节中只存储用户自定义过的模板；未出现的模板回落到 `Defaults.h`。

### 3.5 配置文件路径

- 使用 `QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)` 拼接 `config.json`。
- 首次运行：目录不存在则创建；文件不存在则写入空 `{}`（不预填默认值，默认值在代码中）。
- 翻译历史：同目录下 `history.json`。

---

## 四、国际化（i18n）

- 使用 Qt Linguist：源代码中所有用户可见字符串用 `tr()` 包裹。
- `.ts` 文件维护中文（`riip_zh.ts`）与英文（`riip_en.ts`），通过 CMake 的 `qt6_add_translation` 编译为 `.qm`。
- `main.cpp` 中安装 `QTranslator`，根据 `config.json` 的 `ui.language` 加载对应 `.qm`；切换语言时提示重启或动态重译（推荐：调用 `QCoreApplication::quit()` 提示重启，或重新安装 translator 并对主窗口调用 `retranslateUi()` 方法）。
- 为每个自定义控件/对话框实现 `void retranslateUi()` 槽，集中刷新可见文字，支持运行时切换语言不重启。

---

## 五、网络层与 API 客户端

### 5.1 `ApiClient` 类

```
class ApiClient : public QObject
  - void sendChatRequest(const QJsonObject& body, std::function<void(QString)> onDone, std::function<void(QString)> onStream, std::function<void(QString)> onError)
  - 构造请求：POST {base_url}/chat/completions
  - Header: Authorization: Bearer {api_key};  Content-Type: application/json
  - Body: {"model":..., "messages":[...], "temperature":..., "max_tokens":..., "top_p":..., "stream":..., ...extra_body}
  - stream=true: 解析 SSE (data: 行)，逐 token 回调 onStream
  - stream=false: 一次性解析 choices[0].message.content，回调 onDone
  - 超时处理、错误码映射为可读消息
```

### 5.2 模型参数配置 UI

设置对话框"API"页包含：
- 常用参数的独立输入框：`base_url`、`api_key`（密码模式）、`model`、`temperature`(QDoubleSpinBox 0-2)、`max_tokens`(QSpinBox)、`top_p`(QDoubleSpinBox 0-1)、`stream`(QCheckBox)。
- 每个输入框遵循 3.2 的默认值回落交互。
- **额外 body 参数**：一个 `QPlainTextEdit`，用户可输入 JSON 对象（如 `{"frequency_penalty": 0.5, "presence_penalty": 0}`），解析后 `merge` 到请求 body。输入框下方显示语法校验状态（绿✓/红✗）。空则不追加。

---

## 六、语言与语气常量

### 6.1 支持语言列表（`Language.h`）

定义为一个 `struct LangItem { QString code; QString en; QString zh; }` 数组，`code` 用于 prompt，`en`/`zh` 用于 UI 显示。源语言列表首项为 `Auto`（`code = "auto"`）：

```
auto / 自动检测
zh / Chinese / 中文
en / English / 英语
fr / French / 法语
pt / Portuguese / 葡萄牙语
es / Spanish / 西班牙语
ja / Japanese / 日语
tr / Turkish / 土耳其语
ru / Russian / 俄语
ar / Arabic / 阿拉伯语
ko / Korean / 韩语
th / Thai / 泰语
it / Italian / 意大利语
de / German / 德语
vi / Vietnamese / 越南语
ms / Malay / 马来语
id / Indonesian / 印尼语
fil / Filipino / 菲律宾语
hi / Hindi / 印地语
zh-Hant / Traditional Chinese / 繁体中文
pl / Polish / 波兰语
cs / Czech / 捷克语
nl / Dutch / 荷兰语
km / Khmer / 高棉语
my / Burmese / 缅甸语
fa / Persian / 波斯语
gu / Gujarati / 古吉拉特语
ur / Urdu / 乌尔都语
te / Telugu / 泰卢固语
mr / Marathi / 马拉地语
he / Hebrew / 希伯来语
bn / Bengali / 孟加拉语
ta / Tamil / 泰米尔语
uk / Ukrainian / 乌克兰语
bo / Tibetan / 藏语
kk / Kazakh / 哈萨克语
mn / Mongolian / 蒙古语
ug / Uyghur / 维吾尔语
yue / Cantonese / 粤语
```

### 6.2 语气列表（`Tone.h`）

```cpp
struct ToneItem { QString key; QString en; QString zh; };
// 预设：
formal/正式风格, casual/口语风格, neutral/中性风格, technical/技术风格,
marketing/营销风格, literary/文学风格, academic/学术风格, legal/法律风格,
literal/直译风格, idiomatic/意译风格, transcreation/创译风格, machine-like/机器风格, concise/简明风格
```

- 语气选择为下拉框，预设项 + 用户自定义项（`custom_tones` 数组）。
- 下拉框旁有"管理语气"按钮，打开 `ToneDialog` 可增删自定义语气（仅存 `key` 与显示名，`key` 注入 prompt 占位符）。

---

## 七、Prompt 模板系统

### 7.1 占位符

以下占位符可在所有模板中使用，`PromptBuilder` 统一替换：

| 占位符 | 含义 |
|---|---|
| `{source_text}` | 待翻译原文 |
| `{target_lang}` | 目标语言英文名 |
| `{source_lang}` | 源语言英文名（auto 时为 "the detected language"） |
| `{tone}` | 语气 key |
| `{target_style}` | 翻译风格描述 |
| `{background_text}` | 翻译背景 |
| `{glossary}` | 术语表格式化文本 |
| `{user_preferences}` | 个性化偏好项 |
| `{format_type}` | 结构化数据格式（json/xml/html/md...） |
| `{word}` | 候选遣词：被点击的词 |
| `{translated_text}` | 候选遣词：完整译文 |

### 7.2 模板片段与组合逻辑

`PromptBuilder::build(TranslationContext ctx)` 按以下顺序拼接为最终 `messages`：

1. **背景片段**（若 `background` 非空）：使用 `prompts.background` 模板。
2. **术语片段**（若 glossary 启用且有条目）：使用 `prompts.glossary` 模板，`{glossary}` 替换为逐行 "`src` translates to `tgt`" / 无 tgt 则 "`src` (do not translate)"。
3. **语气片段**（若 tone 非 neutral 或非空）：使用 `prompts.tone` 模板。
4. **风格片段**（若 style 非空）：使用 `prompts.style` 模板。
5. **个性化片段**（若有用户偏好项）：使用 `prompts.personalization` 模板。
6. **主翻译指令**：使用 `prompts.default` 模板（或结构化数据时用 `prompts.structured`）。

每个片段模板都可在设置对话框"Prompt 模板"页编辑，遵循 3.2 默认值回落交互。`Defaults.h` 中硬编码以下默认模板（依据用户提供的参考 prompt）：

### 7.3 默认模板（硬编码于 `Defaults.h`）

**default**（中文/英文根据界面语言自动选择，以下给出中英两版，`PromptBuilder` 按 `ui.language` 选用；模板内可用 `{lang_variant}` 区分，或直接存两套 `default_zh`/`default_en`）：

```
[zh] 将以下文本翻译为 `{target_lang}`，注意**只需要输出翻译后的结果，不要额外解释**：

`{source_text}`

[en] Translate the following text into `{target_lang}`. Note that you should **only output the translated result without any additional explanation**:

`{source_text}`
```

**glossary**：
```
[zh] *参考下面的翻译：*
{glossary}
将以下文本翻译为 `{target_lang}`，注意**只需要输出翻译后的结果，不要额外解释**：

`{source_text}`

[en] *Reference the following translations:*
{glossary}
Translate the following text into `{target_lang}`. Note that you must **ONLY output the translated result without any additional explanation**:

`{source_text}`
```
（`{glossary}` 展开为每行 `` `src` translates to `tgt` ``；tgt 为空则 `` `src` (leave untranslated) ``）

**tone**：
```
[zh] 请将以下文本翻译为 `{target_lang}`。
注意翻译的语气要严格符合【**{tone}**】

`{source_text}`

[en] Please translate the following text into `{target_lang}`. Note that the translation tone must strictly conform to [**{tone}**]:

`{source_text}`
```

**style**：
```
[zh] 请将以下文本翻译为 `{target_lang}`。
注意翻译的风格要严格符合【**{target_style}**】

`{source_text}`

[en] Please translate the following text into `{target_lang}`. Note that the translation style must strictly conform to [**{target_style}**]:

`{source_text}`
```

**background**：
```
[zh] *【背景信息】*
`{background_text}`

请结合背景信息将以下文本翻译为 `{target_lang}`。

*【待翻译文本】*
`{source_text}`

[en] *[Background Information]*
`{background_text}`

Please translate the following text into `{target_lang}`, taking the provided background information into consideration.

*[Source Text]*
`{source_text}`
```

**personalization**：
```
[zh] *【待翻译文本】*
`{source_text}`

*【翻译任务】*
{user_preferences}
将【待翻译文本】翻译为 `{target_lang}`。

[en] *[Source Text]*
`{source_text}`

*[Translation Tasks]*
{user_preferences}
Translate the [Source Text] into `{target_lang}`.
```
（`{user_preferences}` 展开为编号列表 `1. **pref1**\n2. **pref2**\n...`）

**delimiters**（保留分隔符模式，由 UI 按钮触发）：
```
[zh] 请将以下文本准确翻译为 `{target_lang}`。
你必须在译文中**保留等量的分隔符，绝对不可遗漏、转义或翻译该符号，并注意分隔符的位置**。

`{source_text}`

[en] Please accurately translate the following text into `{target_lang}`.
You must **retain the exact same number of delimiters in the translation. Strictly do not omit, escape, or translate these symbols, and pay close attention to their placement**.

`{source_text}`
```

**structured**（结构化数据翻译，`{format_type}` 由 UI 检测或用户指定）：
```
[zh] *# 任务目标*
将下方 `{source_text}` 中的 `{format_type}` 格式数据翻译为 `{target_lang}`。

*# 严格约束*
1. **结构锁定**：绝对保持原有的 `{format_type}` 数据结构、缩进和层级完全不变。
2. **选择性翻译**：仅翻译面向用户展示的可见文本内容。
3. **禁止修改**：**严禁**翻译或更改任何代码标签、键名、变量占位符（如 `{{var}}`、`${var}`、`%s`、`%d` 等）或代码属性。

*# 数据输入*
`{source_text}`

[en] *### Task*
Translate the user-facing text within the following `{format_type}` data into `{target_lang}`.

*### Strict Rules*
1. **Structure Preservation:** You MUST preserve the original `{format_type}` data structure, nesting, hierarchy, and indentation exactly as they are.
2. **Selective Translation:** Translate ONLY the visible, user-facing text content/values.
3. **Strict Non-Translation:** NEVER translate or alter code tags, keys, properties, object names, or variable placeholders. Leave them exactly in their original English/code form.

*### Source Data*
`{source_text}`
```

**candidate**（候选遣词专用）：
```
[zh] 原文：`{source_text}`
译文：`{translated_text}`
用户在译文中选中了「{word}」一词。请为该位置提供 2-4 个语法正确、符合语境的备选表达。
**只输出备选项，每行一个，不要任何解释。**

[en] Source: `{source_text}`
Translation: `{translated_text}`
The user selected the word "{word}" in the translation. Provide 2-4 alternative expressions for this position, grammatically correct and contextually appropriate.
**Output only the alternatives, one per line, without any explanation.**
```

> **实现提示**：为简化，模板按界面语言存中英两套（`_zh`/`_en` 后缀）。`Defaults.h` 中全部硬编码。`config.json` 的 `prompts` 节可选覆盖任一模板键（如 `default_zh`、`candidate_en`）。

---

## 八、翻译引擎与流程

### 8.1 `TranslationEngine`

```
class TranslationEngine : public QObject
  - setInput(QString text)
  - setContext(TranslationContext)  // 源/目标语言、语气、风格、背景、glossary、模式
  - translate()  -> 调 PromptBuilder 构建 prompt -> ApiClient 发请求
  - signals: partialResult(QString), finished(QString), error(QString)
```

### 8.2 翻译流程

1. 源文本变化（含防抖 `auto_translate_delay`）或用户点击"翻译"按钮触发。
2. `PromptBuilder` 按第七节组合 prompt（system 消息可为空，user 消息为拼接结果）。
3. `ApiClient` 发送流式/非流式请求。
4. 流式：`partialResult` 逐步更新译文框（支持中途取消，提供"停止"按钮）。
5. 完成后：写入历史记录（若启用）；保存原文↔译文映射供候选遣词使用。
6. 错误：状态栏显示错误消息。

### 8.3 模式开关（UI 工具栏）

- **保留分隔符** 模式：使用 `delimiters` 模板。
- **结构化数据** 模式：使用 `structured` 模板，`{format_type}` 由简单启发式检测（`{` → json, `<` → html/xml, `#` → markdown）或用户下拉指定。

---

## 九、UI 布局与交互

### 9.1 主窗口 `MainWindow`

```
┌─────────────────────────────────────────────────────┐
│   [≡菜单]                                           │  顶部工具栏
├──────────────────────────┬──────────────────────────┤
│   源语言[▾]            >>> 目标语言[▾]   语气[▾] │
├──────────────────────────┼──────────────────────────┤
│                          │                          │
│   源文本输入框           │   译文显示框             │
│   (QPlainTextEdit)       │   (TranslationEdit)      │
│                          │   支持点击单词弹候选     │
│                          │                          │
├──────────────────────────┼──────────────────────────┤
│ [清空] [粘贴]  字数:123  │ [复制]         模式:[▾] │  底部操作栏
├──────────────────────────┴──────────────────────────┤
│ 状态栏: 就绪 / 翻译中... / 错误信息                 │
└─────────────────────────────────────────────────────┘
```

- 左右双栏可拖拽分隔条调整比例。
- 顶部语言下拉、语气下拉、设置按钮。
- 菜单栏：文件（文档翻译、退出）、编辑（术语表、语气管理、历史）、视图（界面语言、置顶）、工具（剪贴板监听、全局热键设置）、帮助。
- 系统托盘图标 + 右键菜单（显示/隐藏、翻译剪贴板、退出）。

### 9.2 自动翻译

- 防抖定时器：源文本变化后 `auto_translate_delay` 毫秒无新输入则触发翻译。
- 可在设置中开关；UI 有"自动翻译"开关按钮。

---

## 十、候选遣词实现

### 10.1 `TranslationEdit`（自定义 QTextEdit）

- 继承 `QTextEdit`，重写 `mousePressEvent` / `mouseReleaseEvent`。
- 点击时：用 `cursorForPosition` + `select(QTextCursor::WordUnderCursor)` 获取点击的词。
- 该词高亮（`setExtraSelections`）。
- 在点击位置弹出 `CandidatePopup`。

### 10.2 `CandidatePopup`（QFrame / QListWidget）

- 显示加载中提示。
- 调 `TranslationEngine::requestCandidates(sourceText, translatedText, word)`，使用 `prompts.candidate` 模板发送**单独** API 请求。
- 返回结果按行分割为候选列表。
- 用户点击某候选：用 `QTextCursor` 替换译文中的词为该候选，更新内部译文存储。
- 按 Esc 或点击外部关闭。

### 10.3 注意

- 候选请求是非流式的（`stream=false`），快速返回。
- 替换后译文框内容变化，但**不**重新触发主翻译。
- 记录最近一次原文↔译文映射，用于候选请求。

---

## 十一、扩展功能

### 11.1 翻译历史

- `HistoryManager`：每次翻译完成后追加一条 `TranslationRecord{timestamp, source_lang, target_lang, source, target, tone}`。
- 存于 `history.json`（数组），超过 `max_records` 则裁剪头部。
- `HistoryDialog`：列表展示，支持搜索、双击重用（填回主界面）、删除、清空。
- 设置中可开关、调整容量。

### 11.2 系统托盘

- `QSystemTrayIcon`，图标用 `app.svg`。
- 双击切换主窗口可见性。
- 右键菜单：显示/隐藏、翻译剪贴板、暂停剪贴板监听、退出。
- 关闭主窗口时最小化到托盘（可选，设置开关 `minimize_to_tray`）。

### 11.3 全局热键（`platform/GlobalHotkey`）

条件编译封装：

- **Windows**：`RegisterHotKey` / `UnregisterHotKey`，在原生事件过滤器 `nativeEventFilter` 中接收 `WM_HOTKEY`。
- **macOS**：`CGEventTap` 或 Carbon `RegisterEventHotKey` + `EventHandlerCallRef`。
- **Linux/X11**：`XGrabKey`（在 `QX11Info` 或裸 Xlib）；Wayland 下全局热键受限，文档注明。
- 暴露信号 `activated()`，触发时显示/隐藏主窗口（置于屏幕中央并置顶、预填剪贴板内容）。
- 序列解析：`"Ctrl+Alt+T"` → 各平台键码映射。设置页可录制热键。

### 11.4 剪贴板监听

- `QClipboard::dataChanged` 信号 + 防抖 `delay_ms`。
- 检测到变化：取 `QClipboard::text()`，若非空且与上次不同，自动填入源文本框并翻译。
- **避免回环**：当程序自身调用 `QClipboard::setText()`（"复制译文"）时，置标志位跳过本次监听。
- 设置中开关、调整延迟。

### 11.5 文档批量翻译

- 菜单"文件 → 打开文档"，支持 `.txt` / `.md`。
- 读取文件内容，按段落或固定字数分块（每块 ≤ 约 2000 字，保留段落分隔）。
- 顺序翻译各块（或并发有限路数），合并结果。
- "文件 → 导出译文"，保存为同名 `.translated.<ext>`。
- 进度对话框显示 `已完成 N/M 块`。
- 结构化数据模式自动应用于 `.md`/`.json` 等。

---

## 十二、设置对话框 `SettingsDialog`

使用 `QTabWidget`，分页：

1. **API**：base_url、api_key、model、temperature、max_tokens、top_p、stream、extra_body 编辑器（语法校验）。
2. **翻译**：默认源/目标语言、默认语气、自定义语气管理、翻译风格、翻译背景、自动翻译开关与延迟、模式默认值。
3. **术语表**：开关 + 表格编辑（原文 / 译文两列，增删改，支持导入导出 JSON）。
4. **Prompt 模板**：各模板片段的中/英文版多行编辑框，每个遵循默认值回落交互；底部"测试"按钮可输入样例文本预览最终 prompt。
5. **界面**：界面语言（en/zh）、置顶、关闭最小化到托盘、字体大小。
6. **热键**：启用开关、热键录制。
7. **剪贴板**：监听开关、延迟。
8. **历史**：启用开关、最大记录数、立即清空按钮。

所有输入控件遵循第 3.2 节默认值回落交互。

### 术语表 UI 友好设计

- `QTableWidget` 两列：`原文术语` | `译文（留空则不翻译）`。
- 底部按钮：`+ 添加`、`- 删除`、`↑↓ 移动`、`导入 JSON`、`导出 JSON`。
- 顶部搜索框过滤。
- 每行译文列的 placeholder 显示"留空则保留原文不翻译"。

---

## 十三、CMake 构建要点

```cmake
cmake_minimum_required(VERSION 3.21)
project(RiipL LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets Network LinguistTools)

qt6_add_translation(QM_FILES
    ${CMAKE_SOURCE_DIR}/resources/i18n/riip_en.ts
    ${CMAKE_SOURCE_DIR}/resources/i18n/riip_zh.ts)

qt6_add_executable(RiipL
    ${SOURCES} ${HEADERS} ${QM_FILES})

target_link_libraries(RiipL PRIVATE Qt6::Widgets Qt6::Network)
# Linux X11 全局热键
if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    target_link_libraries(RiipL PRIVATE X11::X11)
endif()
```

- `.qm` 文件加入资源或随可执行文件部署。
- Windows 部署用 `windeployqt`；macOS 用 `macdeployqt`。

---

## 十四、实现里程碑

按顺序实现，每阶段确保可编译运行：

1. **骨架**：CMake + Qt6 空窗口 + 资源文件 + main.cpp。验证编译运行。
2. **配置系统**：`Defaults.h`、`ConfigManager`（含默认值回落）、`config.json` 读写。单元测试回落逻辑。
3. **i18n**：`.ts/.qm`、`QTranslator` 安装、`retranslateUi()` 框架、中英切换。
4. **网络层**：`ApiClient` + 模型参数 UI（设置对话框 API 页），手动测试一次 `/chat/completions`。
5. **Prompt 系统**：`PromptBuilder` + 所有默认模板 + 占位符替换 + 模板编辑 UI（含默认值回落交互）。
6. **核心翻译**：主窗口双栏 UI、语言/语气下拉、`TranslationEngine`、流式显示、停止按钮。
7. **术语表 / 风格 / 背景**：`Glossary` 模型 + `GlossaryDialog` + 主窗口风格/背景输入。
8. **候选遣词**：`TranslationEdit` + `CandidatePopup` + 单独 API 请求。
9. **模式开关**：分隔符模式 + 结构化数据模式。
10. **翻译历史**：`HistoryManager` + `history.json` + `HistoryDialog`。
11. **系统托盘**：`QSystemTrayIcon` + 最小化到托盘。
12. **全局热键**：`GlobalHotkey` 三平台条件编译 + 设置录制。
13. **剪贴板监听**：`QClipboard` + 防抖 + 回环规避。
14. **文档批量翻译**：分块 + 进度 + 导出。
15. **打磨**：默认值回落交互全面铺开、错误处理、状态栏、字体、图标、打包部署脚本。

---

## 十五、关键约束与规范

- **仅用 Qt 标准库**，除全局热键的平台原生 API 外不引入第三方依赖。
- **所有用户可见字符串**用 `tr()` 包裹。
- **所有设置**存 JSON；配置项默认值在 `Defaults.h` 硬编码，`config.json` 只存非默认值。
- **每个可配置输入框**实现：placeholder 显示默认值、修改时背景变色、重置按钮按需显示、重置即删除键。
- **prompt 模板**全部支持占位符注入，绝不用硬代码做术语替换——替换工作交给 AI。
- **候选遣词**为独立 API 请求，非流式。
- **不添加任何注释**到源代码，除非有明确必要（agent 默认行为）。
- **跨平台**：避免 `#ifdef` 泛滥，平台差异集中在 `platform/` 目录。
- **错误处理**：网络错误、JSON 解析错误、配置缺失均给出可读提示，不崩溃。
- **密钥**：API key 明文存于 `config.json`（用户已知悉）。
