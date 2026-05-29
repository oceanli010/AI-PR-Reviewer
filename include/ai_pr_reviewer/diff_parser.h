#pragma once

#include "types.h"
#include <string>
#include <vector>

namespace ai_pr_reviewer {

// ============================================================================
// DiffParser - Parses unified diff format into structured FileChange objects
//
// Handles standard git unified diff output including:
//   - File headers (diff --git, ---, +++)
//   - Hunk headers (@@ -old,new +old,new @@)
//   - Context, addition, and deletion lines
//   - Binary file detection
//   - Renamed/deleted files
// ============================================================================
class DiffParser {
public:
    // Parse a complete multi-file diff text
    std::vector<FileChange> parse(const std::string& diff_text);

    // Check if a diff text indicates a binary file change
    static bool is_binary_file(const std::string& diff_text);

    // Extract file extension from filename (e.g., "src/main.cpp" -> "cpp")
    static std::string file_extension(const std::string& filename);

    // Map file extension to programming language name
    static std::string detect_language(const std::string& filename);

private:
    // Parse a single file's diff block
    FileChange parse_file_diff(const std::string& block);

    // Parse hunk header line: "@@ -1,5 +1,7 @@ function() {"
    Hunk parse_hunk_header(const std::string& header_line);

    // Parse individual lines within a hunk
    void parse_hunk_lines(const std::string& lines_text, Hunk& hunk);
};

} // namespace ai_pr_reviewer
