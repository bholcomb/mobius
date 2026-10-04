#ifndef MOBIUS_MODULES_REGEX_PLATFORM_H
#define MOBIUS_MODULES_REGEX_PLATFORM_H

// The regex engine, per platform: POSIX extended regular expressions
// (<regex.h>) on Linux and macOS (regex_platform_posix.cpp), std::regex
// with ECMAScript syntax on Windows (regex_platform_win32.cpp).

#include <cstddef>
#include <string>
#include <vector>

struct RegexOptions {
    bool ignore_case = false;
};

struct MatchResult {
    bool matched = false;
    std::string full;
    std::vector<std::string> groups;
    size_t start = 0;
    size_t end = 0;
};

// Expand \0-\9 in a replacement template (in regex_plugin.cpp).
void append_replacement_template(const char* replacement, const MatchResult& match, std::string& out);

bool compile_and_match(const char* pattern, const char* subject,
                              MatchResult& result, const RegexOptions& options);

bool find_all_matches(const char* pattern, const char* subject,
                             std::vector<MatchResult>& results, const RegexOptions& options);

bool regex_replace_all(const char* pattern, const char* subject,
                              const char* replacement, std::string& out,
                              const RegexOptions& options);

bool regex_split_impl(const char* pattern, const char* subject,
                             std::vector<std::string>& parts, const RegexOptions& options);

#endif // MOBIUS_MODULES_REGEX_PLATFORM_H
