#pragma once

#include "types.h"
#include <string>
#include <vector>
#include <functional>

namespace ai_pr_reviewer {

// ============================================================================
// AIAnalyzer - Calls OpenAI GPT-4o API to analyze code changes
//
// Builds structured prompts with:
//   1. System prompt: Role definition and review guidelines
//   2. User prompt: PR metadata + structured diff context
//
// Parses the JSON response into a ReviewResult with categorized risk items.
// ============================================================================
class AIAnalyzer {
public:
    explicit AIAnalyzer(const AppConfig& config);

    // Analyze PR changes and return structured review result
    // Files are batched if the total diff is too large
    ReviewResult analyze(const PrMetadata& pr_meta,
                          const std::vector<FileChange>& files);

    // Progress callback for UI updates
    using ProgressCallback = std::function<void(const std::string& message)>;
    void set_progress_callback(ProgressCallback callback);

private:
    AppConfig config_;
    ProgressCallback progress_callback_;

    // Build the system prompt defining the reviewer role and output format
    static std::string build_system_prompt();

    // Build the user prompt with PR context and code changes
    static std::string build_user_prompt(const PrMetadata& pr_meta,
                                          const std::vector<FileChange>& files);

    // Build prompt for a single batch of files
    static std::string build_batch_prompt(const PrMetadata& pr_meta,
                                           const std::vector<FileChange>& batch,
                                           int batch_num, int total_batches);

    // Call OpenAI Chat Completions API
    std::string call_openai_api(const std::string& system_prompt,
                                const std::string& user_prompt);

    // Parse the API response JSON into a ReviewResult
    static ReviewResult parse_response(const std::string& json_response);

    // Merge results from multiple batches
    static ReviewResult merge_results(const std::vector<ReviewResult>& batch_results,
                                       const PrMetadata& pr_meta);

    // Convert severity string to enum
    static Severity parse_severity(const std::string& s);

    // Build JSON request body for OpenAI API
    static std::string build_request_body(const std::string& system_prompt,
                                          const std::string& user_prompt,
                                          const std::string& model,
                                          float temperature,
                                          int max_tokens);
};

} // namespace ai_pr_reviewer
