#pragma once

#include "types.h"
#include <string>
#include <vector>
#include <functional>

namespace ai_pr_reviewer {

// ============================================================================
// GitHubClient - Interacts with GitHub REST API v3
//
// Fetches PR metadata and file diffs via GitHub REST API.
// Supports authenticated (higher rate limit) and anonymous access.
// ============================================================================
class GitHubClient {
public:
    explicit GitHubClient(const AppConfig& config);

    // Fetch PR metadata (title, description, author, stats, etc.)
    PrMetadata fetch_pr_metadata(const std::string& owner,
                                  const std::string& repo,
                                  int pr_number);

    // Fetch all changed files with their raw diffs
    // Handles pagination automatically for large PRs
    std::vector<FileChange> fetch_pr_files(const std::string& owner,
                                            const std::string& repo,
                                            int pr_number);

    // --- Repository & PR Discovery ---
    // List repositories accessible by the authenticated user
    // Type: "all", "owner", "public", "private", "member"
    std::vector<RepoInfo> list_repos(const std::string& type = "all",
                                      const std::string& filter = "");

    // List PRs for a specific repository
    // State: "open", "closed", "all"
    std::vector<PrListItem> list_prs(const std::string& owner,
                                      const std::string& repo,
                                      const std::string& state = "open");

    // Progress callback for UI updates
    using ProgressCallback = std::function<void(const std::string& message)>;
    void set_progress_callback(ProgressCallback callback);

private:
    AppConfig config_;
    ProgressCallback progress_callback_;

    // Low-level HTTP GET with retry logic and error handling
    std::string http_get(const std::string& url);

    // Build GitHub API base URL
    std::string api_base() const;

    // Build authorization header value
    std::string auth_header() const;

    // Check and handle rate limit headers
    void check_rate_limit(const std::string& response_headers);

    // Map GitHub file status to human-readable status
    static std::string map_file_status(const std::string& git_status);
};

} // namespace ai_pr_reviewer
