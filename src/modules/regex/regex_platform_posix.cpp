// Linux and macOS regex engine: POSIX extended regular expressions
// (see regex_platform.h).

#include "regex_platform.h"

#include <cstring>
#include <regex.h>

bool compile_and_match(const char* pattern, const char* subject,
                              MatchResult& result, const RegexOptions& options) {
    regex_t re;
    int compile_flags = REG_EXTENDED;
    if (options.ignore_case) compile_flags |= REG_ICASE;
    if (regcomp(&re, pattern, compile_flags) != 0) return false;

    const size_t max_groups = 16;
    regmatch_t pmatch[max_groups];

    if (regexec(&re, subject, max_groups, pmatch, 0) != 0) {
        result.matched = false;
        regfree(&re);
        return true;
    }

    result.matched = true;
    result.start = (size_t)pmatch[0].rm_so;
    result.end = (size_t)pmatch[0].rm_eo;
    result.full = std::string(subject + pmatch[0].rm_so,
                              (size_t)(pmatch[0].rm_eo - pmatch[0].rm_so));

    for (size_t i = 1; i < max_groups; i++) {
        if (pmatch[i].rm_so == -1) break;
        result.groups.push_back(
            std::string(subject + pmatch[i].rm_so,
                        (size_t)(pmatch[i].rm_eo - pmatch[i].rm_so)));
    }

    regfree(&re);
    return true;
}

bool find_all_matches(const char* pattern, const char* subject,
                             std::vector<MatchResult>& results, const RegexOptions& options) {
    regex_t re;
    int compile_flags = REG_EXTENDED;
    if (options.ignore_case) compile_flags |= REG_ICASE;
    if (regcomp(&re, pattern, compile_flags) != 0) return false;

    const size_t max_groups = 16;
    regmatch_t pmatch[max_groups];
    const char* cursor = subject;
    size_t base_offset = 0;
    int flags = 0;

    while (regexec(&re, cursor, max_groups, pmatch, flags) == 0) {
        MatchResult mr;
        mr.matched = true;
        mr.start = base_offset + (size_t)pmatch[0].rm_so;
        mr.end = base_offset + (size_t)pmatch[0].rm_eo;
        mr.full = std::string(cursor + pmatch[0].rm_so,
                              (size_t)(pmatch[0].rm_eo - pmatch[0].rm_so));

        for (size_t i = 1; i < max_groups; i++) {
            if (pmatch[i].rm_so == -1) break;
            mr.groups.push_back(
                std::string(cursor + pmatch[i].rm_so,
                            (size_t)(pmatch[i].rm_eo - pmatch[i].rm_so)));
        }

        results.push_back(std::move(mr));

        if (pmatch[0].rm_eo == 0) {
            if (cursor[0] == '\0') break;
            base_offset += 1;
            cursor += 1;
        } else {
            base_offset += (size_t)pmatch[0].rm_eo;
            cursor += pmatch[0].rm_eo;
        }
        flags = REG_NOTBOL;
    }

    regfree(&re);
    return true;
}

bool regex_replace_all(const char* pattern, const char* subject,
                              const char* replacement, std::string& out,
                              const RegexOptions& options) {
    regex_t re;
    int compile_flags = REG_EXTENDED;
    if (options.ignore_case) compile_flags |= REG_ICASE;
    if (regcomp(&re, pattern, compile_flags) != 0) return false;

    const size_t max_groups = 16;
    regmatch_t pmatch[max_groups];
    const char* cursor = subject;
    out.clear();
    int flags = 0;

    while (regexec(&re, cursor, max_groups, pmatch, flags) == 0) {
        out.append(cursor, (size_t)pmatch[0].rm_so);

        MatchResult mr;
        mr.matched = true;
        mr.start = 0;
        mr.end = (size_t)pmatch[0].rm_eo;
        mr.full = std::string(cursor + pmatch[0].rm_so,
                              (size_t)(pmatch[0].rm_eo - pmatch[0].rm_so));
        for (size_t i = 1; i < max_groups; i++) {
            if (pmatch[i].rm_so == -1) break;
            mr.groups.push_back(
                std::string(cursor + pmatch[i].rm_so,
                            (size_t)(pmatch[i].rm_eo - pmatch[i].rm_so)));
        }
        append_replacement_template(replacement, mr, out);

        if (pmatch[0].rm_eo == 0) {
            if (cursor[0] == '\0') break;
            out.push_back(*cursor);
            cursor += 1;
        } else {
            cursor += pmatch[0].rm_eo;
        }
        // After first iteration, we are no longer at the beginning of the line
        flags = REG_NOTBOL;
    }
    out.append(cursor);
    regfree(&re);
    return true;
}

bool regex_split_impl(const char* pattern, const char* subject,
                             std::vector<std::string>& parts, const RegexOptions& options) {
    regex_t re;
    int compile_flags = REG_EXTENDED;
    if (options.ignore_case) compile_flags |= REG_ICASE;
    if (regcomp(&re, pattern, compile_flags) != 0) return false;

    regmatch_t pmatch[1];
    const char* cursor = subject;
    int flags = 0;

    while (regexec(&re, cursor, 1, pmatch, flags) == 0) {
        parts.push_back(std::string(cursor, (size_t)pmatch[0].rm_so));

        if (pmatch[0].rm_eo == 0) {
            if (cursor[0] == '\0') break;
            parts.back().push_back(*cursor);
            cursor += 1;
        } else {
            cursor += pmatch[0].rm_eo;
        }
        flags = REG_NOTBOL;
    }
    parts.push_back(std::string(cursor));
    regfree(&re);
    return true;
}

