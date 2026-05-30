#pragma once

#include "types.h"
#include <string>
#include <nlohmann/json.hpp>

namespace ai_pr_reviewer {

// ============================================================================
// HtmlReportGenerator - Generates self-contained HTML review reports
//
// Uses inja template engine with TDesign-inspired CSS styling.
// The report contains:
//   - PR overview header (title, author, stats)
//   - Executive summary
//   - Issues by severity with code snippets
//   - Code quality assessment
//   - All CSS is inlined for offline viewing
// ============================================================================
class HtmlReportGenerator {
public:
    explicit HtmlReportGenerator(const AppConfig& config);

    // Load the HTML template (from custom path or built-in default)
    void load_template();

    // Render the report as an HTML string
    std::string render(const PrMetadata& pr_meta,
                       const ReviewResult& result);

    // Generate and write report to file in one step
    // Returns true on success, false on failure.
    bool generate_report(const PrMetadata& pr_meta,
                          const ReviewResult& result,
                          const std::string& output_path);

private:
    AppConfig config_;
    std::string template_content_;
    bool template_loaded_ = false;

    // Convert severity enum to CSS class name
    static std::string severity_class(Severity sev);

    // Convert severity enum to display label
    static std::string severity_label(Severity sev);

    // Convert severity enum to color hex code
    static std::string severity_color(Severity sev);

    // Escape HTML special characters
    static std::string escape_html(const std::string& text);

    // Build inja JSON data object for the template
    nlohmann::json build_template_data(const PrMetadata& pr_meta,
                                        const ReviewResult& result);

    // Get built-in default template
    static std::string builtin_template();
};

} // namespace ai_pr_reviewer
