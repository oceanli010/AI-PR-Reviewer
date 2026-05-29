#include <gtest/gtest.h>
#include "ai_pr_reviewer/diff_parser.h"

using namespace ai_pr_reviewer;

// ============================================================================
// Basic Diff Parsing Tests
// ============================================================================

TEST(DiffParserTest, EmptyInput) {
    DiffParser parser;
    auto files = parser.parse("");
    EXPECT_TRUE(files.empty());
}

TEST(DiffParserTest, SingleFileAddition) {
    DiffParser parser;

    std::string diff = R"(diff --git a/test.cpp b/test.cpp
new file mode 100644
index 0000000..e69de29
--- /dev/null
+++ b/test.cpp
@@ -0,0 +1,3 @@
+#include <iostream>
+int main() {
+    return 0;
+}
)";

    auto files = parser.parse(diff);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].filename, "test.cpp");
    EXPECT_EQ(files[0].hunks.size(), 1u);
    EXPECT_EQ(files[0].hunks[0].lines.size(), 3u);
    EXPECT_EQ(files[0].hunks[0].lines[0].type, LineType::Addition);
    EXPECT_EQ(files[0].hunks[0].lines[1].type, LineType::Addition);
    EXPECT_EQ(files[0].hunks[0].lines[2].type, LineType::Addition);
}

TEST(DiffParserTest, SingleFileModification) {
    DiffParser parser;

    std::string diff = R"(diff --git a/src/utils.cpp b/src/utils.cpp
index abc123..def456 100644
--- a/src/utils.cpp
+++ b/src/utils.cpp
@@ -1,4 +1,5 @@
 #include <string>
-void foo() {
+void bar() {
     return "hello";
 }
+int extra = 42;
)";

    auto files = parser.parse(diff);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].filename, "src/utils.cpp");

    ASSERT_EQ(files[0].hunks.size(), 1u);
    auto& hunk = files[0].hunks[0];
    EXPECT_EQ(hunk.old_start, 1);
    EXPECT_EQ(hunk.new_start, 1);

    // Should have: context, deletion, context, addition, context = 5 lines
    // + 1 for the "#include <string>" that appears before the hunk
    EXPECT_EQ(hunk.lines.size(), 5u);
    EXPECT_EQ(hunk.lines[0].type, LineType::Context);   // #include <string>
    EXPECT_EQ(hunk.lines[1].type, LineType::Deletion);  // void foo() {
    EXPECT_EQ(hunk.lines[2].type, LineType::Addition);  // void bar() {
    EXPECT_EQ(hunk.lines[3].type, LineType::Context);   //     return "hello";
    EXPECT_EQ(hunk.lines[4].type, LineType::Addition);  // int extra = 42;
}

TEST(DiffParserTest, MultipleFiles) {
    DiffParser parser;

    std::string diff = R"(diff --git a/foo.cpp b/foo.cpp
--- a/foo.cpp
+++ b/foo.cpp
@@ -1,3 +1,3 @@
-old line
+new line
 context
 context
diff --git a/bar.cpp b/bar.cpp
--- a/bar.cpp
+++ b/bar.cpp
@@ -1,2 +1,3 @@
 line1
 line2
+line3
)";

    auto files = parser.parse(diff);
    ASSERT_EQ(files.size(), 2u);
    EXPECT_EQ(files[0].filename, "foo.cpp");
    EXPECT_EQ(files[1].filename, "bar.cpp");
}

TEST(DiffParserTest, BinaryFileDetection) {
    std::string binary_diff = R"(diff --git a/image.png b/image.png
index 0000000..1234567 100644
Binary files a/image.png and b/image.png differ
)";

    EXPECT_TRUE(DiffParser::is_binary_file(binary_diff));
}

TEST(DiffParserTest, NotBinaryFile) {
    std::string text_diff = R"(diff --git a/file.txt b/file.txt
--- a/file.txt
+++ b/file.txt
@@ -1 +1 @@
-old
+new
)";

    EXPECT_FALSE(DiffParser::is_binary_file(text_diff));
}

TEST(DiffParserTest, FileExtension) {
    EXPECT_EQ(DiffParser::file_extension("src/main.cpp"), "cpp");
    EXPECT_EQ(DiffParser::file_extension("include/header.h"), "h");
    EXPECT_EQ(DiffParser::file_extension("no_extension"), "");
    EXPECT_EQ(DiffParser::file_extension("path/to/file.rs"), "rs");
    EXPECT_EQ(DiffParser::file_extension("config.yaml"), "yaml");
}

TEST(DiffParserTest, LanguageDetection) {
    EXPECT_EQ(DiffParser::detect_language("test.cpp"), "C++");
    EXPECT_EQ(DiffParser::detect_language("test.cc"), "C++");
    EXPECT_EQ(DiffParser::detect_language("test.py"), "Python");
    EXPECT_EQ(DiffParser::detect_language("test.js"), "JavaScript");
    EXPECT_EQ(DiffParser::detect_language("test.ts"), "TypeScript");
    EXPECT_EQ(DiffParser::detect_language("test.go"), "Go");
    EXPECT_EQ(DiffParser::detect_language("test.rs"), "Rust");
    EXPECT_EQ(DiffParser::detect_language("test.html"), "HTML");
    EXPECT_EQ(DiffParser::detect_language("test.css"), "CSS");
    EXPECT_EQ(DiffParser::detect_language("test.json"), "JSON");
    EXPECT_EQ(DiffParser::detect_language("test.md"), "Markdown");
    EXPECT_EQ(DiffParser::detect_language("test.xyz"), "xyz");   // Unknown extension
    EXPECT_EQ(DiffParser::detect_language("noextension"), "Unknown");
}

TEST(DiffParserTest, HunkWithMultipleChanges) {
    DiffParser parser;

    std::string diff = R"(diff --git a/calc.cpp b/calc.cpp
--- a/calc.cpp
+++ b/calc.cpp
@@ -10,6 +10,7 @@ int add(int a, int b) {
     return a + b;
 }
 
+// Multiply two integers
 int multiply(int a, int b) {
-    return a * b;
+    return static_cast<int>(static_cast<long long>(a) * b);
 }
)";

    auto files = parser.parse(diff);
    ASSERT_EQ(files.size(), 1u);
    ASSERT_EQ(files[0].hunks.size(), 1u);

    auto& hunk = files[0].hunks[0];
    EXPECT_EQ(hunk.old_start, 10);
    EXPECT_EQ(hunk.new_start, 10);

    // Check line types
    // Context, Context, Addition, Context, Deletion, Addition
    ASSERT_GE(hunk.lines.size(), 6u);
    EXPECT_EQ(hunk.lines[0].type, LineType::Context);
    EXPECT_EQ(hunk.lines[3].type, LineType::Addition);
    EXPECT_EQ(hunk.lines[4].type, LineType::Context);
    EXPECT_EQ(hunk.lines[5].type, LineType::Deletion);
}

TEST(DiffParserTest, EmptyHunk) {
    DiffParser parser;

    // Diff with only file header but no hunks
    std::string diff = R"(diff --git a/empty.txt b/empty.txt
new file mode 100644
index 0000000..e69de29
--- /dev/null
+++ b/empty.txt
)";

    auto files = parser.parse(diff);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].filename, "empty.txt");
    EXPECT_TRUE(files[0].hunks.empty());
}
