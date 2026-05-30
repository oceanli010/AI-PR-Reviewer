#include "ai_pr_reviewer/html_report.h"

#include <inja/inja.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <ctime>

namespace ai_pr_reviewer {

// ============================================================================
// Constructor
// ============================================================================
HtmlReportGenerator::HtmlReportGenerator(const AppConfig& config)
    : config_(config) {
    load_template();
}

// ============================================================================
// Template Loading
// ============================================================================
void HtmlReportGenerator::load_template() {
    // Try loading custom template path first
    if (!config_.template_path.empty()) {
        std::ifstream file(config_.template_path);
        if (file.is_open()) {
            std::ostringstream ss;
            ss << file.rdbuf();
            template_content_ = ss.str();
            template_loaded_ = true;
            spdlog::info("Loaded custom template: {}", config_.template_path);
            return;
        }
        spdlog::warn("Custom template not found: {}, using built-in template",
            config_.template_path);
    }

    // Try loading bundled template from templates/ directory
    std::ifstream bundled("templates/report.html");
    if (bundled.is_open()) {
        std::ostringstream ss;
        ss << bundled.rdbuf();
        template_content_ = ss.str();
        template_loaded_ = true;
        spdlog::debug("Loaded bundled template");
        return;
    }

    // Fall back to built-in template
    template_content_ = builtin_template();
    template_loaded_ = true;
    spdlog::debug("Using built-in default template");
}

// ============================================================================
// Render
// ============================================================================
std::string HtmlReportGenerator::render(const PrMetadata& pr_meta,
                                          const ReviewResult& result,
                                          const std::vector<FileChange>* file_changes) {
    if (!template_loaded_) {
        load_template();
    }

    nlohmann::json data = build_template_data(pr_meta, result, file_changes);

    try {
        inja::Environment env;
        env.set_trim_blocks(true);
        env.set_lstrip_blocks(true);

        auto tmpl = env.parse(template_content_);
        std::string html = env.render(tmpl, data);

        spdlog::debug("HTML report rendered: {} bytes", html.size());
        return html;
    } catch (const std::exception& e) {
        spdlog::error("Template rendering failed: {}", e.what());
        throw std::runtime_error("Failed to render HTML report: " + std::string(e.what()));
    }
}

// ============================================================================
// Generate and Save Report
// ============================================================================
bool HtmlReportGenerator::generate_report(const PrMetadata& pr_meta,
                                            const ReviewResult& result,
                                            const std::string& output_path) {
    return generate_report(pr_meta, result, {}, output_path);
}

bool HtmlReportGenerator::generate_report(const PrMetadata& pr_meta,
                                            const ReviewResult& result,
                                            const std::vector<FileChange>& file_changes,
                                            const std::string& output_path) {
    try {
        const std::vector<FileChange>* fc_ptr =
            file_changes.empty() ? nullptr : &file_changes;
        std::string html = render(pr_meta, result, fc_ptr);

        std::ofstream output(output_path, std::ios::out | std::ios::binary);
        if (!output.is_open()) {
            spdlog::error("Failed to open output file: {}", output_path);
            return false;
        }

        output << html;
        output.close();

        spdlog::info("Report saved to: {}", output_path);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to generate report: {}", e.what());
        return false;
    }
}

// ============================================================================
// Template Data Building
// ============================================================================
nlohmann::json HtmlReportGenerator::build_template_data(const PrMetadata& pr_meta,
                                                          const ReviewResult& result,
                                                          const std::vector<FileChange>* file_changes) {
    nlohmann::json data;

    // PR metadata
    data["pr_title"] = escape_html(pr_meta.title);
    data["pr_number"] = pr_meta.number;
    data["pr_author"] = escape_html(pr_meta.author);
    data["pr_author_avatar"] = pr_meta.author_avatar;
    // Convert Markdown PR description to HTML for proper rendering
    data["pr_description"] = pr_meta.description.empty()
        ? "" : markdown_to_html(pr_meta.description);
    data["pr_description_raw"] = escape_html(pr_meta.description);
    data["has_description"] = !pr_meta.description.empty();
    data["pr_url"] = pr_meta.html_url;
    data["pr_created"] = pr_meta.created_at;
    data["pr_updated"] = pr_meta.updated_at;
    data["base_branch"] = pr_meta.base_branch;
    data["head_branch"] = pr_meta.head_branch;

    // PR stats
    data["files_changed"] = pr_meta.changed_files;
    data["additions"] = pr_meta.additions;
    data["deletions"] = pr_meta.deletions;
    data["commits"] = pr_meta.commit_count;

    // Review results
    data["summary"] = escape_html(result.summary);
    data["overall_assessment"] = escape_html(result.overall_assessment);
    data["total_issues"] = result.total_issues;
    data["critical_count"] = result.critical_count;
    data["high_count"] = result.high_count;
    data["medium_count"] = result.medium_count;
    data["low_count"] = result.low_count;
    data["info_count"] = result.info_count;

    // Generate timestamp
    auto now = std::time(nullptr);
    char time_buf[64];
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    data["generated_at"] = std::string(time_buf);

    // Risk items
    data["risk_items"] = nlohmann::json::array();
    for (const auto& item : result.risk_items) {
        nlohmann::json item_json;
        item_json["file"] = escape_html(item.file);
        item_json["line"] = item.line;
        item_json["severity"] = severity_label(item.severity);
        item_json["severity_class"] = severity_class(item.severity);
        item_json["severity_color"] = severity_color(item.severity);
        item_json["category"] = escape_html(item.category);
        item_json["title"] = escape_html(item.title);
        item_json["description"] = escape_html(item.description);
        item_json["suggestion"] = escape_html(item.suggestion);
        item_json["code_snippet"] = escape_html(item.code_snippet);
        data["risk_items"].push_back(item_json);
    }

    // File changes diff data for code changes section
    if (file_changes && !file_changes->empty()) {
        data["has_diff_data"] = true;
        data["diff_files"] = build_diff_data(*file_changes);
    } else {
        data["has_diff_data"] = false;
    }

    return data;
}

// ============================================================================
// Severity Helpers
// ============================================================================
std::string HtmlReportGenerator::severity_class(Severity sev) {
    switch (sev) {
        case Severity::Critical: return "severity-critical";
        case Severity::High:     return "severity-high";
        case Severity::Medium:   return "severity-medium";
        case Severity::Low:      return "severity-low";
        default:                 return "severity-info";
    }
}

std::string HtmlReportGenerator::severity_label(Severity sev) {
    switch (sev) {
        case Severity::Critical: return "Critical";
        case Severity::High:     return "High";
        case Severity::Medium:   return "Medium";
        case Severity::Low:      return "Low";
        default:                 return "Info";
    }
}

std::string HtmlReportGenerator::severity_color(Severity sev) {
    switch (sev) {
        case Severity::Critical: return "#E34D59";  // TDesign Error / Red
        case Severity::High:     return "#ED7B2F";  // TDesign Warning / Orange
        case Severity::Medium:   return "#EBB105";  // TDesign Caution / Yellow
        case Severity::Low:      return "#0052D9";  // TDesign Brand / Blue
        default:                 return "#00A870";  // TDesign Success / Green
    }
}

// ============================================================================
// HTML Escaping
// ============================================================================
std::string HtmlReportGenerator::escape_html(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size());

    for (char c : text) {
        switch (c) {
            case '&':  escaped += "&amp;";  break;
            case '<':  escaped += "&lt;";   break;
            case '>':  escaped += "&gt;";   break;
            case '"':  escaped += "&quot;"; break;
            case '\'': escaped += "&#39;";  break;
            default:   escaped += c;        break;
        }
    }

    return escaped;
}

// ============================================================================
// Markdown to HTML (basic conversion for PR descriptions)
// ============================================================================
std::string HtmlReportGenerator::markdown_to_html(const std::string& md) {
    if (md.empty()) return "";

    std::string result;
    result.reserve(md.size() * 2);
    bool in_paragraph = false;
    bool in_code_block = false;
    bool in_list = false;

    auto close_paragraph = [&]() {
        if (in_paragraph) { result += "</p>\n"; in_paragraph = false; }
    };
    auto close_list = [&]() {
        if (in_list) { result += "</ul>\n"; in_list = false; }
    };

    std::istringstream stream(md);
    std::string line;
    while (std::getline(stream, line)) {
        // Trim trailing \r
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Code block toggle
        if (line.size() >= 3 && line.substr(0, 3) == "```") {
            close_paragraph();
            close_list();
            if (in_code_block) {
                result += "</code></pre>\n";
                in_code_block = false;
            } else {
                result += "<pre><code>";
                in_code_block = true;
            }
            continue;
        }

        if (in_code_block) {
            result += escape_html(line) + "\n";
            continue;
        }

        // Empty line: close paragraph
        if (line.empty()) {
            close_paragraph();
            result += "\n";
            continue;
        }

        // Headings: # ## ###
        if (line[0] == '#' && line.size() > 1 && line[1] == ' ') {
            close_paragraph();
            close_list();
            size_t level = 1;
            while (level < line.size() && line[level] == '#') level++;
            if (level < line.size() && line[level] == ' ') level++;
            size_t h_level = std::min(level > 2 ? level - 1 : level, size_t(6));
            if (h_level < 3) h_level = 3;
            std::string content = escape_html(line.substr(level));
            result += "<h" + std::to_string(h_level) + ">" + content + "</h" + std::to_string(h_level) + ">\n";
            continue;
        }

        // Unordered list: - or *
        if ((line[0] == '-' || line[0] == '*') && line.size() > 1 && line[1] == ' ') {
            close_paragraph();
            if (!in_list) { result += "<ul>\n"; in_list = true; }
            std::string content = line.substr(2);
            // Inline formatting
            content = escape_html(content);
            // **bold**
            std::regex bold(R"(\*\*(.+?)\*\*)");
            content = std::regex_replace(content, bold, "<strong>$1</strong>");
            // `code`
            std::regex code(R"(`(.+?)`)");
            content = std::regex_replace(content, code, "<code>$1</code>");
            // [link](url)
            std::regex link(R"(\[([^\]]+)\]\(([^)]+)\))");
            content = std::regex_replace(content, link, "<a href=\"$2\">$1</a>");
            result += "<li>" + content + "</li>\n";
            continue;
        }

        // Regular paragraph text
        close_list();
        if (!in_paragraph) { result += "<p>"; in_paragraph = true; }

        std::string html_line = escape_html(line);
        // **bold**
        std::regex bold(R"(\*\*(.+?)\*\*)");
        html_line = std::regex_replace(html_line, bold, "<strong>$1</strong>");
        // `code`
        std::regex code(R"(`(.+?)`)");
        html_line = std::regex_replace(html_line, code, "<code>$1</code>");
        // [link](url)
        std::regex link(R"(\[([^\]]+)\]\(([^)]+)\))");
        html_line = std::regex_replace(html_line, link, "<a href=\"$2\">$1</a>");

        result += html_line + "<br>\n";
    }

    close_paragraph();
    close_list();
    if (in_code_block) { result += "</code></pre>\n"; }

    return result;
}

// ============================================================================
// Build diff data for code changes section
// ============================================================================
nlohmann::json HtmlReportGenerator::build_diff_data(const std::vector<FileChange>& files) {
    nlohmann::json diff_files = nlohmann::json::array();

    for (const auto& file : files) {
        if (file.is_binary) continue;

        nlohmann::json f;
        f["filename"] = escape_html(file.filename);
        f["status"] = file.status;
        f["language"] = file.language;
        f["additions"] = file.additions;
        f["deletions"] = file.deletions;

        // Build colorized diff lines
        nlohmann::json diff_lines = nlohmann::json::array();
        nlohmann::json diff_text = nlohmann::json::array();

        for (const auto& hunk : file.hunks) {
            // Add hunk header as context line
            if (!hunk.header.empty()) {
                nlohmann::json hl;
                hl["type"] = "header";
                hl["content"] = escape_html(hunk.header);
                diff_lines.push_back(hl);
                diff_text.push_back(hunk.header);
            }

            for (const auto& dline : hunk.lines) {
                nlohmann::json dl;
                switch (dline.type) {
                    case LineType::Addition:
                        dl["type"] = "addition";
                        dl["prefix"] = "+";
                        break;
                    case LineType::Deletion:
                        dl["type"] = "deletion";
                        dl["prefix"] = "-";
                        break;
                    default:
                        dl["type"] = "context";
                        dl["prefix"] = " ";
                        break;
                }
                dl["content"] = escape_html(dline.content);
                dl["old_lineno"] = dline.old_lineno;
                dl["new_lineno"] = dline.new_lineno;
                diff_lines.push_back(dl);
                diff_text.push_back(dl["prefix"].get<std::string>() + dline.content);
            }
        }

        f["diff_lines"] = diff_lines;
        f["has_diff"] = !diff_lines.empty();

        // Limit to first 200 lines for display
        if (diff_lines.size() > 200) {
            nlohmann::json trunc;
            trunc["type"] = "truncation";
            trunc["content"] = "... (showing first 200 of " +
                std::to_string(diff_lines.size()) + " changed lines)";
            // Truncate the array
            nlohmann::json truncated = nlohmann::json::array();
            for (size_t i = 0; i < 200 && i < diff_lines.size(); i++) {
                truncated.push_back(diff_lines[i]);
            }
            truncated.push_back(trunc);
            f["diff_lines"] = truncated;
        }

        diff_files.push_back(f);
    }

    return diff_files;
}

// ============================================================================
// Built-in Default Template
// ============================================================================
std::string HtmlReportGenerator::builtin_template() {
    std::string t;
    t  = R"TEMPLATE(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>PR #{{ pr_number }} Review Report - {{ pr_title }}</title>
<style>
/* ============================================================
   TDesign-Inspired Design System for AI PR Reviewer Report
   ============================================================ */
:root {
  --td-brand-color: #0052D9;
  --td-brand-color-light: #ECF2FE;
  --td-brand-color-hover: #366EF4;
  --td-error-color: #E34D59;
  --td-error-color-light: #FDECEE;
  --td-warning-color: #ED7B2F;
  --td-warning-color-light: #FEF3E6;
  --td-success-color: #00A870;
  --td-success-color-light: #E8F8F2;
  --td-info-color: #0052D9;
  --td-caution-color: #EBB105;

  --td-bg-color-page: #F5F7FA;
  --td-bg-color-container: #FFFFFF;
  --td-bg-color-container-hover: #F3F3F3;
  --td-bg-color-component: #F3F3F3;

  --td-text-color-primary: #000000E6;
  --td-text-color-secondary: #00000099;
  --td-text-color-placeholder: #00000066;
  --td-text-color-disabled: #00000042;

  --td-border-color: #DCDCDC;
  --td-border-color-light: #EEEEEE;

  --td-radius-small: 3px;
  --td-radius-default: 6px;
  --td-radius-medium: 9px;
  --td-radius-large: 12px;

  --td-shadow-1: 0 1px 4px rgba(0,0,0,0.06);
  --td-shadow-2: 0 2px 8px rgba(0,0,0,0.08);
  --td-shadow-3: 0 4px 16px rgba(0,0,0,0.12);

  --td-font-size-xs: 12px;
  --td-font-size-sm: 13px;
  --td-font-size-base: 14px;
  --td-font-size-md: 16px;
  --td-font-size-lg: 18px;
  --td-font-size-xl: 20px;
  --td-font-size-xxl: 24px;

  --td-line-height: 1.6;
  --td-font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, "Noto Sans SC", "PingFang SC", "Microsoft YaHei", sans-serif;
}

* {
  margin: 0;
  padding: 0;
  box-sizing: border-box;
}

body {
  font-family: var(--td-font-family);
  font-size: var(--td-font-size-base);
  line-height: var(--td-line-height);
  color: var(--td-text-color-primary);
  background: var(--td-bg-color-page);
  -webkit-font-smoothing: antialiased;
}

/* ---- Layout ---- */
.page-wrapper {
  max-width: 1100px;
  margin: 0 auto;
  padding: 24px 20px;
}

/* ---- Header ---- */
.report-header {
  background: linear-gradient(135deg, var(--td-brand-color) 0%, #2856E8 100%);
  color: #FFFFFF;
  border-radius: var(--td-radius-large);
  padding: 32px 36px;
  margin-bottom: 24px;
  box-shadow: var(--td-shadow-2);
}

.report-header .badge {
  display: inline-block;
  background: rgba(255,255,255,0.2);
  padding: 4px 12px;
  border-radius: 100px;
  font-size: var(--td-font-size-sm);
  font-weight: 500;
  letter-spacing: 0.5px;
  margin-bottom: 12px;
}

.report-header h1 {
  font-size: var(--td-font-size-xxl);
  font-weight: 700;
  line-height: 1.3;
  margin-bottom: 8px;
}

.report-header h1 .pr-num {
  opacity: 0.75;
  font-weight: 400;
}

.report-header .pr-meta {
  display: flex;
  align-items: center;
  gap: 16px;
  margin-top: 16px;
  font-size: var(--td-font-size-sm);
  opacity: 0.9;
  flex-wrap: wrap;
}

.report-header .pr-meta .author-avatar {
  width: 28px;
  height: 28px;
  border-radius: 50%;
  border: 2px solid rgba(255,255,255,0.3);
}

/* ---- Stats Cards ---- */
.stats-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(160px, 1fr));
  gap: 16px;
  margin-bottom: 24px;
}

.stat-card {
  background: var(--td-bg-color-container);
  border-radius: var(--td-radius-medium);
  padding: 20px 24px;
  box-shadow: var(--td-shadow-1);
  display: flex;
  flex-direction: column;
  align-items: center;
  text-align: center;
  transition: transform 0.15s ease, box-shadow 0.15s ease;
}

.stat-card:hover {
  transform: translateY(-2px);
  box-shadow: var(--td-shadow-2);
}

.stat-card .stat-number {
  font-size: 32px;
  font-weight: 700;
  line-height: 1.2;
  margin-bottom: 4px;
}

.stat-card .stat-label {
  font-size: var(--td-font-size-xs);
  color: var(--td-text-color-secondary);
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.stat-card.critical .stat-number { color: var(--td-error-color); }
.stat-card.high .stat-number     { color: var(--td-warning-color); }
.stat-card.medium .stat-number   { color: var(--td-caution-color); }
.stat-card.low .stat-number      { color: var(--td-brand-color); }
.stat-card.info .stat-number     { color: var(--td-success-color); }
.stat-card.files .stat-number    { color: var(--td-brand-color); }
.stat-card.additions .stat-number { color: var(--td-success-color); }
.stat-card.deletions .stat-number { color: var(--td-error-color); }

/* ---- Section ---- */
.section {
  background: var(--td-bg-color-container);
  border-radius: var(--td-radius-medium);
  padding: 24px;
  margin-bottom: 20px;
  box-shadow: var(--td-shadow-1);
}

.section-title {
  font-size: var(--td-font-size-lg);
  font-weight: 600;
  color: var(--td-text-color-primary);
  margin-bottom: 16px;
  padding-bottom: 12px;
  border-bottom: 1px solid var(--td-border-color-light);
  display: flex;
  align-items: center;
  gap: 8px;
}

.section-title .icon {
  width: 24px;
  height: 24px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
}

/* ---- Summary Text ---- */
.summary-text {
  font-size: var(--td-font-size-md);
  line-height: 1.8;
  color: var(--td-text-color-primary);
}

.overall-assessment {
  margin-top: 12px;
  padding: 16px;
  background: var(--td-brand-color-light);
  border-radius: var(--td-radius-default);
  border-left: 3px solid var(--td-brand-color);
  font-size: var(--td-font-size-base);
  color: var(--td-text-color-primary);
}

/* ---- Issue Card ---- */
.issue-cards {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.issue-card {
  border: 1px solid var(--td-border-color-light);
  border-radius: var(--td-radius-medium);
  overflow: hidden;
  transition: box-shadow 0.15s ease;
}

.issue-card:hover {
  box-shadow: var(--td-shadow-1);
}

.issue-card-header {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 14px 20px;
  background: var(--td-bg-color-component);
  cursor: default;
}

.issue-card-header .severity-badge {
  padding: 2px 10px;
  border-radius: 100px;
  font-size: var(--td-font-size-xs);
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  white-space: nowrap;
}

.issue-card-header .category-tag {
  padding: 2px 8px;
  border-radius: var(--td-radius-small);
  font-size: var(--td-font-size-xs);
  background: var(--td-bg-color-container);
  border: 1px solid var(--td-border-color-light);
  color: var(--td-text-color-secondary);
}

.issue-card-header .issue-title {
  font-weight: 600;
  font-size: var(--td-font-size-base);
  color: var(--td-text-color-primary);
  flex: 1;
}

.issue-card-header .file-location {
  font-size: var(--td-font-size-sm);
  color: var(--td-text-color-secondary);
  font-family: "SF Mono", "Cascadia Code", "Fira Code", "Consolas", monospace;
}

.issue-card-body {
  padding: 16px 20px;
}

.issue-card-body p {
  margin-bottom: 8px;
  font-size: var(--td-font-size-base);
  line-height: 1.7;
}

.issue-card-body .label {
  font-weight: 600;
  font-size: var(--td-font-size-sm);
  color: var(--td-text-color-secondary);
  display: block;
  margin-bottom: 4px;
}

.issue-card-body .code-block {
  background: #1E1E1E;
  color: #D4D4D4;
  border-radius: var(--td-radius-default);
  padding: 12px 16px;
  font-family: "SF Mono", "Cascadia Code", "Fira Code", "Consolas", monospace;
  font-size: var(--td-font-size-sm);
  line-height: 1.6;
  overflow-x: auto;
  white-space: pre-wrap;
  word-break: break-all;
  margin-top: 8px;
}
)TEMPLATE";
    t += R"TEMPLATE(
/* ---- Severity Colors ---- */
.severity-critical { background: var(--td-error-color); color: #FFFFFF; }
.severity-high     { background: var(--td-warning-color); color: #FFFFFF; }
.severity-medium   { background: var(--td-caution-color); color: #FFFFFF; }
.severity-low      { background: var(--td-brand-color); color: #FFFFFF; }
.severity-info     { background: var(--td-success-color); color: #FFFFFF; }

/* ---- Issue Summary Bar ---- */
.issue-summary-bar {
  display: flex;
  gap: 8px;
  align-items: center;
  margin-bottom: 16px;
  flex-wrap: wrap;
}

.issue-summary-bar .count-item {
  display: flex;
  align-items: center;
  gap: 4px;
  padding: 4px 12px;
  border-radius: 100px;
  font-size: var(--td-font-size-sm);
  font-weight: 500;
}

.issue-summary-bar .count-item.critical { background: var(--td-error-color-light); color: var(--td-error-color); }
.issue-summary-bar .count-item.high     { background: var(--td-warning-color-light); color: var(--td-warning-color); }
.issue-summary-bar .count-item.medium   { background: #FEF9E6; color: #C59400; }
.issue-summary-bar .count-item.low      { background: var(--td-brand-color-light); color: var(--td-brand-color); }
.issue-summary-bar .count-item.info     { background: var(--td-success-color-light); color: var(--td-success-color); }

/* ---- Empty State ---- */
.empty-state {
  text-align: center;
  padding: 48px 24px;
  color: var(--td-text-color-secondary);
}

.empty-state .empty-icon {
  font-size: 48px;
  margin-bottom: 12px;
  opacity: 0.5;
}

.empty-state p {
  font-size: var(--td-font-size-md);
}

/* ---- Diff View ---- */
.diff-file {
  margin-bottom: 16px;
  border: 1px solid var(--td-border-color-light);
  border-radius: var(--td-radius-default);
  overflow: hidden;
}
.diff-file-header {
  display: flex; align-items: center; gap: 10px;
  padding: 10px 16px;
  background: var(--td-bg-color-component);
  font-size: var(--td-font-size-sm);
  flex-wrap: wrap;
}
.diff-file-header .diff-status {
  padding: 2px 8px; border-radius: 100px;
  font-weight: 600; font-size: var(--td-font-size-xs); text-transform: uppercase;
}
.diff-file-header .diff-status.Added    { background: #E8F8F2; color: var(--td-success-color); }
.diff-file-header .diff-status.Modified { background: var(--td-brand-color-light); color: var(--td-brand-color); }
.diff-file-header .diff-status.Removed  { background: var(--td-error-color-light); color: var(--td-error-color); }
.diff-file-header .diff-status.Renamed  { background: #FEF3E6; color: var(--td-warning-color); }
.diff-file-header .diff-filename {
  font-weight: 600; font-family: "SF Mono", "Consolas", monospace; flex: 1;
}
.diff-file-header .diff-stats { color: var(--td-text-color-secondary); }
.diff-file-header .diff-language {
  color: var(--td-text-color-placeholder); font-size: var(--td-font-size-xs);
}
.diff-view {
  font-family: "SF Mono", "Cascadia Code", "Fira Code", "Consolas", monospace;
  font-size: 12px; line-height: 1.6; overflow-x: auto;
  background: #FAFBFC;
}
.diff-line {
  display: flex; align-items: baseline; padding: 1px 0;
  white-space: pre; min-height: 20px;
}
.diff-line.diff-addition { background: #E6FFEC; }
.diff-line.diff-deletion { background: #FFEBE9; }
.diff-line.diff-context { background: transparent; }
.diff-line.diff-header  { background: #F0F1F4; color: #656D76; font-weight: 600; }
.diff-line.diff-truncation { background: #FFF8C5; color: #9A6700; text-align: center; justify-content: center; }
.diff-lineno {
  width: 45px; text-align: right; padding-right: 8px;
  color: #8B949E; user-select: none; flex-shrink: 0;
}
.diff-prefix {
  width: 16px; text-align: center; font-weight: 700; flex-shrink: 0;
}
.diff-addition .diff-prefix { color: var(--td-success-color); }
.diff-deletion .diff-prefix { color: var(--td-error-color); }
.diff-content { flex: 1; }

/* ---- PR Description (MD rendered) ---- */
.pr-description {
  line-height: 1.8; font-size: var(--td-font-size-base);
}
.pr-description h3, .pr-description h4 {
  margin: 16px 0 8px 0; color: var(--td-text-color-primary);
}
.pr-description ul { padding-left: 24px; margin: 8px 0; }
.pr-description li { margin: 4px 0; }
.pr-description p { margin: 8px 0; }
.pr-description code {
  background: var(--td-bg-color-component); padding: 2px 6px;
  border-radius: var(--td-radius-small); font-family: "SF Mono", "Consolas", monospace;
  font-size: 0.9em;
}
.pr-description pre {
  background: #1E1E1E; color: #D4D4D4; padding: 12px 16px;
  border-radius: var(--td-radius-default); overflow-x: auto; margin: 8px 0;
}
.pr-description pre code { background: none; padding: 0; color: inherit; }
.pr-description a { color: var(--td-brand-color); text-decoration: none; }
.pr-description a:hover { text-decoration: underline; }
.pr-description strong { font-weight: 600; }

/* ---- Footer ---- */
.report-footer {
  text-align: center;
  padding: 24px;
  color: var(--td-text-color-placeholder);
  font-size: var(--td-font-size-sm);
}

/* ---- PR Changes Summary ---- */
.pr-summary-list {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  align-items: center;
}

.pr-summary-list .change-stat {
  padding: 4px 12px;
  border-radius: 100px;
  font-size: var(--td-font-size-sm);
  font-weight: 600;
}

.pr-summary-list .branch-tag {
  padding: 4px 10px;
  background: var(--td-bg-color-component);
  border-radius: var(--td-radius-small);
  font-family: monospace;
  font-size: var(--td-font-size-sm);
  color: var(--td-text-color-secondary);
}

/* ---- Responsive ---- */
@media (max-width: 768px) {
  .page-wrapper { padding: 16px 12px; }
  .report-header { padding: 20px 24px; }
  .stats-grid { grid-template-columns: repeat(2, 1fr); }
  .section { padding: 16px; }
}
</style>
</head>
<body>
<div class="page-wrapper">

  <!-- ===== HEADER ===== -->
  <header class="report-header">
    <div class="badge">AI Code Review Report</div>
    <h1>
      {% if pr_title %}{{ pr_title }}{% else %}Untitled PR{% endif %}
      <span class="pr-num">#{{ pr_number }}</span>
    </h1>
    <div class="pr-meta">
      {% if pr_author_avatar %}
      <img class="author-avatar" src="{{ pr_author_avatar }}" alt="{{ pr_author }}">
      {% endif %}
      <span>by <strong>{{ pr_author }}</strong></span>
      <span>|</span>
      <span>{{ base_branch }} &larr; {{ head_branch }}</span>
      <span>|</span>
      <span>Created: {{ pr_created }}</span>
    </div>
  </header>

  <!-- ===== STATS CARDS ===== -->
  <div class="stats-grid">
    <div class="stat-card critical">
      <div class="stat-number">{{ critical_count }}</div>
      <div class="stat-label">Critical</div>
    </div>
    <div class="stat-card high">
      <div class="stat-number">{{ high_count }}</div>
      <div class="stat-label">High</div>
    </div>
    <div class="stat-card medium">
      <div class="stat-number">{{ medium_count }}</div>
      <div class="stat-label">Medium</div>
    </div>
    <div class="stat-card low">
      <div class="stat-number">{{ low_count }}</div>
      <div class="stat-label">Low</div>
    </div>
    <div class="stat-card files">
      <div class="stat-number">{{ files_changed }}</div>
      <div class="stat-label">Files</div>
    </div>
    <div class="stat-card additions">
      <div class="stat-number">+{{ additions }}</div>
      <div class="stat-label">Additions</div>
    </div>
    <div class="stat-card deletions">
      <div class="stat-number">-{{ deletions }}</div>
      <div class="stat-label">Deletions</div>
    </div>
  </div>

  <!-- ===== SUMMARY ===== -->
  <section class="section">
    <h2 class="section-title">
      <span class="icon">&#128203;</span> Summary
    </h2>
    {% if summary %}
    <p class="summary-text">{{ summary }}</p>
    {% endif %}

    {% if overall_assessment %}
    <div class="overall-assessment">
      <strong>Overall Assessment:</strong> {{ overall_assessment }}
    </div>
    {% endif %}
  </section>

  <!-- ===== CODE CHANGES ===== -->
  {% if has_diff_data %}
  <section class="section">
    <h2 class="section-title">
      <span class="icon">&#128196;</span> Code Changes
    </h2>
    {% for file in diff_files %}
    <div class="diff-file">
      <div class="diff-file-header">
        <span class="diff-status {{ file.status }}">{{ file.status }}</span>
        <span class="diff-filename">{{ file.filename }}</span>
        <span class="diff-stats">+{{ file.additions }}/-{{ file.deletions }}</span>
        {% if file.language %}<span class="diff-language">{{ file.language }}</span>{% endif %}
      </div>
      {% if file.has_diff %}
      <div class="diff-view">
        {% for line in file.diff_lines %}
        {% if line.type == "truncation" %}
        <div class="diff-line diff-truncation">{{ line.content }}</div>
        {% else if line.type == "header" %}
        <div class="diff-line diff-header">{{ line.content }}</div>
        {% else %}
        <div class="diff-line diff-{{ line.type }}">
          <span class="diff-lineno">{{ line.old_lineno }}</span>
          <span class="diff-lineno">{{ line.new_lineno }}</span>
          <span class="diff-prefix">{{ line.prefix }}</span>
          <span class="diff-content">{{ line.content }}</span>
        </div>
        {% endif %}
        {% endfor %}
      </div>
      {% endif %}
    </div>
    {% endfor %}
  </section>
  {% endif %}

  <!-- ===== ISSUES ===== -->
  <section class="section">
    <h2 class="section-title">
      <span class="icon">&#9888;</span> Issues Found
      <span style="font-weight:400;font-size:14px;color:var(--td-text-color-secondary);margin-left:8px;">
        ({{ total_issues }} total)
      </span>
    </h2>

    {% if total_issues > 0 %}
    <div class="issue-summary-bar">
      {% if critical_count > 0 %}<span class="count-item critical">{{ critical_count }} Critical</span>{% endif %}
      {% if high_count > 0 %}<span class="count-item high">{{ high_count }} High</span>{% endif %}
      {% if medium_count > 0 %}<span class="count-item medium">{{ medium_count }} Medium</span>{% endif %}
      {% if low_count > 0 %}<span class="count-item low">{{ low_count }} Low</span>{% endif %}
      {% if info_count > 0 %}<span class="count-item info">{{ info_count }} Info</span>{% endif %}
    </div>

    <div class="issue-cards">
      {#{% for item in risk_items %} Comment syntax left open to use inja's {% raw %} or workaround approach #}
      {% for item in risk_items %}
      <div class="issue-card">
        <div class="issue-card-header">
          <span class="severity-badge {{ item.severity_class }}">{{ item.severity }}</span>
          <span class="category-tag">{{ item.category }}</span>
          <span class="issue-title">{{ item.title }}</span>
          <span class="file-location">{{ item.file }}{% if item.line > 0 %}:{{ item.line }}{% endif %}</span>
        </div>
        <div class="issue-card-body">
          {% if item.description %}
          <span class="label">Description</span>
          <p>{{ item.description }}</p>
          {% endif %}

          {% if item.suggestion %}
          <span class="label">Suggestion</span>
          <p>{{ item.suggestion }}</p>
          {% endif %}

          {% if item.code_snippet %}
          <div class="code-block">{{ item.code_snippet }}</div>
          {% endif %}
        </div>
      </div>
      {% endfor %}
    </div>
    {% else %}
    <div class="empty-state">
      <div class="empty-icon">&#9989;</div>
      <p>No issues found! The code changes look good.</p>
    </div>
    {% endif %}
  </section>

  <!-- ===== PR DESCRIPTION (if any) ===== -->
  {% if has_description %}
  <section class="section">
    <h2 class="section-title">
      <span class="icon">&#128220;</span> PR Description
    </h2>
    <div class="pr-description">{{{ pr_description }}}</div>
  </section>
  {% endif %}

  <!-- ===== FOOTER ===== -->
  <footer class="report-footer">
    <p>Generated by <strong>AI PR Reviewer v1.0</strong> on {{ generated_at }}</p>
    <p style="margin-top:4px;">
      <a href="{{ pr_url }}" style="color:var(--td-brand-color);text-decoration:none;">View PR on GitHub &rarr;</a>
    </p>
  </footer>

</div>
</body>
</html>)TEMPLATE";
    return t;
}

} // namespace ai_pr_reviewer
