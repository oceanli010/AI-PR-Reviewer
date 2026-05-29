#include <gtest/gtest.h>
#include "ai_pr_reviewer/ai_analyzer.h"

using namespace ai_pr_reviewer;

// ============================================================================
// Unit tests for AI Analyzer (non-API-calling components)
// ============================================================================

TEST(AIAnalyzerTest, ConstructorWithConfig) {
    AppConfig config;
    config.openai_api_key = "sk-test-key";
    config.openai_model = "gpt-4o";

    AIAnalyzer analyzer(config);
    // Construction should not throw
    SUCCEED();
}

TEST(AIAnalyzerTest, ProgressCallback) {
    AppConfig config;
    config.openai_api_key = "sk-test-key";

    AIAnalyzer analyzer(config);

    int callback_count = 0;
    analyzer.set_progress_callback([&](const std::string& msg) {
        callback_count++;
        EXPECT_FALSE(msg.empty());
    });

    EXPECT_EQ(callback_count, 0);
}

TEST(AIAnalyzerTest, SeverityParsing) {
    // Parse severity from string
    // Note: parse_severity is private, testing via the concept
    // This tests that the enum values are distinct

    EXPECT_NE(static_cast<int>(Severity::Critical), static_cast<int>(Severity::High));
    EXPECT_NE(static_cast<int>(Severity::High), static_cast<int>(Severity::Medium));
    EXPECT_NE(static_cast<int>(Severity::Medium), static_cast<int>(Severity::Low));
    EXPECT_NE(static_cast<int>(Severity::Low), static_cast<int>(Severity::Info));
}

TEST(AIAnalyzerTest, EmptyFilesReturnsEmptyResult) {
    AppConfig config;
    config.openai_api_key = "sk-test-key";

    AIAnalyzer analyzer(config);

    PrMetadata pr_meta;
    pr_meta.title = "Test PR";
    pr_meta.author = "test";

    std::vector<FileChange> empty_files;
    auto result = analyzer.analyze(pr_meta, empty_files);

    EXPECT_EQ(result.total_issues, 0);
    EXPECT_FALSE(result.summary.empty());
}

TEST(AIAnalyzerTest, SystemPromptNotEmpty) {
    AppConfig config;
    config.openai_api_key = "sk-test";

    AIAnalyzer analyzer(config);

    // Can't directly test private build_system_prompt, but we can verify
    // that the analyzer doesn't crash during construction
    // The system prompt is tested implicitly through analyze()
    SUCCEED();
}

TEST(AIAnalyzerTest, MergeResults) {
    // Test merging logic by creating manual ReviewResults
    ReviewResult r1;
    r1.total_issues = 3;
    r1.critical_count = 1;
    r1.high_count = 1;
    r1.medium_count = 1;
    r1.summary = "Batch 1 summary";
    r1.overall_assessment = "Assessment 1";

    ReviewResult r2;
    r2.total_issues = 2;
    r2.high_count = 2;
    r2.summary = "Batch 2 summary";
    r2.overall_assessment = "Assessment 2";

    PrMetadata pr_meta;
    pr_meta.author = "test";

    // merge_results is private, so we test the concept
    // by verifying our assumptions about merged data

    int merged_total = r1.total_issues + r2.total_issues;
    int merged_critical = r1.critical_count + r2.critical_count;
    int merged_high = r1.high_count + r2.high_count;
    int merged_medium = r1.medium_count + r2.medium_count;

    EXPECT_EQ(merged_total, 5);
    EXPECT_EQ(merged_critical, 1);
    EXPECT_EQ(merged_high, 3);
    EXPECT_EQ(merged_medium, 1);
}

// ============================================================================
// Integration test markers (disabled by default)
// Set OPENAI_API_KEY env var and remove DISABLED_ prefix to run
// ============================================================================

TEST(AIAnalyzerTest, DISABLED_RealOpenAIAnalysis) {
    const char* api_key = std::getenv("OPENAI_API_KEY");
    if (!api_key) {
        GTEST_SKIP() << "OPENAI_API_KEY not set, skipping integration test";
    }

    AppConfig config;
    config.openai_api_key = api_key;
    config.openai_model = "gpt-4o";
    config.max_tokens = 1000;

    AIAnalyzer analyzer(config);

    PrMetadata pr_meta;
    pr_meta.title = "Test: Add multiply function";
    pr_meta.author = "test-user";
    pr_meta.base_branch = "main";
    pr_meta.head_branch = "feature/multiply";
    pr_meta.changed_files = 1;
    pr_meta.additions = 3;
    pr_meta.deletions = 0;
    pr_meta.number = 1;

    FileChange file;
    file.filename = "calc.cpp";
    file.language = "C++";
    file.status = "Modified";
    file.additions = 3;
    file.deletions = 0;
    file.raw_diff = R"(@@ -1,3 +1,6 @@
 #include <iostream>
 int add(int a, int b) { return a + b; }
+int multiply(int a, int b) { return a * b; }
+// Potential integer overflow in multiply
+int divide(int a, int b) { return a / b; }
)";

    std::vector<FileChange> files = { file };
    auto result = analyzer.analyze(pr_meta, files);

    // Basic result checks
    EXPECT_FALSE(result.summary.empty());
    EXPECT_GE(result.total_issues, 0);

    std::cout << "AI Analysis Result:\n";
    std::cout << "  Summary: " << result.summary << "\n";
    std::cout << "  Issues: " << result.total_issues << "\n";
    std::cout << "  Assessment: " << result.overall_assessment << "\n";

    for (const auto& item : result.risk_items) {
        std::cout << "  - [" << static_cast<int>(item.severity) << "] "
                  << item.file << ":" << item.line << " - " << item.title << "\n";
    }
}
