#include "ai_pr_reviewer/config.h"

#include <CLI/CLI.hpp>
#include <yaml-cpp/yaml.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <regex>
#include <stdexcept>
#include <iostream>

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

    // --- Load config file if specified ---
    if (!config.config_file.empty()) {
        load_from_file(config, config.config_file);
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

        // OpenAI section
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
    // Check required: PR source
    if (config.owner.empty() || config.repo.empty() || config.pr_number <= 0) {
        throw std::runtime_error(
            "Missing PR information. Provide one of:\n"
            "  --pr-url https://github.com/owner/repo/pull/123\n"
            "  --owner OWNER --repo REPO --pr-number 123"
        );
    }

    // Check required: API key
    if (config.openai_api_key.empty()) {
        throw std::runtime_error(
            "OpenAI API key is required. Provide it via:\n"
            "  --api-key YOUR_KEY\n"
            "  OPENAI_API_KEY environment variable\n"
            "  config.yaml 'openai.api_key' field"
        );
    }

    // Validate temperature range
    if (config.temperature < 0.0f || config.temperature > 2.0f) {
        spdlog::warn("Temperature {} out of range [0.0, 2.0], clamping", config.temperature);
        config.temperature = std::max(0.0f, std::min(2.0f, config.temperature));
    }

    // Validate retry count
    if (config.retry_count < 0) config.retry_count = 0;
    if (config.retry_count > 10) config.retry_count = 10;

    spdlog::debug("Configuration validated successfully");
    spdlog::debug("  PR: {}/{}/{}", config.owner, config.repo, config.pr_number);
    spdlog::debug("  Model: {}", config.openai_model);
    spdlog::debug("  Output: {}", config.output_path);
}

} // namespace ai_pr_reviewer
