#include "ai_pr_reviewer/config.h"
#include "ai_pr_reviewer/github_client.h"
#include "ai_pr_reviewer/diff_parser.h"
#include "ai_pr_reviewer/ai_analyzer.h"
#include "ai_pr_reviewer/html_report.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <iostream>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <limits>

// ============================================================================
// Console UI helpers
// ============================================================================
void print_header() {
    std::cout << "\n";
    std::cout << "  ============================================================\n";
    std::cout << "     AI PR Reviewer v1.1\n";
    std::cout << "     Intelligent Pull Request Code Review Tool\n";
    std::cout << "  ============================================================\n\n";
}

void print_step(const std::string& step, int current, int total) {
    std::cout << "  [" << current << "/" << total << "] " << step << std::endl;
}

void print_success(const std::string& message) {
    std::cout << "  [+] " << message << std::endl;
}

void print_error(const std::string& message) {
    std::cerr << "  [-] ERROR: " << message << std::endl;
}

void print_info(const std::string& message) {
    std::cout << "  [*] " << message << std::endl;
}

void print_result_summary(const ai_pr_reviewer::ReviewResult& result) {
    std::cout << "\n";
    std::cout << "  ============================================================\n";
    std::cout << "     Review Complete!\n";
    std::cout << "  ============================================================\n";
    std::cout << "     Total Issues:    " << result.total_issues << "\n";
    std::cout << "     " << std::left << std::setw(16) << "Critical:" << result.critical_count << "\n";
    std::cout << "     " << std::left << std::setw(16) << "High:" << result.high_count << "\n";
    std::cout << "     " << std::left << std::setw(16) << "Medium:" << result.medium_count << "\n";
    std::cout << "     " << std::left << std::setw(16) << "Low:" << result.low_count << "\n";
    std::cout << "     " << std::left << std::setw(16) << "Info:" << result.info_count << "\n";
    std::cout << "  ============================================================\n\n";
}

// Display repo list and let user select one
bool select_repo(ai_pr_reviewer::GitHubClient& github,
                 const std::string& filter,
                 std::string& out_owner,
                 std::string& out_repo) {
    auto repos = github.list_repos("all", filter);

    if (repos.empty()) {
        std::cout << "\n  No repositories found";
        if (!filter.empty()) std::cout << " matching '" << filter << "'";
        std::cout << ".\n";
        std::cout << "  Make sure your GitHub token has repo access permissions.\n\n";
        return false;
    }

    std::cout << "\n  === Accessible Repositories ===\n\n";
    std::cout << "  " << std::left << std::setw(6) << "No."
              << std::setw(35) << "Repository"
              << std::setw(10) << "Stars"
              << std::setw(12) << "Language"
              << "Updated" << "\n";
    std::cout << "  " << std::string(90, '-') << "\n";

    const int max_display = 50;
    int count = 0;
    for (const auto& r : repos) {
        if (count >= max_display) {
            std::cout << "  ... and " << (repos.size() - max_display) << " more repositories\n";
            break;
        }
        std::string display_name = r.full_name.size() > 33 ? r.full_name.substr(0, 30) + "..." : r.full_name;
        std::string updated = r.updated_at.substr(0, 10);
        std::cout << "  " << std::left << std::setw(6) << (count + 1)
                  << std::setw(35) << display_name
                  << std::setw(10) << r.stars
                  << std::setw(12) << (r.language.empty() ? "-" : r.language)
                  << updated << "\n";
        count++;
    }

    std::cout << "\n  Enter repository number (1-" << std::min(static_cast<int>(repos.size()), max_display)
              << ") or 0 to cancel: ";

    int selection = 0;
    std::cin >> selection;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    if (selection <= 0 || selection > static_cast<int>(repos.size())) {
        std::cout << "  Cancelled.\n";
        return false;
    }

    const auto& selected = repos[selection - 1];
    // Parse full_name "owner/repo"
    size_t slash = selected.full_name.find('/');
    if (slash != std::string::npos) {
        out_owner = selected.full_name.substr(0, slash);
        out_repo = selected.full_name.substr(slash + 1);
    } else {
        out_owner = "";
        out_repo = selected.full_name;
    }

    std::cout << "\n  Selected: " << selected.full_name << "\n";
    return true;
}

// Display PR list and let user select one
bool select_pr(ai_pr_reviewer::GitHubClient& github,
               const std::string& owner,
               const std::string& repo,
               const std::string& state,
               int& out_pr_number) {
    auto prs = github.list_prs(owner, repo, state);

    if (prs.empty()) {
        std::cout << "\n  No " << state << " PRs found in " << owner << "/" << repo << ".\n\n";
        return false;
    }

    std::cout << "\n  === " << (state == "open" ? "Open" : state) << " Pull Requests in "
              << owner << "/" << repo << " ===\n\n";
    std::cout << "  " << std::left << std::setw(6) << "No."
              << std::setw(8) << "PR #"
              << std::setw(50) << "Title"
              << std::setw(18) << "Author"
              << "Created" << "\n";
    std::cout << "  " << std::string(110, '-') << "\n";

    const int max_display = 30;
    int count = 0;
    for (const auto& pr : prs) {
        if (count >= max_display) {
            std::cout << "  ... and " << (prs.size() - max_display) << " more PRs\n";
            break;
        }
        std::string title = pr.title.size() > 48 ? pr.title.substr(0, 45) + "..." : pr.title;
        std::string created = pr.created_at.substr(0, 10);
        std::string label = pr.is_draft ? "[DRAFT] " : "";
        std::cout << "  " << std::left << std::setw(6) << (count + 1)
                  << std::setw(8) << pr.number
                  << std::setw(50) << (label + title)
                  << std::setw(18) << (pr.author.size() > 16 ? pr.author.substr(0, 13) + "..." : pr.author)
                  << created << "\n";
        count++;
    }

    std::cout << "\n  Enter PR number (1-" << std::min(static_cast<int>(prs.size()), max_display)
              << ") or 0 to cancel: ";

    int selection = 0;
    std::cin >> selection;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    if (selection <= 0 || selection > static_cast<int>(prs.size())) {
        std::cout << "  Cancelled.\n";
        return false;
    }

    out_pr_number = prs[selection - 1].number;
    std::cout << "\n  Selected PR #" << out_pr_number << ": " << prs[selection - 1].title << "\n";
    return true;
}

// ============================================================================
// Run the full review pipeline
// ============================================================================
int run_review(const ai_pr_reviewer::AppConfig& config) {
    int total_steps = 5;

    ai_pr_reviewer::GitHubClient github(config);
    github.set_progress_callback([](const std::string& msg) {
        spdlog::info("  {}", msg);
    });

    // Step 1: Fetch PR data
    print_step("Fetching PR data from GitHub...", 1, total_steps);
    auto pr_meta = github.fetch_pr_metadata(config.owner, config.repo, config.pr_number);
    print_success("PR metadata loaded: #" + std::to_string(pr_meta.number) +
                  " - " + pr_meta.title);

    // Step 2: Fetch file diffs
    print_step("Fetching PR file changes...", 2, total_steps);
    auto files = github.fetch_pr_files(config.owner, config.repo, config.pr_number);
    print_success("Downloaded " + std::to_string(files.size()) + " file diffs");

    if (files.empty()) {
        std::cout << "\n  [!] No files changed in this PR. Nothing to analyze.\n";
        return 0;
    }

    int binary_count = 0;
    for (const auto& f : files) { if (f.is_binary) binary_count++; }
    int analyzable = static_cast<int>(files.size()) - binary_count;
    spdlog::info("  Files: {} total, {} analyzable, {} binary",
                 files.size(), analyzable, binary_count);

    // Step 3: Parse diffs
    print_step("Parsing diffs into structured data...", 3, total_steps);
    ai_pr_reviewer::DiffParser parser;
    std::vector<ai_pr_reviewer::FileChange> parsed_files;
    int total_hunks = 0, total_lines = 0;

    for (auto& file : files) {
        if (!file.is_binary && !file.raw_diff.empty()) {
            auto parsed = parser.parse("diff --git a/" + file.filename + " b/" + file.filename + "\n" + file.raw_diff);
            for (auto& pf : parsed) {
                pf.filename = file.filename; pf.status = file.status;
                pf.additions = file.additions; pf.deletions = file.deletions;
                pf.changes = file.changes; pf.language = file.language;
                pf.is_binary = file.is_binary;
                total_hunks += static_cast<int>(pf.hunks.size());
                for (const auto& h : pf.hunks) total_lines += static_cast<int>(h.lines.size());
                parsed_files.push_back(std::move(pf));
            }
        } else {
            parsed_files.push_back(file);
        }
    }
    print_success("Parsed " + std::to_string(total_hunks) + " diff hunks, " +
                  std::to_string(total_lines) + " changed lines");

    // Step 4: AI analysis
    auto provider = config.active_provider();
    print_step("Calling AI analysis engine (" + provider.name + ", " + provider.model + ")...", 4, total_steps);

    ai_pr_reviewer::AIAnalyzer analyzer(config);
    analyzer.set_progress_callback([](const std::string& msg) {
        spdlog::info("  {}", msg);
    });

    auto result = analyzer.analyze(pr_meta, parsed_files);
    print_success("Analysis complete: " + std::to_string(result.total_issues) + " issues found");

    // Step 5: Generate HTML report
    print_step("Generating HTML report...", 5, total_steps);

    ai_pr_reviewer::HtmlReportGenerator reporter(config);
    bool report_ok = reporter.generate_report(pr_meta, result, config.output_path);

    if (report_ok) {
        print_success("Report saved to: " + config.output_path);
    } else {
        print_error("Failed to save report to: " + config.output_path);
        return 1;
    }

    print_result_summary(result);
    return 0;
}

// ============================================================================
// Main entry point
// ============================================================================
int main(int argc, char* argv[]) {
    auto console = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(console);
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("%^[%L]%$ %v");

    // Check for vcpkg proxy artifact that can break GitHub API calls
    const char* proxy = std::getenv("HTTP_PROXY");
    if (proxy) {
        std::string ps(proxy);
        if (ps.find("127.0.0.1:7890") != std::string::npos || ps.find("localhost") != std::string::npos) {
            spdlog::warn("HTTP_PROXY={} detected (likely vcpkg artifact)", ps);
            spdlog::warn("If GitHub API calls fail, unset it: $env:HTTP_PROXY=$null");
        }
    }

    // No arguments: show friendly banner and exit gracefully (prevents flash-close)
    if (argc <= 1) {
        print_header();
        std::cout << "  This is a command-line tool. Please run from a terminal:\n\n";
        std::cout << "    ai-pr-reviewer --help              Show all options\n";
        std::cout << "    ai-pr-reviewer --discover          Browse repos and PRs\n";
        std::cout << "    ai-pr-reviewer --pr-url <URL>      Analyze a specific PR\n\n";
        std::cout << "  Quick start:\n";
        std::cout << "    set OPENAI_API_KEY=sk-your-key\n";
        std::cout << "    ai-pr-reviewer --pr-url https://github.com/user/repo/pull/1\n\n";
        std::cout << "  Press any key to exit...";
        std::cin.get();
        return 0;
    }

    try {
        auto config = ai_pr_reviewer::ConfigManager::load(argc, argv);
        print_header();

        // --- Mode: Discovery (interactive repo/PR selection) ---
        if (config.discover_mode) {
            ai_pr_reviewer::GitHubClient github(config);

            // Step A: Select repository
            std::string owner, repo;
            if (!select_repo(github, config.repo_filter, owner, repo)) {
                return 1;
            }

            // Step B: Select PR
            int pr_number = 0;
            if (!select_pr(github, owner, repo, config.pr_state_filter, pr_number)) {
                return 1;
            }

            // Update config with selected values
            config.owner = owner;
            config.repo = repo;
            config.pr_number = pr_number;

            return run_review(config);
        }

        // --- Mode: List repos only ---
        if (config.list_repos) {
            ai_pr_reviewer::GitHubClient github(config);
            std::string dummy_owner, dummy_repo;
            select_repo(github, config.repo_filter, dummy_owner, dummy_repo);
            // select_repo already prints the repo list, just exit
            return 0;
        }

        // --- Mode: Standard PR review ---
        // Display active AI provider info
        auto provider = config.active_provider();
        if (!config.ai_providers.empty()) {
            spdlog::info("AI Provider: {} ({})", provider.name, provider.model);
        }

        return run_review(config);

    } catch (const std::exception& e) {
        print_error(e.what());
        return 1;
    } catch (...) {
        print_error("Unknown error occurred");
        return 1;
    }
}
