#include "ai_pr_reviewer/diff_parser.h"

#include <regex>
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <spdlog/spdlog.h>

namespace ai_pr_reviewer {

// ============================================================================
// Public API
// ============================================================================
std::vector<FileChange> DiffParser::parse(const std::string& diff_text) {
    std::vector<FileChange> files;

    if (diff_text.empty()) {
        return files;
    }

    // Split the diff into per-file blocks
    // Each file block starts with "diff --git a/... b/..."
    std::regex file_header_pattern(R"(^diff --git )");

    std::vector<std::string> blocks;
    std::istringstream stream(diff_text);
    std::string line;
    std::string current_block;

    while (std::getline(stream, line)) {
        if (std::regex_search(line, file_header_pattern) && !current_block.empty()) {
            blocks.push_back(current_block);
            current_block.clear();
        }
        current_block += line + "\n";
    }

    if (!current_block.empty()) {
        blocks.push_back(current_block);
    }

    for (const auto& block : blocks) {
        try {
            FileChange parsed = parse_file_diff(block);
            if (!parsed.filename.empty()) {
                files.push_back(std::move(parsed));
            }
        } catch (const std::exception& e) {
            spdlog::warn("Failed to parse diff block: {}", e.what());
        }
    }

    return files;
}

// ============================================================================
// Binary Check
// ============================================================================
bool DiffParser::is_binary_file(const std::string& diff_text) {
    return diff_text.find("Binary files") != std::string::npos ||
           diff_text.find("GIT binary patch") != std::string::npos;
}

// ============================================================================
// File Extension
// ============================================================================
std::string DiffParser::file_extension(const std::string& filename) {
    size_t dot_pos = filename.rfind('.');
    if (dot_pos == std::string::npos || dot_pos == 0) {
        return "";
    }
    return filename.substr(dot_pos + 1);
}

// ============================================================================
// Language Detection
// ============================================================================
std::string DiffParser::detect_language(const std::string& filename) {
    std::string ext = file_extension(filename);

    // Common programming language extensions
    static const std::unordered_map<std::string, std::string> lang_map = {
        { "cpp", "C++" }, { "cc", "C++" }, { "cxx", "C++" }, { "hpp", "C++" }, { "h", "C" },
        { "c", "C" },
        { "java", "Java" },
        { "cs", "C#" },
        { "py", "Python" }, { "pyw", "Python" },
        { "js", "JavaScript" }, { "jsx", "JavaScript" }, { "mjs", "JavaScript" },
        { "ts", "TypeScript" }, { "tsx", "TypeScript" },
        { "go", "Go" },
        { "rs", "Rust" },
        { "rb", "Ruby" },
        { "php", "PHP" },
        { "swift", "Swift" },
        { "kt", "Kotlin" }, { "kts", "Kotlin" },
        { "scala", "Scala" },
        { "lua", "Lua" },
        { "r", "R" },
        { "sh", "Shell" }, { "bash", "Shell" }, { "zsh", "Shell" },
        { "bat", "Batch" }, { "cmd", "Batch" },
        { "ps1", "PowerShell" },
        { "sql", "SQL" },
        { "html", "HTML" }, { "htm", "HTML" },
        { "css", "CSS" }, { "scss", "SCSS" }, { "less", "Less" },
        { "json", "JSON" }, { "xml", "XML" }, { "yaml", "YAML" }, { "yml", "YAML" },
        { "toml", "TOML" }, { "ini", "INI" }, { "cfg", "Config" },
        { "md", "Markdown" }, { "markdown", "Markdown" },
        { "cmake", "CMake" }, { "txt", "Text" },
        { "vue", "Vue" }, { "svelte", "Svelte" },
        { "dart", "Dart" },
    };

    auto it = lang_map.find(ext);
    if (it != lang_map.end()) {
        return it->second;
    }

    return ext.empty() ? "Unknown" : ext;
}

// ============================================================================
// Single File Diff Parsing
// ============================================================================
FileChange DiffParser::parse_file_diff(const std::string& block) {
    FileChange file;
    std::istringstream stream(block);
    std::string line;

    // Parse headers
    bool in_hunk = false;
    std::string hunk_text;
    std::string hunk_header_line;

    while (std::getline(stream, line)) {
        // diff --git a/... b/...
        if (line.find("diff --git ") == 0) {
            std::regex filename_pattern(R"(diff --git a/(.*) b/(.*))");
            std::smatch m;
            if (std::regex_search(line, m, filename_pattern)) {
                file.filename = m[2].str();
            }
        }
        // --- a/file and +++ b/file
        else if (line.find("--- ") == 0 || line.find("+++ ") == 0) {
            // Skip - these contain filenames we already parsed
        }
        // Binary files
        else if (line.find("Binary files") != std::string::npos) {
            file.is_binary = true;
            return file;
        }
        // index line
        else if (line.find("index ") == 0) {
            // index line - skip
        }
        // old/new mode
        else if (line.find("old mode ") == 0 || line.find("new mode ") == 0 ||
                 line.find("deleted file mode ") == 0 || line.find("new file mode ") == 0) {
            // Mode lines - skip
        }
        // rename from/to
        else if (line.find("rename from ") == 0) {
            file.previous_filename = line.substr(12);
        }
        else if (line.find("rename to ") == 0) {
            file.status = "Renamed";
        }
        else if (line.find("similarity index ") == 0) {
            // Skip similarity index
        }
        // Hunk header
        else if (line.find("@@") == 0) {
            // Parse previous hunk if any
            if (in_hunk && !hunk_text.empty()) {
                Hunk h = parse_hunk_header(hunk_header_line);
                parse_hunk_lines(hunk_text, h);
                file.hunks.push_back(std::move(h));
                hunk_text.clear();
            }

            in_hunk = true;
            hunk_header_line = line;
        }
        // Lines within a hunk
        else if (in_hunk) {
            hunk_text += line + "\n";
        }
    }

    // Parse the last hunk
    if (in_hunk && !hunk_text.empty()) {
        Hunk h = parse_hunk_header(hunk_header_line);
        parse_hunk_lines(hunk_text, h);
        file.hunks.push_back(std::move(h));
    }

    // Detect language
    file.language = detect_language(file.filename);

    return file;
}

// ============================================================================
// Hunk Header Parsing
// ============================================================================
Hunk DiffParser::parse_hunk_header(const std::string& header_line) {
    Hunk hunk;

    // Pattern: @@ -old_start,old_count +new_start,new_count @@
    // or:      @@ -old_start +new_start @@ (when count == 1)
    std::regex pattern(R"(@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@(.*)?)");
    std::smatch m;

    if (std::regex_search(header_line, m, pattern)) {
        hunk.old_start = std::stoi(m[1].str());
        hunk.old_count = m[2].matched ? std::stoi(m[2].str()) : 1;
        hunk.new_start = std::stoi(m[3].str());
        hunk.new_count = m[4].matched ? std::stoi(m[4].str()) : 1;
        hunk.header = header_line;
    }

    return hunk;
}

// ============================================================================
// Hunk Lines Parsing
// ============================================================================
void DiffParser::parse_hunk_lines(const std::string& lines_text, Hunk& hunk) {
    std::istringstream stream(lines_text);
    std::string line;
    int old_lineno = hunk.old_start;
    int new_lineno = hunk.new_start;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;

        DiffLine dline;
        dline.old_lineno = old_lineno;
        dline.new_lineno = new_lineno;

        if (line[0] == '+') {
            dline.type = LineType::Addition;
            dline.content = line.substr(1);
            new_lineno++;
        } else if (line[0] == '-') {
            dline.type = LineType::Deletion;
            dline.content = line.substr(1);
            old_lineno++;
        } else if (line[0] == ' ') {
            dline.type = LineType::Context;
            dline.content = line.substr(1);
            old_lineno++;
            new_lineno++;
        } else if (line.find("\\ No newline") == 0) {
            // No newline at end of file marker - skip
            continue;
        }

        hunk.lines.push_back(std::move(dline));
    }
}

} // namespace ai_pr_reviewer
