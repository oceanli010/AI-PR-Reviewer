#pragma once

#include "types.h"
#include <string>

namespace ai_pr_reviewer {

// ============================================================================
// ConfigManager - Loads configuration from CLI args, env vars, and YAML file
//
// Priority (high to low):
//   1. CLI arguments
//   2. Environment variables
//   3. YAML config file
//   4. Built-in defaults
// ============================================================================
class ConfigManager {
public:
    // Parse CLI arguments and load all config sources.
    // Returns the merged configuration.
    // Throws std::runtime_error on fatal config errors (e.g., missing API key).
    static AppConfig load(int argc, char* argv[]);

private:
    // Print help message to stdout
    static void print_help(const std::string& program_name);

    // Print version info to stdout
    static void print_version();

    // Load config from YAML file, if specified
    static void load_from_file(AppConfig& config, const std::string& filepath);

    // Apply environment variable overrides
    static void apply_env_overrides(AppConfig& config);

    // Validate config and fill in defaults
    static void validate(AppConfig& config);

    // Parse GitHub PR URL into owner/repo/number
    static bool parse_pr_url(const std::string& url,
                             std::string& owner,
                             std::string& repo,
                             int& pr_number);
};

} // namespace ai_pr_reviewer
