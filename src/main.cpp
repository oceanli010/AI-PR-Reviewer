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

// ============================================================================
// Progress bar helper
// ============================================================================
void print_header() {
    std::cout << "\n";
    std::cout << "  ============================================================\n";
    std::cout << "     AI PR Reviewer v1.0\n";
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

// ============================================================================
// Main entry point
// ============================================================================
int main(int argc, char* argv[]) {
    // Setup logging
    auto console = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(console);
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("%^[%L]%$ %v");

    try {
        // Step 1: Load configuration
        auto config = ai_pr_reviewer::ConfigManager::load(argc, argv);

        print_header();

        int total_steps = 5;
        if (config.verbose) total_steps = 5;

        // Step 1: Fetch PR data
        print_step("Fetching PR data from GitHub...", 1, total_steps);

        ai_pr_reviewer::GitHubClient github(config);
        github.set_progress_callback([](const std::string& msg) {
            spdlog::info("  {}", msg);
        });

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

        // Filter out binary files
        int binary_count = 0;
        for (const auto& f : files) {
            if (f.is_binary) binary_count++;
        }

        int analyzable = static_cast<int>(files.size()) - binary_count;
        spdlog::info("  Files: {} total, {} analyzable, {} binary",
                     files.size(), analyzable, binary_count);

        // Step 3: Parse diffs
        print_step("Parsing diffs into structured data...", 3, total_steps);

        ai_pr_reviewer::DiffParser parser;
        std::vector<ai_pr_reviewer::FileChange> parsed_files;
        int total_hunks = 0;
        int total_lines = 0;

        for (auto& file : files) {
            if (!file.is_binary && !file.raw_diff.empty()) {
                auto parsed = parser.parse("diff --git a/" + file.filename + " b/" + file.filename + "\n" + file.raw_diff);
                for (auto& pf : parsed) {
                    pf.filename = file.filename;
                    pf.status = file.status;
                    pf.additions = file.additions;
                    pf.deletions = file.deletions;
                    pf.changes = file.changes;
                    pf.language = file.language;
                    pf.is_binary = file.is_binary;
                    total_hunks += static_cast<int>(pf.hunks.size());
                    for (const auto& h : pf.hunks) {
                        total_lines += static_cast<int>(h.lines.size());
                    }
                    parsed_files.push_back(std::move(pf));
                }
            } else {
                // Binary files: pass through without parsing
                parsed_files.push_back(file);
            }
        }

        print_success("Parsed " + std::to_string(total_hunks) + " diff hunks, " +
                      std::to_string(total_lines) + " changed lines");

        // Step 4: AI analysis
        print_step("Calling AI analysis engine (OpenAI GPT-4o)...", 4, total_steps);

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

        // Print summary
        print_result_summary(result);

        return 0;

    } catch (const std::exception& e) {
        print_error(e.what());
        return 1;
    } catch (...) {
        print_error("Unknown error occurred");
        return 1;
    }
}
