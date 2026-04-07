#include <mobius/mobius_plugin.h>

#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
  #include <regex>
#else
  #include <regex.h>
#endif

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

#ifdef _WIN32

// Windows: std::regex with ECMAScript (ERE-compatible) syntax

struct MatchResult {
    bool matched;
    std::string full;
    std::vector<std::string> groups;
    size_t start;
    size_t end;
};

static bool compile_and_match(const char* pattern, const char* subject,
                              MatchResult& result, bool global = false) {
    try {
        std::regex re(pattern, std::regex::ECMAScript);
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

static bool find_all_matches(const char* pattern, const char* subject,
                             std::vector<MatchResult>& results) {
    try {
        std::regex re(pattern, std::regex::ECMAScript);
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

static bool regex_replace_all(const char* pattern, const char* subject,
                              const char* replacement, std::string& out) {
    try {
        std::regex re(pattern, std::regex::ECMAScript);
        out = std::regex_replace(subject, re, replacement);
        return true;
    } catch (const std::regex_error&) {
        return false;
    }
}

static bool regex_split_impl(const char* pattern, const char* subject,
                             std::vector<std::string>& parts) {
    try {
        std::regex re(pattern, std::regex::ECMAScript);
        std::cregex_token_iterator it(subject, subject + strlen(subject), re, -1);
        std::cregex_token_iterator end;
        for (; it != end; ++it)
            parts.push_back(it->str());
        return true;
    } catch (const std::regex_error&) {
        return false;
    }
}

#else

// POSIX: <regex.h> with extended regex (ERE)

struct MatchResult {
    bool matched;
    std::string full;
    std::vector<std::string> groups;
    size_t start;
    size_t end;
};

static bool compile_and_match(const char* pattern, const char* subject,
                              MatchResult& result, bool /*global*/ = false) {
    regex_t re;
    if (regcomp(&re, pattern, REG_EXTENDED) != 0) return false;

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

static bool find_all_matches(const char* pattern, const char* subject,
                             std::vector<MatchResult>& results) {
    regex_t re;
    if (regcomp(&re, pattern, REG_EXTENDED) != 0) return false;

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

static bool regex_replace_all(const char* pattern, const char* subject,
                              const char* replacement, std::string& out) {
    regex_t re;
    if (regcomp(&re, pattern, REG_EXTENDED) != 0) return false;

    const size_t max_groups = 16;
    regmatch_t pmatch[max_groups];
    const char* cursor = subject;
    out.clear();
    int flags = 0;

    while (regexec(&re, cursor, max_groups, pmatch, flags) == 0) {
        out.append(cursor, (size_t)pmatch[0].rm_so);

        for (const char* r = replacement; *r; r++) {
            if (*r == '\\' && r[1] >= '0' && r[1] <= '9') {
                int idx = r[1] - '0';
                r++;
                if (idx < (int)max_groups && pmatch[idx].rm_so != -1)
                    out.append(cursor + pmatch[idx].rm_so,
                               (size_t)(pmatch[idx].rm_eo - pmatch[idx].rm_so));
            } else {
                out.push_back(*r);
            }
        }

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

static bool regex_split_impl(const char* pattern, const char* subject,
                             std::vector<std::string>& parts) {
    regex_t re;
    if (regcomp(&re, pattern, REG_EXTENDED) != 0) return false;

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

#endif

// ============================================================================
// Helper: push a match result as a Mobius table
// ============================================================================

static void push_match_table(MobiusState* state, const MatchResult& mr) {
    mobius_stack_pushNewTable(state, 4);
    int tbl = mobius_stack_size(state) - 1;

    mobius_stack_pushString(state, mr.full.c_str());
    mobius_stack_setTableField(state, tbl, "match");

    mobius_stack_pushInt64(state, (int64_t)mr.start);
    mobius_stack_setTableField(state, tbl, "start");

    mobius_stack_pushInt64(state, (int64_t)mr.end);
    mobius_stack_setTableField(state, tbl, "end");

    if (!mr.groups.empty()) {
        mobius_stack_pushNewArray(state, mr.groups.size());
        int arr = mobius_stack_size(state) - 1;
        for (size_t i = 0; i < mr.groups.size(); i++) {
            mobius_stack_pushString(state, mr.groups[i].c_str());
            mobius_stack_arrayPush(state, arr);
        }
        mobius_stack_setTableField(state, tbl, "groups");
    }
}

// ============================================================================
// regex.match(pattern, string) -> table | nil
// ============================================================================

static int regex_match(MobiusState* state, int arg_count) {
    if (arg_count != 2)
        return mobius_error(state, "regex.match() expects 2 arguments (pattern, string)");
    if (!mobius_stack_isString(state, -2))
        return mobius_error(state, "regex.match() first argument must be a string");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "regex.match() second argument must be a string");

    const char* pattern = mobius_stack_asString(state, -2);
    const char* subject = mobius_stack_asString(state, -1);

    MatchResult mr;
    if (!compile_and_match(pattern, subject, mr))
        return mobius_error(state, "regex.match() invalid regex pattern");

    mobius_stack_pop(state, arg_count);

    if (!mr.matched) {
        mobius_stack_pushNil(state);
    } else {
        // match() requires the pattern to match the entire string
        if (mr.start != 0 || mr.end != strlen(subject)) {
            mobius_stack_pushNil(state);
        } else {
            push_match_table(state, mr);
        }
    }
    return 1;
}

// ============================================================================
// regex.search(pattern, string) -> table | nil
// ============================================================================

static int regex_search(MobiusState* state, int arg_count) {
    if (arg_count != 2)
        return mobius_error(state, "regex.search() expects 2 arguments (pattern, string)");
    if (!mobius_stack_isString(state, -2))
        return mobius_error(state, "regex.search() first argument must be a string");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "regex.search() second argument must be a string");

    const char* pattern = mobius_stack_asString(state, -2);
    const char* subject = mobius_stack_asString(state, -1);

    MatchResult mr;
    if (!compile_and_match(pattern, subject, mr))
        return mobius_error(state, "regex.search() invalid regex pattern");

    mobius_stack_pop(state, arg_count);

    if (!mr.matched) {
        mobius_stack_pushNil(state);
    } else {
        push_match_table(state, mr);
    }
    return 1;
}

// ============================================================================
// regex.findall(pattern, string) -> array of tables
// ============================================================================

static int regex_findall(MobiusState* state, int arg_count) {
    if (arg_count != 2)
        return mobius_error(state, "regex.findall() expects 2 arguments (pattern, string)");
    if (!mobius_stack_isString(state, -2))
        return mobius_error(state, "regex.findall() first argument must be a string");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "regex.findall() second argument must be a string");

    const char* pattern = mobius_stack_asString(state, -2);
    const char* subject = mobius_stack_asString(state, -1);

    std::vector<MatchResult> results;
    if (!find_all_matches(pattern, subject, results))
        return mobius_error(state, "regex.findall() invalid regex pattern");

    mobius_stack_pop(state, arg_count);

    mobius_stack_pushNewArray(state, results.size());
    int arr_idx = mobius_stack_size(state) - 1;

    for (size_t i = 0; i < results.size(); i++) {
        push_match_table(state, results[i]);
        mobius_stack_arrayPush(state, arr_idx);
    }
    return 1;
}

// ============================================================================
// regex.replace(pattern, string, replacement) -> string
// ============================================================================

static int regex_replace(MobiusState* state, int arg_count) {
    if (arg_count != 3)
        return mobius_error(state, "regex.replace() expects 3 arguments (pattern, string, replacement)");
    if (!mobius_stack_isString(state, -3))
        return mobius_error(state, "regex.replace() first argument must be a string");
    if (!mobius_stack_isString(state, -2))
        return mobius_error(state, "regex.replace() second argument must be a string");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "regex.replace() third argument must be a string");

    const char* pattern     = mobius_stack_asString(state, -3);
    const char* subject     = mobius_stack_asString(state, -2);
    const char* replacement = mobius_stack_asString(state, -1);

    std::string out;
    if (!regex_replace_all(pattern, subject, replacement, out))
        return mobius_error(state, "regex.replace() invalid regex pattern");

    mobius_stack_pop(state, arg_count);
    mobius_stack_pushString(state, out.c_str());
    return 1;
}

// ============================================================================
// regex.split(pattern, string) -> array of strings
// ============================================================================

static int regex_split(MobiusState* state, int arg_count) {
    if (arg_count != 2)
        return mobius_error(state, "regex.split() expects 2 arguments (pattern, string)");
    if (!mobius_stack_isString(state, -2))
        return mobius_error(state, "regex.split() first argument must be a string");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "regex.split() second argument must be a string");

    const char* pattern = mobius_stack_asString(state, -2);
    const char* subject = mobius_stack_asString(state, -1);

    std::vector<std::string> parts;
    if (!regex_split_impl(pattern, subject, parts))
        return mobius_error(state, "regex.split() invalid regex pattern");

    mobius_stack_pop(state, arg_count);

    mobius_stack_pushNewArray(state, parts.size());
    int arr_idx = mobius_stack_size(state) - 1;

    for (size_t i = 0; i < parts.size(); i++) {
        mobius_stack_pushString(state, parts[i].c_str());
        mobius_stack_arrayPush(state, arr_idx);
    }
    return 1;
}

// ============================================================================
// Plugin boilerplate
// ============================================================================

static int init_regex_plugin(MobiusState* /*state*/) {
    return 0;
}

static void cleanup_regex_plugin(void) {
}

static MobiusPluginFunction regex_functions[] = {
    {"match",   regex_match,   2, MOBIUS_VAL_TABLE,   "Full match: returns table if pattern matches entire string, else nil"},
    {"search",  regex_search,  2, MOBIUS_VAL_TABLE,   "Search: returns table for first match found anywhere, else nil"},
    {"findall", regex_findall, 2, MOBIUS_VAL_ARRAY,   "Find all matches, return array of match tables"},
    {"replace", regex_replace, 3, MOBIUS_VAL_STRING,  "Replace all occurrences of pattern in string"},
    {"split",   regex_split,   2, MOBIUS_VAL_ARRAY,   "Split string by regex pattern, return array of strings"},
};

static MobiusPlugin regex_plugin = {
    .metadata = {
        .name = "regex",
        .version = "1.0.0",
        .description = "Regular Expression Support",
        .author = "Mobius Team",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license = "MIT"
    },
    .functions = regex_functions,
    .function_count = sizeof(regex_functions) / sizeof(regex_functions[0]),
    .init_plugin = init_regex_plugin,
    .cleanup_plugin = cleanup_regex_plugin,
    .post_init = nullptr,
};

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &regex_plugin;
}
