#include "ai_pr_reviewer/ai_analyzer.h"

#include <cpprest/http_client.h>
#include <cpprest/json.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <sstream>
#include <thread>
#include <chrono>

namespace ai_pr_reviewer {

using namespace web;
using namespace web::http;
using namespace web::http::client;

using nlohmann_json = nlohmann::json;

// ============================================================================
// Constructor
// ============================================================================
AIAnalyzer::AIAnalyzer(const AppConfig& config)
    : config_(config) {}

void AIAnalyzer::set_progress_callback(ProgressCallback callback) {
    progress_callback_ = std::move(callback);
}

// ============================================================================
// Main Analysis Entry Point
// ============================================================================
ReviewResult AIAnalyzer::analyze(const PrMetadata& pr_meta,
                                   const std::vector<FileChange>& files) {
    if (files.empty()) {
        ReviewResult empty;
        empty.summary = "No files were changed in this PR.";
        empty.overall_assessment = "No changes to analyze.";
        return empty;
    }

    if (progress_callback_) {
        progress_callback_("Starting AI analysis of " + std::to_string(files.size()) + " files...");
    }

    // Determine if batching is needed
    bool need_batching = static_cast<int>(files.size()) > config_.max_files_per_batch;

    if (!need_batching) {
        // Single request for small PRs
        spdlog::info("Analyzing PR with single API call ({} files)", files.size());
        std::string system_prompt = build_system_prompt();
        std::string user_prompt = build_user_prompt(pr_meta, files);

        if (progress_callback_) {
            progress_callback_("Calling OpenAI API for code analysis...");
        }

        std::string response = call_openai_api(system_prompt, user_prompt);
        return parse_response(response);
    }

    // Batch processing for large PRs
    spdlog::info("Analyzing PR in batches ({} files total, {} per batch)",
        files.size(), config_.max_files_per_batch);

    int total_batches = (static_cast<int>(files.size()) + config_.max_files_per_batch - 1)
                         / config_.max_files_per_batch;
    std::vector<ReviewResult> batch_results;

    for (int i = 0; i < total_batches; i++) {
        int start = i * config_.max_files_per_batch;
        int end = std::min(start + config_.max_files_per_batch, static_cast<int>(files.size()));

        std::vector<FileChange> batch(files.begin() + start, files.begin() + end);

        if (progress_callback_) {
            progress_callback_("Analyzing batch " + std::to_string(i + 1) +
                               "/" + std::to_string(total_batches) + " (" +
                               std::to_string(batch.size()) + " files)...");
        }

        spdlog::info("Batch {}/{}: {} files", i + 1, total_batches, batch.size());

        std::string system_prompt = build_system_prompt();
        std::string user_prompt = build_batch_prompt(pr_meta, batch, i + 1, total_batches);

        std::string response = call_openai_api(system_prompt, user_prompt);
        batch_results.push_back(parse_response(response));

        // Small delay between batches to avoid rate limiting
        if (i < total_batches - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    return merge_results(batch_results, pr_meta);
}

// ============================================================================
// Prompt Building
// ============================================================================
std::string AIAnalyzer::build_system_prompt() {
    return R"PROMPT(You are an expert code reviewer with deep experience in software engineering,
security auditing, and code quality assurance. Your task is to analyze Pull Request
changes and provide constructive, actionable feedback.

## Review Guidelines:
1. **Security**: Look for SQL injection, XSS, CSRF, hardcoded secrets, unsafe deserialization,
   path traversal, improper authentication/authorization, and insecure dependencies.
2. **Bugs & Logic Errors**: Identify potential null dereferences, off-by-one errors,
   race conditions, memory leaks (in C++), incorrect error handling, and edge cases.
3. **Performance**: Spot inefficient algorithms (high complexity), unnecessary allocations,
   excessive database queries, missing caching opportunities, and blocking I/O.
4. **Code Style & Maintainability**: Check naming conventions, code duplication,
   overly complex functions, missing documentation, and violation of SOLID principles.
5. **Best Practices**: Verify proper use of APIs, framework conventions, error handling patterns,
   and language-specific idioms.

## Output Format:
You MUST respond with a valid JSON object in this exact structure:
```json
{
  "summary": "Overall summary of changes (2-3 sentences, in Chinese)",
  "overall_assessment": "Final overall evaluation and key recommendations (in Chinese)",
  "risk_items": [
    {
      "file": "path/to/file.cpp",
      "line": 42,
      "severity": "critical|high|medium|low|info",
      "category": "security|bug|performance|style|logic|maintainability|other",
      "title": "Short issue title",
      "description": "Detailed explanation of the problem",
      "suggestion": "How to fix or improve",
      "code_snippet": "Relevant code context (2-5 lines)"
    }
  ]
}
```

## Severity Definitions:
- **critical**: Security vulnerabilities, data loss, crashes - must fix immediately
- **high**: Logic errors, potential bugs that could cause incorrect behavior
- **medium**: Performance issues, code smells, moderate complexity problems
- **low**: Style inconsistencies, minor improvements, nitpicks
- **info**: Educational suggestions, alternative approaches

Focus on real issues and avoid false positives. For each issue flagged, you must be confident
it represents a legitimate concern. If the code quality is excellent, provide positive feedback.
Respond in Chinese for descriptions, suggestions, and assessments.)PROMPT";
}

std::string AIAnalyzer::build_user_prompt(const PrMetadata& pr_meta,
                                           const std::vector<FileChange>& files) {
    std::ostringstream prompt;

    // PR Overview
    prompt << "## PR Overview\n";
    prompt << "- Title: " << pr_meta.title << "\n";
    prompt << "- Author: " << pr_meta.author << "\n";
    prompt << "- Base: " << pr_meta.base_branch << " <- Head: " << pr_meta.head_branch << "\n";
    prompt << "- Files Changed: " << pr_meta.changed_files << "\n";
    prompt << "- Additions: " << pr_meta.additions << "\n";
    prompt << "- Deletions: " << pr_meta.deletions << "\n";

    if (!pr_meta.description.empty()) {
        prompt << "\n## PR Description\n";
        prompt << pr_meta.description << "\n";
    }

    // File Changes
    prompt << "\n## Changed Files\n";
    for (const auto& file : files) {
        prompt << "- " << file.filename << " (" << file.status << ", "
               << "+" << file.additions << "/-" << file.deletions << ")"
               << " [" << file.language << "]\n";
    }

    // Code Diffs
    prompt << "\n## Code Changes (Unified Diff)\n";
    for (const auto& file : files) {
        if (file.is_binary) {
            prompt << "\n### " << file.filename << " (BINARY FILE - SKIPPED)\n";
            continue;
        }

        prompt << "\n### " << file.filename << " [" << file.language << "]\n";
        prompt << "```diff\n";

        // Limit diff size per file to avoid token overflow
        std::string diff = file.raw_diff;
        const size_t max_diff_chars = 15000;
        if (diff.size() > max_diff_chars) {
            diff = diff.substr(0, max_diff_chars);
            diff += "\n... (diff truncated due to size)\n";
        }

        prompt << diff << "\n```\n";
    }

    prompt << "\n## Review Instructions\n";
    prompt << "Please analyze the above code changes carefully. ";
    prompt << "Identify security issues, bugs, performance problems, style issues, ";
    prompt << "and provide actionable suggestions. ";
    prompt << "Respond strictly in the JSON format specified in the system prompt.\n";

    return prompt.str();
}

std::string AIAnalyzer::build_batch_prompt(const PrMetadata& pr_meta,
                                            const std::vector<FileChange>& batch,
                                            int batch_num, int total_batches) {
    std::ostringstream prompt;

    prompt << "## PR Review - Batch " << batch_num << " of " << total_batches << "\n\n";
    prompt << "PR: #" << pr_meta.number << " " << pr_meta.title << " by " << pr_meta.author << "\n\n";
    prompt << "This is batch " << batch_num << " of " << total_batches << ". ";
    prompt << "Analyze only the files in this batch. ";
    prompt << "The final results will be merged.\n\n";

    // File list for this batch
    prompt << "## Files in this batch\n";
    for (const auto& file : batch) {
        prompt << "- " << file.filename << " (" << file.language << ")\n";
    }

    // Code diffs for this batch
    prompt << "\n## Code Changes\n";
    for (const auto& file : batch) {
        if (file.is_binary) continue;

        prompt << "\n### " << file.filename << "\n";
        prompt << "```diff\n";

        size_t max_diff = 15000;
        std::string diff = file.raw_diff;
        if (diff.size() > max_diff) {
            diff = diff.substr(0, max_diff) + "\n... (truncated)\n";
        }

        prompt << diff << "\n```\n";
    }

    prompt << "\nRespond in the JSON format specified in the system prompt.\n";
    return prompt.str();
}

// ============================================================================
// OpenAI API Call
// ============================================================================
std::string AIAnalyzer::call_openai_api(const std::string& system_prompt,
                                         const std::string& user_prompt) {
    std::string request_body = build_request_body(
        system_prompt, user_prompt, config_.openai_model,
        config_.temperature, config_.max_tokens);

    std::string url = config_.openai_base_url + "/chat/completions";

    if (config_.verbose) {
        spdlog::debug("OpenAI API URL: {}", url);
        spdlog::debug("Request body size: {} bytes", request_body.size());
    }

    http_client_config client_config;
    client_config.set_timeout(std::chrono::seconds(config_.context_timeout_seconds));

    std::string last_error;
    for (int attempt = 0; attempt <= config_.retry_count; attempt++) {
        try {
            http_client client(utility::conversions::to_string_t(url), client_config);
            http_request request(methods::POST);

            request.headers().add(U("Content-Type"), U("application/json"));
            request.headers().add(U("Authorization"),
                U("Bearer ") + utility::conversions::to_string_t(config_.openai_api_key));

            request.set_body(utility::conversions::to_string_t(request_body));

            http_response response = client.request(request).get();
            int status_code = response.status_code();

            auto response_body = response.extract_string().get();
            std::string response_str = utility::conversions::to_utf8string(response_body);

            if (status_code == 200) {
                return response_str;
            }

            // Handle errors
            std::string error_msg;
            try {
                auto err_json = nlohmann_json::parse(response_str);
                if (err_json.contains("error") && err_json["error"].contains("message")) {
                    error_msg = err_json["error"]["message"].get<std::string>();
                } else {
                    error_msg = response_str;
                }
            } catch (...) {
                error_msg = response_str;
            }

            last_error = "OpenAI API error " + std::to_string(status_code) + ": " + error_msg;

            // Don't retry on auth errors
            if (status_code == 401 || status_code == 403) {
                throw std::runtime_error(last_error);
            }

            // Rate limiting - wait and retry
            if (status_code == 429) {
                spdlog::warn("OpenAI rate limit reached");
                if (attempt < config_.retry_count) {
                    int wait_seconds = 5 * (attempt + 1);
                    spdlog::info("Waiting {} seconds before retry...", wait_seconds);
                    std::this_thread::sleep_for(std::chrono::seconds(wait_seconds));
                    continue;
                }
            }

            // Server errors - retry
            if (status_code >= 500 && attempt < config_.retry_count) {
                int delay_ms = config_.retry_delay_ms * (attempt + 1) * 2;
                spdlog::warn("Server error, retrying in {}ms...", delay_ms);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                continue;
            }

            throw std::runtime_error(last_error);

        } catch (const std::runtime_error&) {
            throw;
        } catch (const std::exception& e) {
            last_error = e.what();
            if (attempt < config_.retry_count) {
                int delay_ms = config_.retry_delay_ms * (attempt + 1) * 2;
                spdlog::warn("API call failed (attempt {}/{}): {}. Retrying in {}ms...",
                    attempt + 1, config_.retry_count + 1, last_error, delay_ms);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                continue;
            }
            throw std::runtime_error("OpenAI API call failed after " +
                std::to_string(config_.retry_count + 1) + " attempts: " + last_error);
        }
    }

    throw std::runtime_error("OpenAI API call failed: " + last_error);
}

// ============================================================================
// Request Body Building
// ============================================================================
std::string AIAnalyzer::build_request_body(const std::string& system_prompt,
                                            const std::string& user_prompt,
                                            const std::string& model,
                                            float temperature,
                                            int max_tokens) {
    nlohmann_json body;

    body["model"] = model;
    body["temperature"] = temperature;
    body["max_tokens"] = max_tokens;

    body["messages"] = nlohmann_json::array();

    // System message
    nlohmann_json sys_msg;
    sys_msg["role"] = "system";
    sys_msg["content"] = system_prompt;
    body["messages"].push_back(sys_msg);

    // User message
    nlohmann_json user_msg;
    user_msg["role"] = "user";
    user_msg["content"] = user_prompt;
    body["messages"].push_back(user_msg);

    // Request JSON response format
    body["response_format"]["type"] = "json_object";

    return body.dump();
}

// ============================================================================
// Response Parsing
// ============================================================================
ReviewResult AIAnalyzer::parse_response(const std::string& json_response) {
    try {
        auto response = nlohmann_json::parse(json_response);

        // Extract the message content
        if (!response.contains("choices") || response["choices"].empty()) {
            throw std::runtime_error("OpenAI response missing 'choices'");
        }

        std::string content = response["choices"][0]["message"]["content"].get<std::string>();

        // Log token usage if available
        if (response.contains("usage")) {
            auto& usage = response["usage"];
            spdlog::info("API Usage - Prompt: {} tokens, Completion: {} tokens, Total: {}",
                usage["prompt_tokens"].get<int>(),
                usage["completion_tokens"].get<int>(),
                usage["total_tokens"].get<int>());
        }

        // Parse the content JSON
        auto result_json = nlohmann_json::parse(content);

        ReviewResult result;
        result.summary = result_json.value("summary", "");
        result.overall_assessment = result_json.value("overall_assessment", "");

        if (result_json.contains("risk_items") && result_json["risk_items"].is_array()) {
            for (const auto& item_json : result_json["risk_items"]) {
                RiskItem item;
                item.file = item_json.value("file", "");
                item.line = item_json.value("line", 0);
                item.category = item_json.value("category", "other");
                item.title = item_json.value("title", "");
                item.description = item_json.value("description", "");
                item.suggestion = item_json.value("suggestion", "");
                item.code_snippet = item_json.value("code_snippet", "");

                // Parse severity
                item.severity = parse_severity(item_json.value("severity", "info"));

                result.risk_items.push_back(std::move(item));
            }
        }

        // Count by severity
        result.total_issues = static_cast<int>(result.risk_items.size());
        for (const auto& item : result.risk_items) {
            switch (item.severity) {
                case Severity::Critical: result.critical_count++; break;
                case Severity::High:     result.high_count++;     break;
                case Severity::Medium:   result.medium_count++;   break;
                case Severity::Low:      result.low_count++;      break;
                case Severity::Info:     result.info_count++;     break;
            }
        }

        spdlog::info("Analysis complete: {} issues ({} critical, {} high, {} medium, {} low, {} info)",
            result.total_issues, result.critical_count, result.high_count,
            result.medium_count, result.low_count, result.info_count);

        return result;
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse OpenAI response: {}", e.what());
        throw std::runtime_error("Failed to parse AI analysis response: " + std::string(e.what()));
    }
}

// ============================================================================
// Merge Batch Results
// ============================================================================
ReviewResult AIAnalyzer::merge_results(const std::vector<ReviewResult>& batch_results,
                                        const PrMetadata& pr_meta) {
    ReviewResult merged;
    std::ostringstream summary;

    summary << "This PR by " << pr_meta.author << " changes "
            << pr_meta.changed_files << " files with "
            << pr_meta.additions << " additions and "
            << pr_meta.deletions << " deletions. ";

    for (const auto& result : batch_results) {
        if (!result.summary.empty()) {
            if (batch_results.size() > 1) {
                summary << "\n\nBatch analysis: " << result.summary;
            } else {
                summary << result.summary;
            }
        }

        merged.risk_items.insert(merged.risk_items.end(),
            result.risk_items.begin(), result.risk_items.end());

        merged.total_issues += result.total_issues;
        merged.critical_count += result.critical_count;
        merged.high_count += result.high_count;
        merged.medium_count += result.medium_count;
        merged.low_count += result.low_count;
        merged.info_count += result.info_count;
    }

    merged.summary = summary.str();

    if (batch_results.size() > 1) {
        merged.overall_assessment = "Multi-batch analysis complete. ";
    }

    // Combine assessments
    for (const auto& result : batch_results) {
        if (!result.overall_assessment.empty()) {
            if (!merged.overall_assessment.empty()) {
                merged.overall_assessment += " ";
            }
            merged.overall_assessment += result.overall_assessment;
        }
    }

    return merged;
}

// ============================================================================
// Helpers
// ============================================================================
Severity AIAnalyzer::parse_severity(const std::string& s) {
    if (s == "critical") return Severity::Critical;
    if (s == "high")     return Severity::High;
    if (s == "medium")   return Severity::Medium;
    if (s == "low")      return Severity::Low;
    return Severity::Info;
}

} // namespace ai_pr_reviewer
