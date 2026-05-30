#include "ai_pr_reviewer/config.h"

#include <CLI/CLI.hpp>
#include <yaml-cpp/yaml.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <regex>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <algorithm>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace ai_pr_reviewer {

// ============================================================================
// Public API
// ============================================================================
AppConfig ConfigManager::load(int argc, char* argv[]) {
    CLI::App cli_app{"AI PR Reviewer - Intelligent Pull Request Code Review Tool"};
    cli_app.set_version_flag("--version,-V", "1.0.0");

    AppConfig config;

    // --- PR Source Options ---
    std::string pr_url;
    cli_app.add_option("--pr-url,-u", pr_url,
        "GitHub PR URL (e.g., https://github.com/owner/repo/pull/123)");

    std::string owner;
    cli_app.add_option("--owner,-o", owner,
        "Repository owner (alternative to --pr-url)");

    std::string repo;
    cli_app.add_option("--repo,-r", repo,
        "Repository name (alternative to --pr-url)");

    int pr_number = 0;
    cli_app.add_option("--pr-number,-n", pr_number,
        "PR number (alternative to --pr-url)");

    // --- OpenAI Options ---
    cli_app.add_option("--api-key,-k", config.openai_api_key,
        "OpenAI API key (or set OPENAI_API_KEY env var)")
        ->envname("OPENAI_API_KEY");

    cli_app.add_option("--model,-m", config.openai_model,
        "OpenAI model name (default: gpt-4o)")
        ->default_val("gpt-4o");

    cli_app.add_option("--base-url", config.openai_base_url,
        "OpenAI API base URL")
        ->default_val("https://api.openai.com/v1");

    cli_app.add_option("--temperature", config.temperature,
        "Response temperature (0.0-1.0)")
        ->default_val(0.3f);

    // --- GitHub Options ---
    cli_app.add_option("--github-token", config.github_token,
        "GitHub personal access token (or set GITHUB_TOKEN env var)")
        ->envname("GITHUB_TOKEN");

    // --- Discovery Mode ---
    cli_app.add_flag("--discover,-D", config.discover_mode,
        "Interactive mode: discover repos and PRs via GitHub token");

    cli_app.add_flag("--list-repos,-L", config.list_repos,
        "List accessible repositories (requires --github-token)");

    cli_app.add_option("--repo-filter", config.repo_filter,
        "Filter repositories by name pattern (supports wildcard)");

    cli_app.add_option("--pr-state", config.pr_state_filter,
        "Filter PRs by state (open, closed, all)")
        ->default_val("open");

    cli_app.add_option("--provider,-p", config.selected_provider_index,
        "Select AI provider by index (0-based, from config file)")
        ->default_val(0);

    // --- Output Options ---
    cli_app.add_option("--output,-O", config.output_path,
        "Output HTML report path")
        ->default_val("review-report.html");

    cli_app.add_option("--template,-t", config.template_path,
        "Custom HTML template path");

    // --- Config File ---
    cli_app.add_option("--config,-c", config.config_file,
        "YAML configuration file path");

    // --- Runtime Flags ---
    cli_app.add_flag("--verbose,-v", config.verbose,
        "Enable verbose logging");
    cli_app.add_flag("--quiet,-q", config.quiet,
        "Suppress all output except errors");

    cli_app.add_option("--retry", config.retry_count,
        "Number of API retry attempts")
        ->default_val(3);

    cli_app.add_option("--max-files-per-batch", config.max_files_per_batch,
        "Max files per AI analysis batch")
        ->default_val(10);

    // --- Parse ---
    try {
        cli_app.parse(argc, argv);
    } catch (const CLI::CallForHelp& e) {
        std::exit(cli_app.exit(e));
    } catch (const CLI::CallForVersion& e) {
        std::exit(cli_app.exit(e));
    } catch (const CLI::Error& e) {
        SPDLOG_ERROR("Failed to parse command line arguments: {}", e.what());
        std::exit(cli_app.exit(e));
    }

    // --- Apply PR URL or individual params ---
    if (!pr_url.empty()) {
        config.pr_url = pr_url;
        if (!parse_pr_url(pr_url, config.owner, config.repo, config.pr_number)) {
            throw std::runtime_error("Invalid PR URL format: " + pr_url +
                "\nExpected: https://github.com/{owner}/{repo}/pull/{number}");
        }
    } else {
        config.owner = owner;
        config.repo = repo;
        config.pr_number = pr_number;
    }

    // --- Load config file: explicit path > auto-search > skip ---
    if (!config.config_file.empty()) {
        load_from_file(config, config.config_file);
    } else {
        // Auto-search config.yaml in: exe dir > project root > CWD
        std::vector<std::string> search_paths;

        // 1. Executable directory
#ifdef _WIN32
        char exe_path[MAX_PATH];
        DWORD len = GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            std::string exe_dir(exe_path, len);
            auto last_slash = exe_dir.find_last_of("\\/");
            if (last_slash != std::string::npos) {
                exe_dir = exe_dir.substr(0, last_slash);
                search_paths.push_back(exe_dir + "\\config.yaml");
                // 2. Parent directory (project root when running from build/Release/)
                auto penultimate = exe_dir.find_last_of("\\/", last_slash - 1);
                if (penultimate != std::string::npos) {
                    search_paths.push_back(exe_dir.substr(0, penultimate) + "\\config.yaml");
                }
            }
        }
#endif
        // 3. Current working directory
        search_paths.push_back("config.yaml");

        for (const auto& path : search_paths) {
            std::ifstream auto_cfg(path);
            if (auto_cfg.good()) {
                auto_cfg.close();
                spdlog::debug("Auto-detected config: {}", path);
                load_from_file(config, path);
                break;
            }
        }
    }

    // --- Apply environment variable overrides ---
    apply_env_overrides(config);

    // --- Validate ---
    validate(config);

    if (config.verbose) {
        spdlog::set_level(spdlog::level::debug);
        spdlog::debug("Verbose logging enabled");
    } else if (config.quiet) {
        spdlog::set_level(spdlog::level::err);
    }

    return config;
}

// ============================================================================
// PR URL Parsing
// ============================================================================
bool ConfigManager::parse_pr_url(const std::string& url,
                                  std::string& owner,
                                  std::string& repo,
                                  int& pr_number) {
    // Pattern: https://github.com/{owner}/{repo}/pull/{number}
    std::regex pattern(
        R"(^https?://github\.com/([^/]+)/([^/]+)/pull/(\d+)/?.*$)",
        std::regex::icase
    );

    std::smatch matches;
    if (std::regex_match(url, matches, pattern)) {
        owner = matches[1].str();
        repo = matches[2].str();
        pr_number = std::stoi(matches[3].str());
        return true;
    }

    return false;
}

// ============================================================================
// YAML Config Loading
// ============================================================================
void ConfigManager::load_from_file(AppConfig& config, const std::string& filepath) {
    try {
        YAML::Node root = YAML::LoadFile(filepath);

        // --- AI Providers section (new multi-model support) ---
        if (root["ai"] && root["ai"]["providers"] && root["ai"]["providers"].IsSequence()) {
            config.ai_providers.clear();
            int idx = 0;
            for (const auto& prov_node : root["ai"]["providers"]) {
                AiProvider provider;
                provider.name = prov_node["name"] ? prov_node["name"].as<std::string>()
                    : ("Provider-" + std::to_string(idx));
                provider.model = prov_node["model"] ? prov_node["model"].as<std::string>()
                    : "gpt-4o";
                provider.base_url = prov_node["base_url"] ? prov_node["base_url"].as<std::string>()
                    : "https://api.openai.com/v1";

                // API key: explicit value or env var reference
                if (prov_node["api_key"]) {
                    std::string key = prov_node["api_key"].as<std::string>();
                    // Support ${ENV_VAR} syntax for env var references
                    if (key.size() > 3 && key[0] == '$' && key[1] == '{' && key.back() == '}') {
                        std::string env_name = key.substr(2, key.size() - 3);
                        const char* env_val = std::getenv(env_name.c_str());
                        provider.api_key = env_val ? env_val : "";
                    } else {
                        provider.api_key = key;
                    }
                }

                if (prov_node["temperature"]) provider.temperature = prov_node["temperature"].as<float>();
                if (prov_node["max_tokens"]) provider.max_tokens = prov_node["max_tokens"].as<int>();
                if (prov_node["timeout_seconds"]) provider.timeout_seconds = prov_node["timeout_seconds"].as<int>();
                if (prov_node["default"] && prov_node["default"].as<bool>()) {
                    provider.is_default = true;
                    config.selected_provider_index = idx;
                }

                config.ai_providers.push_back(std::move(provider));
                idx++;
            }
            spdlog::info("Loaded {} AI provider(s) from config", config.ai_providers.size());
        }

        // Backward compatibility: legacy openai section
        if (root["openai"]) {
            auto openai = root["openai"];
            if (openai["api_key"] && config.openai_api_key.empty()) {
                config.openai_api_key = openai["api_key"].as<std::string>();
            }
            if (openai["model"]) {
                config.openai_model = openai["model"].as<std::string>();
            }
            if (openai["base_url"]) {
                config.openai_base_url = openai["base_url"].as<std::string>();
            }
            if (openai["temperature"]) {
                config.temperature = openai["temperature"].as<float>();
            }
            if (openai["max_tokens"]) {
                config.max_tokens = openai["max_tokens"].as<int>();
            }
            if (openai["timeout_seconds"]) {
                config.context_timeout_seconds = openai["timeout_seconds"].as<int>();
            }
        }

        // GitHub section
        if (root["github"]) {
            auto github = root["github"];
            if (github["token"] && config.github_token.empty()) {
                config.github_token = github["token"].as<std::string>();
            }
        }

        // Output section
        if (root["output"]) {
            auto output = root["output"];
            if (output["path"]) {
                config.output_path = output["path"].as<std::string>();
            }
            if (output["template"] && config.template_path.empty()) {
                config.template_path = output["template"].as<std::string>();
            }
        }

        // Runtime section
        if (root["runtime"]) {
            auto runtime = root["runtime"];
            if (runtime["retry_count"]) {
                config.retry_count = runtime["retry_count"].as<int>();
            }
            if (runtime["retry_delay_ms"]) {
                config.retry_delay_ms = runtime["retry_delay_ms"].as<int>();
            }
            if (runtime["max_files_per_batch"]) {
                config.max_files_per_batch = runtime["max_files_per_batch"].as<int>();
            }
            if (runtime["verbose"]) {
                config.verbose = runtime["verbose"].as<bool>();
            }
            if (runtime["quiet"]) {
                config.quiet = runtime["quiet"].as<bool>();
            }
        }

        spdlog::info("Loaded configuration from: {}", filepath);
    } catch (const YAML::Exception& e) {
        throw std::runtime_error("Failed to parse config file '" + filepath + "': " + e.what());
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to load config file '" + filepath + "': " + e.what());
    }
}

// ============================================================================
// Environment Variable Overrides
// ============================================================================
void ConfigManager::apply_env_overrides(AppConfig& config) {
    // OPENAI_API_KEY
    const char* env_key = std::getenv("OPENAI_API_KEY");
    if (env_key && config.openai_api_key.empty()) {
        config.openai_api_key = env_key;
    }

    // OPENAI_MODEL
    const char* env_model = std::getenv("OPENAI_MODEL");
    if (env_model) {
        config.openai_model = env_model;
    }

    // OPENAI_BASE_URL
    const char* env_base_url = std::getenv("OPENAI_BASE_URL");
    if (env_base_url) {
        config.openai_base_url = env_base_url;
    }

    // GITHUB_TOKEN
    const char* env_gh_token = std::getenv("GITHUB_TOKEN");
    if (env_gh_token && config.github_token.empty()) {
        config.github_token = env_gh_token;
    }
}

// ============================================================================
// Validation
// ============================================================================
void ConfigManager::validate(AppConfig& config) {
    // --- Discovery mode: requires GitHub token ---
    if (config.discover_mode) {
        if (config.github_token.empty()) {
            throw std::runtime_error(
                "Discovery mode requires a GitHub token. Provide it via:\n"
                "  --github-token YOUR_TOKEN\n"
                "  GITHUB_TOKEN environment variable\n"
                "  config.yaml 'github.token' field"
            );
        }
        spdlog::info("Discovery mode enabled - will list repos and PRs");
        // Don't validate PR source - will be discovered
    }
    // --- List repos mode ---
    else if (config.list_repos) {
        if (config.github_token.empty()) {
            throw std::runtime_error(
                "Listing repositories requires a GitHub token. Provide it via:\n"
                "  --github-token YOUR_TOKEN\n"
                "  GITHUB_TOKEN environment variable"
            );
        }
        // Don't validate PR source
    }
    // --- Standard PR review mode ---
    else {
        if (config.owner.empty() || config.repo.empty() || config.pr_number <= 0) {
            throw std::runtime_error(
                "Missing PR information. Provide one of:\n"
                "  --pr-url https://github.com/owner/repo/pull/123\n"
                "  --owner OWNER --repo REPO --pr-number 123\n"
                "  --discover (to find repos and PRs interactively)"
            );
        }
    }

    // Check required: API key (from provider or legacy)
    auto active = config.active_provider();
    if (active.api_key.empty()) {
        std::string provider_hint;
        if (!config.ai_providers.empty()) {
            int idx = config.selected_provider_index;
            if (idx >= 0 && idx < static_cast<int>(config.ai_providers.size())) {
                provider_hint = " (active provider: " + config.ai_providers[idx].name +
                                ", model: " + config.ai_providers[idx].model + ")";
            }
        }
        throw std::runtime_error(
            "AI API key is required" + provider_hint + ".\n"
            "Provide it via:\n"
            "  --api-key YOUR_KEY\n"
            "  set OPENAI_API_KEY=YOUR_KEY    (environment variable on CMD)\n"
            "  $env:OPENAI_API_KEY = 'YOUR_KEY'  (environment variable on PowerShell)\n"
            "  Or fill api_key in config.yaml 'ai.providers[].api_key' field"
        );
    }

    // Validate temperature range
    if (config.temperature < 0.0f || config.temperature > 2.0f) {
        spdlog::warn("Temperature {} out of range [0.0, 2.0], clamping", config.temperature);
        config.temperature = std::max(0.0f, std::min(2.0f, config.temperature));
    }

    // Validate provider index
    if (!config.ai_providers.empty() &&
        (config.selected_provider_index < 0 ||
         config.selected_provider_index >= static_cast<int>(config.ai_providers.size()))) {
        spdlog::warn("Invalid provider index {}, using 0", config.selected_provider_index);
        config.selected_provider_index = 0;
    }

    // Validate retry count
    if (config.retry_count < 0) config.retry_count = 0;
    if (config.retry_count > 10) config.retry_count = 10;

    spdlog::debug("Configuration validated successfully");
    if (config.discover_mode || config.list_repos) {
        spdlog::debug("  Mode: repository discovery");
    } else {
        spdlog::debug("  PR: {}/{}/{}", config.owner, config.repo, config.pr_number);
    }
    spdlog::debug("  Model: {}", active.model);
    spdlog::debug("  Provider: {} ({})", active.name, active.base_url);
    spdlog::debug("  Output: {}", config.output_path);
}

} // namespace ai_pr_reviewer
