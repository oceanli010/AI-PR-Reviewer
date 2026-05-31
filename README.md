# AI PR Reviewer

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows-lightgrey.svg)]()

### 项目演示

[![项目演示视频](https://img.shields.io/badge/Bilibili-演示视频-00A1D6?style=flat&logo=bilibili)](https://www.bilibili.com/video/BV1iNVJ6mEPh)

**AI PR Reviewer** 是一个基于 AI 的 Pull Request 代码评审工具，帮助开发者提升代码审查的效率和代码质量。支持多 AI 模型切换（OpenAI/DeepSeek/本地模型等），可通过 GitHub Token 自动发现仓库和 PR，实现端到端的智能代码评审体验。

---

## 功能特性

- **PR 变更自动获取** — 通过 GitHub REST API 获取 PR 的元数据和文件 diff
- **多 AI 模型支持** — 通过配置文件配置多个 AI 提供商（OpenAI、DeepSeek、Azure、Ollama 等），自由切换
- **仓库与 PR 发现** — 使用 GitHub Token 自动列出可访问仓库和 PR，支持交互式选择（`--discover`）
- **结构化 Diff 解析** — 将 unified diff 格式解析为结构化的代码变更数据
- **AI 智能分析** — 从安全、Bug、性能、风格等多维度分析代码
- **批量处理** — 大 PR 自动拆分为多个批次进行分析，避免 token 超限
- **风险代码识别** — 按严重程度（Critical/High/Medium/Low/Info）分类标注问题
- **HTML 报告生成** — 受 TDesign 设计规范启发的现代 HTML 评审报告（纯内联 CSS，零 CDN 依赖，支持离线查看）
- **灵活配置** — 支持 CLI 参数、环境变量、YAML 配置文件三种配置方式
- **指数退避重试** — API 调用失败自动重试，提高可靠性

---

## 架构设计

```
┌──────────────────────────────────────────────────────────┐
│                    AI PR Reviewer v1.1                     │
├──────────────────────────────────────────────────────────┤
│                                                          │
│  CLI Entry (CLI11)                                       │
│      │                                                   │
│      ▼                                                   │
│  Config Manager (yaml-cpp + env vars)                    │
│      │                                                   │
│      ▼                                                   │
│  ┌─────────────┐   ┌──────────────┐   ┌───────────────┐ │
│  │ GitHub API  │──▶│ Diff Parser  │──▶│ AI Analyzer   │ │
│  │ (cpprestsdk)│   │  (C++ regex) │   │ (Multi-model) │ │
│  └─────────────┘   └──────────────┘   └───────┬───────┘ │
│                                                │         │
│                                                ▼         │
│                               ┌───────────────────────┐  │
│                               │ HTML Report Generator │  │
│                               │  (inja + inline CSS)  │  │
│                               └───────────┬───────────┘  │
│                                           │              │
│                                           ▼              │
│                                  review-report.html      │
└──────────────────────────────────────────────────────────┘
```

### 模块说明

| 模块 | 职责 | 核心技术 |
|------|------|---------|
| CLI Entry | 命令行参数解析、流程编排（含交互式发现模式） | CLI11 |
| Config Manager | 配置文件加载、多 AI Provider 管理、三级优先级合并 | yaml-cpp |
| GitHub Client | PR 元数据获取、仓库/PR 发现、文件 diff 下载 | cpprestsdk |
| Diff Parser | Unified diff 解析、语言检测（40+ 语言） | C++ regex |
| AI Analyzer | Prompt 构建、多模型 API 调用（兼容 OpenAI API）、结果解析 | cpprestsdk + nlohmann/json |
| HTML Report | inja 模板渲染、Markdown 渲染、Diff 视图、内联 CSS（受 TDesign 启发，零 CDN） | inja + 内联 CSS |

---

## 依赖清单

### 核心运行时依赖

| 库 | 最低版本 | 用途 | 许可证 |
|----|---------|------|--------|
| **CLI11** | ≥ 2.6.2 | 命令行参数解析 | 3-Clause BSD |
| **yaml-cpp** | ≥ 0.9.0 | YAML 配置文件解析 | MIT |
| **nlohmann/json** | ≥ 3.12.0 | JSON 序列化/反序列化 | MIT |
| **cpprestsdk** | ≥ 2.10.19 | HTTP 客户端（GitHub + OpenAI API） | MIT |
| **spdlog** | ≥ 1.17.0 | 日志系统 | MIT |
| **inja** | ≥ 3.5.0 | HTML 模板引擎 | MIT |

### 构建与测试依赖

| 工具 | 说明 |
|------|------|
| **CMake** | ≥ 3.22 构建系统 |
| **vcpkg** | C++ 包管理器（manifest 模式） |
| **Google Test** | ≥ 1.17.0 单元测试框架 |
| **Visual Studio 2022** | Windows 编译工具链（MSVC） |

### 依赖冲突排查

| 依赖对 | 兼容性 | 说明 |
|--------|--------|------|
| nlohmann/json ↔ inja | ✅ 兼容 | inja 依赖 nlohmann/json |
| cpprestsdk ↔ WinHTTP | ✅ 兼容 | 默认 WinHTTP 后端，无需 libcurl |
| spdlog ↔ fmt | ✅ 兼容 | spdlog 内置 fmt 支持 |
| yaml-cpp ↔ 所有库 | ✅ 兼容 | 零外部依赖 |
| CLI11 ↔ 所有库 | ✅ 兼容 | 纯头文件库 |

**结论**: 所选依赖链无已知版本冲突，所有库均可通过 vcpkg 统一管理。

### 外部服务依赖（运行时）

以下外部 API 服务是程序运行的必要条件，需确保网络可达：

| 服务 | 端点 | 用途 | 说明 |
|------|------|------|------|
| **GitHub REST API v3** | `https://api.github.com` | 获取 PR 元数据、文件 Diff、仓库/PR 列表 | 匿名访问限 60 次/小时，建议配置 GitHub Token 提升至 5000 次/小时 |
| **OpenAI 兼容 API** | 可配置（默认 `https://api.openai.com/v1`） | AI 代码评审分析 | 支持任意兼容 `/v1/chat/completions` 端点的服务 |

---

## 快速开始

### 前置条件

1. **Windows 10/11** 操作系统
2. **Visual Studio 2022**（含 C++ 桌面开发工作负载）
3. **CMake** ≥ 3.22
4. **vcpkg**（配置环境变量 `VCPKG_ROOT`）
5. **OpenAI API Key**（从 https://platform.openai.com 获取）

### 构建步骤

```powershell
# 1. 克隆仓库
git clone https://github.com/oceanli010/AI-PR-Reviewer.git
cd AI-PR-Reviewer

# 2. 使用 vcpkg 安装依赖（manifest 模式）
vcpkg install --triplet x64-windows

# 3. CMake 配置与构建
cmake --preset default
cmake --build --preset default --config Release

# 4. 构建测试（可选）
cmake --build --preset default --config Release --target ai_pr_reviewer_tests
```

> **提示**: 如果未配置 CMake presets，可以手动执行：
> ```powershell
> mkdir build; cd build
> cmake .. -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
> cmake --build . --config Release
> ```

### 配置

#### 方式一：环境变量

```powershell
$env:OPENAI_API_KEY = "sk-your-api-key-here"
$env:GITHUB_TOKEN = "ghp_your_github_token"  # 可选，提升 API 频率限制
```

#### 方式二：YAML 配置文件

```powershell
Copy-Item config.example.yaml config.yaml
# 编辑 config.yaml 填入你的 API Key
```

#### 方式三：命令行参数

```powershell
ai-pr-reviewer --api-key "sk-xxx" --pr-url "https://github.com/owner/repo/pull/123"
```

### 使用示例

```powershell
# === 方式一：交互式发现模式（推荐） ===
# 使用 GitHub Token 自动列出仓库和 PR，交互选择
$env:GITHUB_TOKEN = "ghp_your_token"
.\ai-pr-reviewer.exe --discover

# 带过滤条件的发现模式
.\ai-pr-reviewer.exe --discover --repo-filter "AI" --pr-state "open"

# === 方式二：列出仓库 ===
.\ai-pr-reviewer.exe --list-repos --github-token "ghp_xxx"
.\ai-pr-reviewer.exe --list-repos --repo-filter "backend"

# === 方式三：直接指定 PR ===
# 基本用法
.\ai-pr-reviewer.exe --api-key "sk-your-key" `
    --pr-url "https://github.com/oceanli010/AI-PR-Reviewer/pull/1"

# 使用单独的 owner/repo/number 参数
.\ai-pr-reviewer.exe --owner "myorg" --repo "myrepo" --pr-number 42

# 切换 AI 提供商（使用配置文件中的第 2 个 provider）
.\ai-pr-reviewer.exe --config "config.yaml" --provider 1 `
    --pr-url "https://github.com/org/repo/pull/100"

# 使用本地 Ollama 模型
.\ai-pr-reviewer.exe --api-key "ollama" `
    --base-url "http://localhost:11434/v1" `
    --model "qwen2.5:7b" `
    --pr-url "https://github.com/org/repo/pull/100"

# === 方式四：配置文件 ===
.\ai-pr-reviewer.exe --config "config.yaml" `
    --pr-url "https://github.com/org/repo/pull/100"
```

### 命令行参数说明

```
Usage: ai-pr-reviewer [OPTIONS]

PR Source (choose one):
  -u, --pr-url TEXT           GitHub PR URL
  -o, --owner TEXT            Repository owner
  -r, --repo TEXT             Repository name
  -n, --pr-number INT         PR number
  -D, --discover              Interactive: discover repos & PRs via GitHub token
  -L, --list-repos            List accessible repositories
      --repo-filter TEXT      Filter repos by name pattern
      --pr-state TEXT         Filter PRs by state [default: open]

Discovery Mode:
  -D, --discover              Interactive mode: browse repos and PRs
      --repo-filter TEXT      Filter repos by name (e.g. "backend", "AI")
      --pr-state TEXT         PR state filter: open, closed, all [default: open]

AI Provider Settings:
  -k, --api-key TEXT          AI API key [$OPENAI_API_KEY]
  -m, --model TEXT            Model name [default: gpt-4o]
      --base-url TEXT         API base URL [default: https://api.openai.com/v1]
  -p, --provider INT          Select AI provider index from config [default: 0]
      --temperature FLOAT     Response temperature [default: 0.3]

GitHub Settings:
  --github-token TEXT         GitHub PAT [$GITHUB_TOKEN]

Output:
  -O, --output TEXT           HTML report output path [default: review-report.html]
  -t, --template TEXT         Custom HTML template path

Config:
  -c, --config TEXT           YAML config file path

Runtime:
  -v, --verbose               Enable verbose logging
  -q, --quiet                 Suppress all output except errors
  --retry INT                 API retry attempts [default: 3]
  --max-files-per-batch INT   Max files per AI batch [default: 10]
  -V, --version               Print version
```

---

### 多 AI 模型配置

在 `config.yaml` 中配置多个 AI 提供商，通过配置文件或 `--provider N` 参数切换：

```yaml
ai:
  providers:
    - name: "OpenAI GPT-4o"
      model: "gpt-4o"
      base_url: "https://api.openai.com/v1"
      api_key: "${OPENAI_API_KEY}"    # 支持 ${ENV_VAR} 环境变量引用
      temperature: 0.3
      max_tokens: 4096
      default: true                    # 默认使用此提供商

    - name: "DeepSeek V3"
      model: "deepseek-chat"
      base_url: "https://api.deepseek.com/v1"
      api_key: "${DEEPSEEK_API_KEY}"
      temperature: 0.3
      max_tokens: 4096

    - name: "Local Ollama"
      model: "qwen2.5:7b"
      base_url: "http://localhost:11434/v1"
      api_key: "ollama"
      temperature: 0.3
      max_tokens: 2048
```

支持的 AI 提供商：
- **OpenAI** — GPT-4o, GPT-4-turbo 等
- **DeepSeek** — deepseek-chat, deepseek-reasoner
- **Azure OpenAI** — 企业级部署
- **Ollama / LM Studio** — 本地部署模型（兼容 OpenAI API）
- **任何 OpenAI API 兼容服务** — 只要支持 `/v1/chat/completions` 端点

---

## 项目结构

```
AI-PR-Reviewer/
├── CMakeLists.txt                    # 构建配置
├── CMakePresets.json                 # CMake 预设配置（VS2022 + vcpkg）
├── vcpkg.json                        # vcpkg 依赖清单（manifest 模式）
├── config.example.yaml               # 多模型配置示例文件
├── README.md                         # 项目文档
├── LICENSE                           # MIT 许可证
├── include/
│   └── ai_pr_reviewer/
│       ├── types.h                   # 公共数据类型（含 AiProvider、RepoInfo 等）
│       ├── config.h                  # 配置管理接口
│       ├── github_client.h           # GitHub API 客户端接口（含仓库/PR 发现）
│       ├── diff_parser.h             # Diff 解析器接口
│       ├── ai_analyzer.h             # AI 分析引擎接口（多模型支持）
│       └── html_report.h             # HTML 报告生成器接口（含 Diff 视图）
├── src/
│   ├── main.cpp                      # CLI 入口（含交互式发现模式、防闪退）
│   ├── config.cpp                    # 配置管理（含多 Provider 解析、自动发现配置）
│   ├── github_client.cpp             # GitHub API 客户端（含仓库/PR 列表、增强错误诊断）
│   ├── diff_parser.cpp               # Diff 解析器（40+ 语言检测）
│   ├── ai_analyzer.cpp               # AI 分析引擎（多模型路由）
│   └── html_report.cpp               # HTML 报告生成器（Markdown 渲染 + Diff 视图）
├── templates/
│   └── report.html                   # inja HTML 报告模板（TDesign 风格）
└── tests/
    ├── test_diff_parser.cpp          # Diff 解析器单元测试
    ├── test_config.cpp               # 配置管理单元测试
    ├── test_github_client.cpp        # GitHub 客户端测试
    └── test_ai_analyzer.cpp          # AI 分析引擎测试
```

---

## 设计思路

### 模型选择

系统采用**多 AI 模型可配置**架构，默认推荐 **OpenAI GPT-4o**，同时支持任意兼容 OpenAI API 的模型服务：

- **代码理解能力强**: GPT-4o 在代码理解和分析任务上表现优异，能准确识别安全漏洞、逻辑错误、性能问题
- **多模型热切换**: 支持通过配置文件或命令行参数在 OpenAI / DeepSeek / Azure / Ollama / 本地模型之间切换
- **结构化输出**: 支持 JSON mode，确保分析结果格式统一可解析
- **上下文窗口大**: 主流模型具备 128K+ token 上下文窗口，可处理大部分 PR 中的所有文件变更
- **API 生态兼容**: 所有兼容 OpenAI `/v1/chat/completions` 端点的服务均可接入

### 上下文获取方式

系统通过以下策略最大化 AI 对代码变更的理解：

1. **完整 Diff 上下文**: 不仅获取变更行，还保留 `git diff` 的上下文行（默认前后 3 行），帮助 AI 理解代码的来龙去脉
2. **结构化元数据**: 提供 PR 标题、描述、作者、分支信息等元数据，让 AI 了解变更的业务背景
3. **语言检测**: 自动检测每个文件使用的编程语言，使 AI 能应用特定语言的评审规则
4. **批量分片**: 对于超大 PR，自动将文件拆分为多个批次分别分析，最后合并结果
5. **Prompt 工程**: 精心设计的系统 Prompt 定义了评审维度、输出格式、严重程度标准，确保评审质量一致

### 未来扩展方向

1. **增量评审**: 针对同一 PR 的后续 push，只评审增量变化，减少 token 消耗
2. **自定义规则**: 支持用户配置项目级别的评审规则文件（如 `.pr-review-rules.yaml`）
3. **PR 评论自动发布**: 将评审结果自动作为 PR Review Comments 发布到 GitHub
4. **历史趋势分析**: 追踪多次 PR 评审结果，分析代码质量变化趋势
5. **CI/CD 集成**: 提供 GitHub Actions / Jenkins 等 CI 插件，在流水线中自动执行
6. **多平台支持**: 扩展至 GitLab、Bitbucket 等其他代码托管平台
7. **GUI 界面**: 基于 Qt 或 Electron 开发图形化客户端

---

## 原创实现说明

本项目所有核心功能均为独立开发实现，**未引入任何未声明的第三方库或框架**。以下功能模块为原创开发，不依赖第三方现成方案：

| 功能模块 | 位置 | 实现说明 |
|----------|------|----------|
| **Unified Diff 解析器** | `src/diff_parser.cpp` | 基于 C++ `std::regex` 自研，支持 40+ 编程语言自动检测，逐行解析 Hunks 与行号映射 |
| **Markdown → HTML 渲染器** | `src/html_report.cpp` (`markdown_to_html`) | 自研轻量级转换器，支持标题、列表、代码块、行内代码、链接、粗体等格式，无第三方 Markdown 库 |
| **Diff 视图生成器** | `src/html_report.cpp` (`build_diff_data`) | 生成色彩编码的代码 Diff HTML 视图，支持添加/删除/上下文行的精确着色与截断 |
| **HTML 报告模板** | `templates/report.html` + `src/html_report.cpp` 内置模板 | 受 TDesign 设计规范启发，全部采用内联 CSS，零外部 CDN 引用，完全可离线使用 |
| **多 AI Provider 路由** | `src/ai_analyzer.cpp` + `src/config.cpp` | 支持通过配置文件热切换 OpenAI / DeepSeek / Azure / Ollama 等多种 AI 模型 |
| **交互式发现模式** | `src/main.cpp` | 基于 GitHub Token 列出仓库与 PR 的终端交互界面，支持名称过滤和状态筛选 |
| **配置自动发现** | `src/config.cpp` | 智能搜索 exe 目录 → 上级目录 → 当前工作目录，自动加载配置文件 |
| **指数退避重试** | `src/ai_analyzer.cpp` + `src/github_client.cpp` | API 调用失败后的自动重试机制，配合递增延迟策略 |
| **Prompt 工程** | `src/ai_analyzer.cpp` | 精心设计的系统 Prompt，定义五级严重度标准和多维度评审框架 |

> HTML 模板 HTML/CSS 均为手写实现，未使用 Bootstrap、Tailwind、TDesign 等第三方 CSS 框架库。

---

## 测试

```powershell
# 构建测试
cmake --build build --config Release --target ai_pr_reviewer_tests

# 运行测试
cd build
ctest --output-on-failure -C Release

# 运行特定测试套件
./tests/Release/ai_pr_reviewer_tests.exe --gtest_filter="DiffParserTest.*"
```

---

## 打包分发

项目使用 CMake + CPack + NSIS 生成 Windows 安装包：

```powershell
# 构建 Release 版本
cmake --build build --config Release

# 生成安装包
cd build
cpack -C Release
# 会在 build/ 目录下生成 AI-PR-Reviewer-1.0.0-win64.exe 安装包
```

---

## 许可证

MIT License - 详见 [LICENSE](LICENSE)

---

## 贡献指南

欢迎提交 Issue 和 Pull Request！提交时请遵循：

1. 每个 PR 只做一件事
2. PR 标题清晰描述变更内容
3. PR 描述包含：功能说明、实现思路、测试方式
4. 确保主分支保持可运行状态
