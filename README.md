# AI PR Reviewer

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows-lightgrey.svg)]()

**AI PR Reviewer** 是一个基于 AI 的 Pull Request 代码评审工具，帮助开发者提升代码审查的效率和代码质量。用户只需指定一个 GitHub PR，工具会自动获取代码变更并调用 OpenAI GPT-4o 进行智能分析，生成一份结构化的 HTML 评审报告。

---

## 功能特性

- **PR 变更自动获取** — 通过 GitHub REST API 获取 PR 的元数据和文件 diff
- **结构化 Diff 解析** — 将 unified diff 格式解析为结构化的代码变更数据
- **AI 智能分析** — 调用 OpenAI GPT-4o 从安全、Bug、性能、风格等多维度分析代码
- **批量处理** — 大 PR 自动拆分为多个批次进行分析，避免 token 超限
- **风险代码识别** — 按严重程度（Critical/High/Medium/Low/Info）分类标注问题
- **HTML 报告生成** — 基于 TDesign 设计规范的现代 HTML 评审报告，支持离线查看
- **灵活配置** — 支持 CLI 参数、环境变量、YAML 配置文件三种配置方式
- **指数退避重试** — API 调用失败自动重试，提高可靠性

---

## 架构设计

```
┌──────────────────────────────────────────────────────────┐
│                    AI PR Reviewer v1.0                     │
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
│  │ (cpprestsdk)│   │  (C++ regex) │   │ (OpenAI GPT-4o)│ │
│  └─────────────┘   └──────────────┘   └───────┬───────┘ │
│                                                │         │
│                                                ▼         │
│                               ┌───────────────────────┐  │
│                               │ HTML Report Generator │  │
│                               │  (inja + TDesign CSS)  │  │
│                               └───────────┬───────────┘  │
│                                           │              │
│                                           ▼              │
│                                  review-report.html      │
└──────────────────────────────────────────────────────────┘
```

### 模块说明

| 模块 | 职责 | 核心技术 |
|------|------|---------|
| CLI Entry | 命令行参数解析、流程编排 | CLI11 |
| Config Manager | 配置文件加载、三级优先级合并 | yaml-cpp |
| GitHub Client | PR 元数据获取、文件 diff 下载 | cpprestsdk |
| Diff Parser | Unified diff 解析、语言检测 | C++ regex |
| AI Analyzer | Prompt 构建、OpenAI API 调用、结果解析 | cpprestsdk + nlohmann/json |
| HTML Report | inja 模板渲染、TDesign 样式 | inja + 内联 CSS |

---

## 依赖清单

### 核心运行时依赖

| 库 | 最低版本 | 用途 | 许可证 |
|----|---------|------|--------|
| **CLI11** | ≥ 2.4.0 | 命令行参数解析 | 3-Clause BSD |
| **yaml-cpp** | ≥ 0.8.0 | YAML 配置文件解析 | MIT |
| **nlohmann/json** | ≥ 3.11.0 | JSON 序列化/反序列化 | MIT |
| **cpprestsdk** | ≥ 2.10.0 | HTTP 客户端（GitHub + OpenAI API） | MIT |
| **spdlog** | ≥ 1.13.0 | 日志系统 | MIT |
| **inja** | ≥ 3.4.0 | HTML 模板引擎 | MIT |

### 构建与测试依赖

| 工具 | 说明 |
|------|------|
| **CMake** | ≥ 3.22 构建系统 |
| **vcpkg** | C++ 包管理器（manifest 模式） |
| **Google Test** | ≥ 1.14.0 单元测试框架 |
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
# 基本用法：分析一个 GitHub PR
.\build\Release\ai-pr-reviewer.exe `
    --api-key "sk-your-key" `
    --pr-url "https://github.com/oceanli010/AI-PR-Reviewer/pull/1" `
    --output "report.html"

# 使用环境变量中的 API Key
$env:OPENAI_API_KEY = "sk-your-key"
.\build\Release\ai-pr-reviewer.exe `
    --pr-url "https://github.com/myorg/myrepo/pull/42" `
    --output "review-results.html" `
    --verbose

# 使用配置文件
.\build\Release\ai-pr-reviewer.exe `
    --config "config.yaml" `
    --pr-url "https://github.com/org/repo/pull/100"

# 使用单独的 owner/repo/number 参数
.\build\Release\ai-pr-reviewer.exe `
    --owner "myorg" --repo "myrepo" --pr-number 42

# 自定义模型和温度参数
.\build\Release\ai-pr-reviewer.exe `
    --pr-url "https://github.com/org/repo/pull/100" `
    --model "gpt-4-turbo" `
    --temperature 0.5 `
    --max-files-per-batch 5
```

### 命令行参数说明

```
Usage: ai-pr-reviewer [OPTIONS]

PR Source (choose one):
  -u, --pr-url TEXT           GitHub PR URL
  -o, --owner TEXT            Repository owner
  -r, --repo TEXT             Repository name
  -n, --pr-number INT         PR number

OpenAI Settings:
  -k, --api-key TEXT          OpenAI API key [$OPENAI_API_KEY]
  -m, --model TEXT            Model name [default: gpt-4o]
  --base-url TEXT             API base URL [default: https://api.openai.com/v1]
  --temperature FLOAT         Response temperature [default: 0.3]

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

## 项目结构

```
AI-PR-Reviewer/
├── CMakeLists.txt                    # 构建配置
├── vcpkg.json                        # vcpkg 依赖清单
├── config.example.yaml               # 示例配置文件
├── README.md                         # 项目文档
├── LICENSE                           # MIT 许可证
├── include/
│   └── ai_pr_reviewer/
│       ├── types.h                   # 公共数据类型定义
│       ├── config.h                  # 配置管理接口
│       ├── github_client.h           # GitHub API 客户端接口
│       ├── diff_parser.h             # Diff 解析器接口
│       ├── ai_analyzer.h             # AI 分析引擎接口
│       └── html_report.h             # HTML 报告生成器接口
├── src/
│   ├── main.cpp                      # CLI 入口与流程编排
│   ├── config.cpp                    # 配置管理实现
│   ├── github_client.cpp             # GitHub API 客户端实现
│   ├── diff_parser.cpp               # Diff 解析器实现
│   ├── ai_analyzer.cpp               # AI 分析引擎实现
│   └── html_report.cpp               # HTML 报告生成器实现
├── templates/
│   └── report.html                   # inja HTML 报告模板
├── tests/
│   ├── test_diff_parser.cpp          # Diff 解析器单元测试
│   ├── test_config.cpp               # 配置管理单元测试
│   ├── test_github_client.cpp        # GitHub 客户端测试
│   └── test_ai_analyzer.cpp          # AI 分析引擎测试
└── packaging/                        # Inno Setup 安装包脚本（待添加）
```

---

## 设计思路

### 模型选择

选用 **OpenAI GPT-4o** 作为核心分析引擎，理由如下：
- **代码理解能力强**: GPT-4o 在代码理解和分析任务上表现优异，能准确识别安全漏洞、逻辑错误、性能问题
- **结构化输出**: 支持 JSON mode，确保分析结果格式统一可解析
- **上下文窗口大**: 128K token 上下文窗口，可处理大部分 PR 中的所有文件变更
- **API 成熟稳定**: OpenAI API 文档完善，SDK 生态丰富

### 上下文获取方式

系统通过以下策略最大化 AI 对代码变更的理解：

1. **完整 Diff 上下文**: 不仅获取变更行，还保留 `git diff` 的上下文行（默认前后 3 行），帮助 AI 理解代码的来龙去脉
2. **结构化元数据**: 提供 PR 标题、描述、作者、分支信息等元数据，让 AI 了解变更的业务背景
3. **语言检测**: 自动检测每个文件使用的编程语言，使 AI 能应用特定语言的评审规则
4. **批量分片**: 对于超大 PR，自动将文件拆分为多个批次分别分析，最后合并结果
5. **Prompt 工程**: 精心设计的系统 Prompt 定义了评审维度、输出格式、严重程度标准，确保评审质量一致

### 未来扩展方向

1. **多模型支持**: 支持 Claude、DeepSeek、本地 Ollama 模型，让用户灵活选择
2. **增量评审**: 针对同一 PR 的后续 push，只评审增量变化，减少 token 消耗
3. **自定义规则**: 支持用户配置项目级别的评审规则文件（如 `.pr-review-rules.yaml`）
4. **PR 评论自动发布**: 将评审结果自动作为 PR Review Comments 发布到 GitHub
5. **历史趋势分析**: 追踪多次 PR 评审结果，分析代码质量变化趋势
6. **CI/CD 集成**: 提供 GitHub Actions / Jenkins 插件，在 CI 流水线中自动执行
7. **多仓库支持**: 支持 GitLab、Bitbucket 等其他代码托管平台
8. **GUI 界面**: 基于 Qt 或 Electron 开发图形化客户端

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
