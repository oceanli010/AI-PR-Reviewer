#include "ai_pr_reviewer/github_client.h"
#include "ai_pr_reviewer/diff_parser.h"

#include <cpprest/http_client.h>
#include <cpprest/json.h>
#include <spdlog/spdlog.h>

#include <thread>
#include <chrono>
#include <sstream>

namespace ai_pr_reviewer {

using namespace web;
using namespace web::http;
using namespace web::http::client;

// ============================================================================
// Constructor
// ============================================================================
GitHubClient::GitHubClient(const AppConfig& config)
    : config_(config) {}

void GitHubClient::set_progress_callback(ProgressCallback callback) {
    progress_callback_ = std::move(callback);
}

// ============================================================================
// PR Metadata
// ============================================================================
PrMetadata GitHubClient::fetch_pr_metadata(const std::string& owner,
                                            const std::string& repo,
                                            int pr_number) {
    if (progress_callback_) {
        progress_callback_("Fetching PR metadata from GitHub...");
    }

    std::string url = api_base() + "/repos/" + owner + "/" + repo + "/pulls/" + std::to_string(pr_number);
    std::string response_body = http_get(url);

    auto json = json::value::parse(utility::conversions::to_string_t(response_body));

    PrMetadata meta;
    meta.number = json.at(U("number")).as_integer();
    meta.title = utility::conversions::to_utf8string(json.at(U("title")).as_string());
    meta.html_url = utility::conversions::to_utf8string(json.at(U("html_url")).as_string());
    meta.base_branch = utility::conversions::to_utf8string(json.at(U("base")).at(U("ref")).as_string());
    meta.head_branch = utility::conversions::to_utf8string(json.at(U("head")).at(U("ref")).as_string());

    if (json.has_field(U("body")) && !json.at(U("body")).is_null()) {
        meta.description = utility::conversions::to_utf8string(json.at(U("body")).as_string());
    }

    if (json.has_field(U("user"))) {
        meta.author = utility::conversions::to_utf8string(json.at(U("user")).at(U("login")).as_string());
        if (json.at(U("user")).has_field(U("avatar_url"))) {
            meta.author_avatar = utility::conversions::to_utf8string(json.at(U("user")).at(U("avatar_url")).as_string());
        }
    }

    meta.created_at = utility::conversions::to_utf8string(json.at(U("created_at")).as_string());
    meta.updated_at = utility::conversions::to_utf8string(json.at(U("updated_at")).as_string());

    if (json.has_field(U("commits"))) {
        meta.commit_count = json.at(U("commits")).as_integer();
    }
    if (json.has_field(U("comments"))) {
        meta.comment_count = json.at(U("comments")).as_integer();
    }
    if (json.has_field(U("additions"))) {
        meta.additions = json.at(U("additions")).as_integer();
    }
    if (json.has_field(U("deletions"))) {
        meta.deletions = json.at(U("deletions")).as_integer();
    }
    if (json.has_field(U("changed_files"))) {
        meta.changed_files = json.at(U("changed_files")).as_integer();
    }

    spdlog::info("PR #{}: {} by {}", meta.number, meta.title, meta.author);
    return meta;
}

// ============================================================================
// PR Files (with pagination support)
// ============================================================================
std::vector<FileChange> GitHubClient::fetch_pr_files(const std::string& owner,
                                                       const std::string& repo,
                                                       int pr_number) {
    if (progress_callback_) {
        progress_callback_("Fetching PR file changes from GitHub...");
    }

    std::vector<FileChange> all_files;
    int page = 1;
    const int per_page = 100; // GitHub max per page

    while (true) {
        std::string url = api_base() + "/repos/" + owner + "/" + repo +
                          "/pulls/" + std::to_string(pr_number) + "/files"
                          "?page=" + std::to_string(page) +
                          "&per_page=" + std::to_string(per_page);

        std::string response_body = http_get(url);
        auto json_array = json::value::parse(utility::conversions::to_string_t(response_body));

        if (!json_array.is_array()) break;

        auto arr = json_array.as_array();
        if (arr.size() == 0) break;

        for (const auto& file_json : arr) {
            FileChange file;

            file.filename = utility::conversions::to_utf8string(file_json.at(U("filename")).as_string());

            std::string status_str = utility::conversions::to_utf8string(file_json.at(U("status")).as_string());
            file.status = map_file_status(status_str);

            file.additions = file_json.at(U("additions")).as_integer();
            file.deletions = file_json.at(U("deletions")).as_integer();
            file.changes = file_json.at(U("changes")).as_integer();

            if (file_json.has_field(U("patch"))) {
                file.raw_diff = utility::conversions::to_utf8string(file_json.at(U("patch")).as_string());
            }

            if (file_json.has_field(U("previous_filename"))) {
                file.previous_filename = utility::conversions::to_utf8string(
                    file_json.at(U("previous_filename")).as_string());
            }

            file.is_binary = file.raw_diff.empty();

            // Detect language from filename
            file.language = DiffParser::detect_language(file.filename);

            all_files.push_back(std::move(file));
        }

        spdlog::info("Fetched page {}: {} files (total: {})", page, arr.size(), all_files.size());

        // If less than per_page, we've reached the last page
        if (static_cast<size_t>(arr.size()) < static_cast<size_t>(per_page)) break;

        page++;
    }

    spdlog::info("Total files to analyze: {}", all_files.size());
    return all_files;
}

// ============================================================================
// HTTP GET with retry logic
// ============================================================================
std::string GitHubClient::http_get(const std::string& url) {
    http_client_config client_config;
    client_config.set_timeout(std::chrono::seconds(30));

    if (config_.verbose) {
        spdlog::debug("HTTP GET: {}", url);
    }

    std::string last_error;
    for (int attempt = 0; attempt <= config_.retry_count; attempt++) {
        try {
            http_client client(utility::conversions::to_string_t(url), client_config);
            http_request request(methods::GET);

            // Set headers
            request.headers().add(U("Accept"), U("application/vnd.github.v3+json"));
            request.headers().add(U("User-Agent"), U("AI-PR-Reviewer/1.0"));

            if (!config_.github_token.empty()) {
                request.headers().add(U("Authorization"),
                    U("token ") + utility::conversions::to_string_t(config_.github_token));
            }

            http_response response = client.request(request).get();
            int status_code = response.status_code();

            if (status_code == 200) {
                return utility::conversions::to_utf8string(response.extract_string().get());
            }

            // Handle rate limiting
            auto& headers = response.headers();
            auto rate_limit_iter = headers.find(U("X-RateLimit-Remaining"));
            if (status_code == 429 || (status_code == 403 &&
                rate_limit_iter != headers.end() &&
                rate_limit_iter->second == U("0"))) {

                std::string reset_time = "60";
                auto reset_iter = headers.find(U("X-RateLimit-Reset"));
                if (reset_iter != headers.end()) {
                    reset_time = utility::conversions::to_utf8string(
                        reset_iter->second);
                }

                spdlog::warn("GitHub API rate limit reached. Reset at Unix timestamp: {}", reset_time);

                if (attempt < config_.retry_count) {
                    int wait_seconds = 60;
                    spdlog::info("Waiting {} seconds before retry...", wait_seconds);
                    std::this_thread::sleep_for(std::chrono::seconds(wait_seconds));
                    continue;
                }
            }

            // Not found
            if (status_code == 404) {
                throw std::runtime_error("PR not found. Check owner/repo/number and access permissions.");
            }

            // Other errors
            auto body = utility::conversions::to_utf8string(response.extract_string().get());
            last_error = "GitHub API error " + std::to_string(status_code) + ": " + body;

            if (attempt < config_.retry_count) {
                int delay_ms = config_.retry_delay_ms * (attempt + 1);
                spdlog::warn("Request failed (attempt {}/{}): {}. Retrying in {}ms...",
                    attempt + 1, config_.retry_count + 1, last_error, delay_ms);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                continue;
            }

            throw std::runtime_error(last_error);

        } catch (const std::runtime_error&) {
            throw; // Re-throw our own exceptions
        } catch (const std::exception& e) {
            last_error = e.what();
            if (attempt < config_.retry_count) {
                int delay_ms = config_.retry_delay_ms * (attempt + 1);
                spdlog::warn("Network error (attempt {}/{}): {}. Retrying in {}ms...",
                    attempt + 1, config_.retry_count + 1, last_error, delay_ms);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                continue;
            }
            throw std::runtime_error("GitHub API request failed after " +
                std::to_string(config_.retry_count + 1) + " attempts: " + last_error);
        }
    }

    throw std::runtime_error("GitHub API request failed: " + last_error);
}

// ============================================================================
// Helpers
// ============================================================================
std::string GitHubClient::api_base() const {
    return "https://api.github.com";
}

std::string GitHubClient::auth_header() const {
    if (!config_.github_token.empty()) {
        return "token " + config_.github_token;
    }
    return "";
}

std::string GitHubClient::map_file_status(const std::string& git_status) {
    if (git_status == "added") return "Added";
    if (git_status == "modified") return "Modified";
    if (git_status == "removed") return "Removed";
    if (git_status == "renamed") return "Renamed";
    if (git_status == "copied") return "Copied";
    if (git_status == "changed") return "Changed";
    return git_status;
}

} // namespace ai_pr_reviewer
