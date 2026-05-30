#pragma once

#include <string>
#include <vector>
#include <optional>

namespace ai_pr_reviewer {

// ============================================================================
// Severity levels for review issues
// ============================================================================
enum class Severity {
    Critical,  // Security vulnerabilities, crashes
    High,      // Logic errors, data loss risk
    Medium,    // Performance issues, bad practices
    Low,       // Style issues, minor improvements
    Info       // Informational suggestions
};

// ============================================================================
// Diff parsing types
// ============================================================================
enum class LineType {
    Context,   // Unchanged context line
    Addition,  // Added line (starts with +)
    Deletion   // Deleted line (starts with -)
};

struct DiffLine {
    LineType type;
    std::string content;
    int old_lineno;
    int new_lineno;
};

struct Hunk {
    int old_start;
    int old_count;
    int new_start;
    int new_count;
    std::string header;            // @@ -1,5 +1,7 @@
    std::vector<DiffLine> lines;
};

struct FileChange {
    std::string filename;
    std::string status;            // added, modified, removed, renamed
    int additions = 0;
    int deletions = 0;
    int changes = 0;
    std::string raw_diff;
    std::string language;          // detected programming language
    std::vector<Hunk> hunks;
    std::optional<std::string> previous_filename;
    bool is_binary = false;
};

// ============================================================================
// PR metadata
// ============================================================================
struct PrMetadata {
    std::string title;
    std::string description;
    std::string author;
    std::string author_avatar;
    std::string created_at;
    std::string updated_at;
    std::string html_url;
    int number = 0;
    std::string base_branch;
    std::string head_branch;
    int commit_count = 0;
    int comment_count = 0;
    int additions = 0;
    int deletions = 0;
    int changed_files = 0;
};

// ============================================================================
// Review result types
// ============================================================================
struct RiskItem {
    std::string file;
    int line = 0;
    Severity severity = Severity::Info;
    std::string category;          // security, bug, performance, style, logic, maintainability
    std::string title;             // Short title for this issue
    std::string description;       // Detailed description of the problem
    std::string suggestion;        // How to fix or improve
    std::string code_snippet;      // Relevant code context
};

struct CodeQualityMetrics {
    int total_lines_changed = 0;
    int files_modified = 0;
    std::vector<std::string> strengths;
    std::vector<std::string> concerns;
};

struct ReviewResult {
    std::string summary;           // Overall summary of the PR changes
    std::vector<RiskItem> risk_items;
    int total_issues = 0;
    int critical_count = 0;
    int high_count = 0;
    int medium_count = 0;
    int low_count = 0;
    int info_count = 0;
    std::string overall_assessment; // Final evaluation and recommendations
    CodeQualityMetrics quality_metrics;
};

// ============================================================================
// AI Provider configuration (supports multi-model switching)
// ============================================================================
struct AiProvider {
    std::string name;                // Display name, e.g. "OpenAI GPT-4o", "DeepSeek V3"
    std::string api_key;             // API key (or env var reference)
    std::string model;               // Model identifier, e.g. "gpt-4o", "deepseek-chat"
    std::string base_url;            // API base URL, e.g. "https://api.openai.com/v1"
    float temperature = 0.3f;
    int max_tokens = 4096;
    int timeout_seconds = 120;
    bool is_default = false;         // Default provider to use
};

// ============================================================================
// Repository list item (for discovery mode)
// ============================================================================
struct RepoInfo {
    std::string name;                // e.g. "AI-PR-Reviewer"
    std::string full_name;           // e.g. "oceanli010/AI-PR-Reviewer"
    std::string description;
    std::string language;
    std::string default_branch;
    std::string updated_at;
    int stars = 0;
    int open_issues = 0;
    bool is_private = false;
    bool is_fork = false;
};

// ============================================================================
// PR list item (for discovery mode)
// ============================================================================
struct PrListItem {
    int number = 0;
    std::string title;
    std::string author;
    std::string created_at;
    std::string updated_at;
    std::string html_url;
    std::string state;               // "open", "closed", "merged"
    bool is_draft = false;
};

// ============================================================================
// Application configuration
// ============================================================================
struct AppConfig {
    // PR source - parsed from URL or individual params
    std::string pr_url;
    std::string owner;
    std::string repo;
    int pr_number = 0;

    // AI Provider settings (multi-provider support)
    std::vector<AiProvider> ai_providers;
    int selected_provider_index = 0; // Index in ai_providers vector

    // Legacy fields (used when no providers configured, or for CLI override)
    std::string openai_api_key;
    std::string openai_model = "gpt-4o";
    std::string openai_base_url = "https://api.openai.com/v1";
    float temperature = 0.3f;
    int max_tokens = 4096;
    int context_timeout_seconds = 120;

    // GitHub settings
    std::string github_token;

    // Discovery mode settings
    bool discover_mode = false;      // Interactive repo/PR discovery
    bool list_repos = false;         // List accessible repositories
    std::string repo_filter;          // Filter repos by name pattern
    std::string pr_state_filter = "open"; // open, closed, all

    // Output settings
    std::string output_path = "review-report.html";
    std::string template_path;       // Custom HTML template path

    // Runtime settings
    bool verbose = false;
    bool quiet = false;
    int retry_count = 3;
    int retry_delay_ms = 1000;
    int max_files_per_batch = 10;

    // Config file path
    std::string config_file;

    // Helper: get the active AI provider (fallback to legacy fields)
    AiProvider active_provider() const {
        if (!ai_providers.empty() && selected_provider_index >= 0 &&
            selected_provider_index < static_cast<int>(ai_providers.size())) {
            AiProvider p = ai_providers[selected_provider_index];
            // Apply CLI overrides if legacy fields are set
            if (!openai_api_key.empty()) p.api_key = openai_api_key;
            if (!openai_model.empty() && openai_model != "gpt-4o") p.model = openai_model;
            if (!openai_base_url.empty() && openai_base_url != "https://api.openai.com/v1")
                p.base_url = openai_base_url;
            return p;
        }
        // Legacy mode: construct provider from flat fields
        AiProvider legacy;
        legacy.name = "OpenAI (legacy)";
        legacy.api_key = openai_api_key;
        legacy.model = openai_model;
        legacy.base_url = openai_base_url;
        legacy.temperature = temperature;
        legacy.max_tokens = max_tokens;
        legacy.timeout_seconds = context_timeout_seconds;
        return legacy;
    }
};

} // namespace ai_pr_reviewer
