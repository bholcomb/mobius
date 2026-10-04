// Windows regex engine: std::regex with ECMAScript syntax (see
// regex_platform.h).

#include "regex_platform.h"

#include <regex>

bool compile_and_match(const char* pattern, const char* subject,
                              MatchResult& result, const RegexOptions& options) {
    try {
        std::regex_constants::syntax_option_type syntax = std::regex::ECMAScript;
        if (options.ignore_case) syntax |= std::regex::icase;
        std::regex re(pattern, syntax);
        std::cmatch cm;
        if (!std::regex_search(subject, cm, re)) {
            result.matched = false;
            return true;
        }
        result.matched = true;
        result.full = cm[0].str();
        result.start = (size_t)cm.position(0);
        result.end = result.start + cm[0].length();
        for (size_t i = 1; i < cm.size(); i++)
            result.groups.push_back(cm[i].str());
        return true;
    } catch (const std::regex_error&) {
        return false;
    }
}

bool find_all_matches(const char* pattern, const char* subject,
                             std::vector<MatchResult>& results, const RegexOptions& options) {
    try {
        std::regex_constants::syntax_option_type syntax = std::regex::ECMAScript;
        if (options.ignore_case) syntax |= std::regex::icase;
        std::regex re(pattern, syntax);
        std::cregex_iterator it(subject, subject + strlen(subject), re);
        std::cregex_iterator end;
        for (; it != end; ++it) {
            MatchResult mr;
            mr.matched = true;
            mr.full = (*it)[0].str();
            mr.start = (size_t)it->position(0);
            mr.end = mr.start + (*it)[0].length();
            for (size_t i = 1; i < it->size(); i++)
                mr.groups.push_back((*it)[i].str());
            results.push_back(std::move(mr));
        }
        return true;
    } catch (const std::regex_error&) {
        return false;
    }
}

bool regex_replace_all(const char* pattern, const char* subject,
                              const char* replacement, std::string& out,
                              const RegexOptions& options) {
    try {
        std::regex_constants::syntax_option_type syntax = std::regex::ECMAScript;
        if (options.ignore_case) syntax |= std::regex::icase;
        std::regex re(pattern, syntax);
        out.clear();

        const char* cursor = subject;
        std::cregex_iterator it(subject, subject + strlen(subject), re);
        std::cregex_iterator end;
        for (; it != end; ++it) {
            size_t match_start = (size_t)it->position(0);
            size_t match_len = (*it)[0].length();
            out.append(cursor, match_start - (size_t)(cursor - subject));

            MatchResult mr;
            mr.matched = true;
            mr.full = (*it)[0].str();
            mr.start = match_start;
            mr.end = match_start + match_len;
            for (size_t i = 1; i < it->size(); i++) {
                mr.groups.push_back((*it)[i].str());
            }
            append_replacement_template(replacement, mr, out);

            cursor = subject + match_start + match_len;
            if (match_len == 0 && *cursor != '\0') {
                out.push_back(*cursor);
                ++cursor;
            }
        }
        out.append(cursor);
        return true;
    } catch (const std::regex_error&) {
        return false;
    }
}

bool regex_split_impl(const char* pattern, const char* subject,
                             std::vector<std::string>& parts, const RegexOptions& options) {
    try {
        std::regex_constants::syntax_option_type syntax = std::regex::ECMAScript;
        if (options.ignore_case) syntax |= std::regex::icase;
        std::regex re(pattern, syntax);
        std::cregex_token_iterator it(subject, subject + strlen(subject), re, -1);
        std::cregex_token_iterator end;
        for (; it != end; ++it)
            parts.push_back(it->str());
        return true;
    } catch (const std::regex_error&) {
        return false;
    }
}

