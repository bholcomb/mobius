#include <mobius/mobius_plugin.h>

#include <cstring>
#include <string>
#include <vector>

#include "regex_engine.h"

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

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


static bool apply_flag_string(const char* flags, RegexOptions& options, std::string& error) {
    for (const char* p = flags; *p; ++p) {
        char c = *p;
        if (c == ' ' || c == '\t' || c == ',') continue;
        if (c == 'i') {
            options.ignore_case = true;
            continue;
        }
        error = "unsupported regex flag: ";
        error.push_back(c);
        return false;
    }
    return true;
}

static bool read_regex_options(MobiusState* state, int idx, RegexOptions& options, char* error, size_t error_size) {
    if (mobius_stack_isString(state, idx)) {
        std::string parse_error;
        if (!apply_flag_string(mobius_stack_asString(state, idx), options, parse_error)) {
            snprintf(error, error_size, "regex options error: %s", parse_error.c_str());
            return false;
        }
        return true;
    }

    if (!mobius_stack_isTable(state, idx)) {
        snprintf(error, error_size, "regex options must be a flags string or options table");
        return false;
    }

    mobius_stack_getTableField(state, idx, "flags");
    if (!mobius_stack_isNil(state, -1)) {
        if (!mobius_stack_isString(state, -1)) {
            snprintf(error, error_size, "regex options.flags must be a string");
            mobius_stack_pop(state, 1);
            return false;
        }
        std::string parse_error;
        if (!apply_flag_string(mobius_stack_asString(state, -1), options, parse_error)) {
            snprintf(error, error_size, "regex options error: %s", parse_error.c_str());
            mobius_stack_pop(state, 1);
            return false;
        }
    }
    mobius_stack_pop(state, 1);

    mobius_stack_getTableField(state, idx, "ignore_case");
    if (!mobius_stack_isNil(state, -1)) {
        if (!mobius_stack_isBool(state, -1)) {
            snprintf(error, error_size, "regex options.ignore_case must be a bool");
            mobius_stack_pop(state, 1);
            return false;
        }
        if (mobius_stack_asBool(state, -1)) {
            options.ignore_case = true;
        }
    }
    mobius_stack_pop(state, 1);

    return true;
}

static void append_replacement_template(const char* replacement, const MatchResult& match, std::string& out) {
    for (const char* r = replacement; *r; ++r) {
        if (*r == '\\' && r[1] != '\0') {
            char next = r[1];
            if (next >= '0' && next <= '9') {
                int idx = next - '0';
                if (idx == 0) {
                    out.append(match.full);
                } else if ((size_t)(idx - 1) < match.groups.size()) {
                    out.append(match.groups[(size_t)idx - 1]);
                }
                ++r;
                continue;
            }
            if (next == '\\') {
                out.push_back('\\');
                ++r;
                continue;
            }
            out.push_back(next);
            ++r;
            continue;
        }
        out.push_back(*r);
    }
}


// ============================================================================
// Helper: push a match result as a Mobius table
// ============================================================================

static void push_match_table(MobiusState* state, const MatchResult& mr) {
    mobius_stack_pushNewTable(state, 6);
    int tbl = mobius_stack_size(state) - 1;

    mobius_stack_pushString(state, mr.full.c_str());
    mobius_stack_setTableField(state, tbl, "match");

    mobius_stack_pushString(state, mr.full.c_str());
    mobius_stack_setTableField(state, tbl, "full");

    mobius_stack_pushInt64(state, (int64_t)mr.start);
    mobius_stack_setTableField(state, tbl, "start");

    mobius_stack_pushInt64(state, (int64_t)mr.end);
    mobius_stack_setTableField(state, tbl, "end");

    mobius_stack_pushInt64(state, (int64_t)mr.groups.size());
    mobius_stack_setTableField(state, tbl, "group_count");

    mobius_stack_pushNewArray(state, mr.groups.size());
    int arr = mobius_stack_size(state) - 1;
    for (size_t i = 0; i < mr.groups.size(); i++) {
        mobius_stack_pushString(state, mr.groups[i].c_str());
        mobius_stack_arrayPush(state, arr);
    }
    mobius_stack_setTableField(state, tbl, "groups");
}

// ============================================================================
// MATCHING (regex_engine.h)
// ============================================================================

static bool compile_regex(const char* pattern, const RegexOptions& options,
                          mobius_regex::Regex* re, std::string* error) {
    return re->compile(pattern, options.ignore_case, error);
}

// The match in `caps` as a MatchResult. Groups are listed up to the first
// one that took no part in the match.
static MatchResult to_match(const char* subject, const std::vector<ptrdiff_t>& caps, size_t groups) {
    MatchResult mr;
    mr.matched = true;
    mr.start = (size_t)caps[0];
    mr.end = (size_t)caps[1];
    mr.full.assign(subject + caps[0], (size_t)(caps[1] - caps[0]));
    for (size_t g = 1; g <= groups; g++) {
        if (caps[2 * g] < 0) break;
        mr.groups.emplace_back(subject + caps[2 * g], (size_t)(caps[2 * g + 1] - caps[2 * g]));
    }
    return mr;
}

static bool compile_and_match(const char* pattern, const char* subject,
                              MatchResult& result, const RegexOptions& options, std::string* error) {
    mobius_regex::Regex re;
    if (!compile_regex(pattern, options, &re, error)) return false;
    std::vector<ptrdiff_t> caps;
    if (re.search(subject, strlen(subject), 0, &caps)) result = to_match(subject, caps, re.group_count());
    else result.matched = false;
    return true;
}

static bool find_all_matches(const char* pattern, const char* subject,
                             std::vector<MatchResult>& results, const RegexOptions& options,
                             std::string* error) {
    mobius_regex::Regex re;
    if (!compile_regex(pattern, options, &re, error)) return false;
    size_t len = strlen(subject);
    std::vector<ptrdiff_t> caps;
    size_t pos = 0;
    while (pos <= len && re.search(subject, len, pos, &caps)) {
        results.push_back(to_match(subject, caps, re.group_count()));
        // After an empty match, look again one byte further on.
        pos = caps[1] > caps[0] ? (size_t)caps[1] : (size_t)caps[1] + 1;
    }
    return true;
}

static bool regex_replace_all(const char* pattern, const char* subject,
                              const char* replacement, std::string& out,
                              const RegexOptions& options, std::string* error) {
    mobius_regex::Regex re;
    if (!compile_regex(pattern, options, &re, error)) return false;
    size_t len = strlen(subject);
    std::vector<ptrdiff_t> caps;
    out.clear();
    size_t pos = 0;
    while (pos <= len && re.search(subject, len, pos, &caps)) {
        size_t ms = (size_t)caps[0], me = (size_t)caps[1];
        out.append(subject + pos, ms - pos);
        append_replacement_template(replacement, to_match(subject, caps, re.group_count()), out);
        if (me > ms) {
            pos = me;
        } else {
            // An empty match: keep the byte after it, and look again past it.
            if (ms < len) out.push_back(subject[ms]);
            pos = ms + 1;
        }
    }
    if (pos < len) out.append(subject + pos);
    return true;
}

static bool regex_split_impl(const char* pattern, const char* subject,
                             std::vector<std::string>& parts, const RegexOptions& options,
                             std::string* error) {
    mobius_regex::Regex re;
    if (!compile_regex(pattern, options, &re, error)) return false;
    size_t len = strlen(subject);
    std::vector<ptrdiff_t> caps;
    size_t pos = 0;
    while (re.search(subject, len, pos, &caps)) {
        size_t ms = (size_t)caps[0], me = (size_t)caps[1];
        parts.push_back(std::string(subject + pos, ms - pos));
        if (me == pos) {
            // An empty match where the part began: it doesn't split; the
            // byte joins the part.
            if (pos >= len) break;
            parts.back().push_back(subject[pos]);
            pos += 1;
        } else {
            pos = me;
        }
    }
    parts.push_back(std::string(subject + (pos < len ? pos : len)));
    return true;
}

// ============================================================================
// regex.match(pattern, string) -> table | nil
// ============================================================================

static int regex_match(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count < 2 || arg_count > 3)
        return mobius_error(state, "regex.match() expects 2 or 3 arguments (pattern, string [, flags|options])");
    if (!mobius_stack_isString(state, -arg_count))
        return mobius_error(state, "regex.match() first argument must be a string");
    if (!mobius_stack_isString(state, 1 - arg_count))
        return mobius_error(state, "regex.match() second argument must be a string");

    RegexOptions options;
    if (arg_count == 3) {
        char error[256];
        if (!read_regex_options(state, mobius_stack_size(state) - 1, options, error, sizeof(error))) {
            return mobius_error(state, error);
        }
    }

    const char* pattern = mobius_stack_asString(state, -arg_count);
    const char* subject = mobius_stack_asString(state, 1 - arg_count);

    MatchResult mr;
    std::string error;
    if (!compile_and_match(pattern, subject, mr, options, &error))
        return mobius_error(state, ("regex.match() invalid regex pattern: " + error).c_str());

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

static int regex_search(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count < 2 || arg_count > 3)
        return mobius_error(state, "regex.search() expects 2 or 3 arguments (pattern, string [, flags|options])");
    if (!mobius_stack_isString(state, -arg_count))
        return mobius_error(state, "regex.search() first argument must be a string");
    if (!mobius_stack_isString(state, 1 - arg_count))
        return mobius_error(state, "regex.search() second argument must be a string");

    RegexOptions options;
    if (arg_count == 3) {
        char error[256];
        if (!read_regex_options(state, mobius_stack_size(state) - 1, options, error, sizeof(error))) {
            return mobius_error(state, error);
        }
    }

    const char* pattern = mobius_stack_asString(state, -arg_count);
    const char* subject = mobius_stack_asString(state, 1 - arg_count);

    MatchResult mr;
    std::string error;
    if (!compile_and_match(pattern, subject, mr, options, &error))
        return mobius_error(state, ("regex.search() invalid regex pattern: " + error).c_str());

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

static int regex_findall(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count < 2 || arg_count > 3)
        return mobius_error(state, "regex.findall() expects 2 or 3 arguments (pattern, string [, flags|options])");
    if (!mobius_stack_isString(state, -arg_count))
        return mobius_error(state, "regex.findall() first argument must be a string");
    if (!mobius_stack_isString(state, 1 - arg_count))
        return mobius_error(state, "regex.findall() second argument must be a string");

    RegexOptions options;
    if (arg_count == 3) {
        char error[256];
        if (!read_regex_options(state, mobius_stack_size(state) - 1, options, error, sizeof(error))) {
            return mobius_error(state, error);
        }
    }

    const char* pattern = mobius_stack_asString(state, -arg_count);
    const char* subject = mobius_stack_asString(state, 1 - arg_count);

    std::vector<MatchResult> results;
    std::string error;
    if (!find_all_matches(pattern, subject, results, options, &error))
        return mobius_error(state, ("regex.findall() invalid regex pattern: " + error).c_str());

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

static int regex_replace(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count < 3 || arg_count > 4)
        return mobius_error(state, "regex.replace() expects 3 or 4 arguments (pattern, string, replacement [, flags|options])");
    if (!mobius_stack_isString(state, -arg_count))
        return mobius_error(state, "regex.replace() first argument must be a string");
    if (!mobius_stack_isString(state, 1 - arg_count))
        return mobius_error(state, "regex.replace() second argument must be a string");
    if (!mobius_stack_isString(state, 2 - arg_count))
        return mobius_error(state, "regex.replace() third argument must be a string");

    RegexOptions options;
    if (arg_count == 4) {
        char error[256];
        if (!read_regex_options(state, mobius_stack_size(state) - 1, options, error, sizeof(error))) {
            return mobius_error(state, error);
        }
    }

    const char* pattern     = mobius_stack_asString(state, -arg_count);
    const char* subject     = mobius_stack_asString(state, 1 - arg_count);
    const char* replacement = mobius_stack_asString(state, 2 - arg_count);

    std::string out;
    std::string error;
    if (!regex_replace_all(pattern, subject, replacement, out, options, &error))
        return mobius_error(state, ("regex.replace() invalid regex pattern: " + error).c_str());

    mobius_stack_pop(state, arg_count);
    mobius_stack_pushString(state, out.c_str());
    return 1;
}

// ============================================================================
// regex.split(pattern, string) -> array of strings
// ============================================================================

static int regex_split(MobiusState* state, int arg_count, void* /*userdata*/) {
    if (arg_count < 2 || arg_count > 3)
        return mobius_error(state, "regex.split() expects 2 or 3 arguments (pattern, string [, flags|options])");
    if (!mobius_stack_isString(state, -arg_count))
        return mobius_error(state, "regex.split() first argument must be a string");
    if (!mobius_stack_isString(state, 1 - arg_count))
        return mobius_error(state, "regex.split() second argument must be a string");

    RegexOptions options;
    if (arg_count == 3) {
        char error[256];
        if (!read_regex_options(state, mobius_stack_size(state) - 1, options, error, sizeof(error))) {
            return mobius_error(state, error);
        }
    }

    const char* pattern = mobius_stack_asString(state, -arg_count);
    const char* subject = mobius_stack_asString(state, 1 - arg_count);

    std::vector<std::string> parts;
    std::string error;
    if (!regex_split_impl(pattern, subject, parts, options, &error))
        return mobius_error(state, ("regex.split() invalid regex pattern: " + error).c_str());

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
    {"match",   regex_match,   SIZE_MAX, MOBIUS_VAL_TABLE,   "Full match: returns table if pattern matches entire string, else nil"},
    {"search",  regex_search,  SIZE_MAX, MOBIUS_VAL_TABLE,   "Search: returns table for first match found anywhere, else nil"},
    {"findall", regex_findall, SIZE_MAX, MOBIUS_VAL_ARRAY,   "Find all matches, return array of match tables"},
    {"replace", regex_replace, SIZE_MAX, MOBIUS_VAL_STRING,  "Replace all occurrences of pattern in string"},
    {"split",   regex_split,   SIZE_MAX, MOBIUS_VAL_ARRAY,   "Split string by regex pattern, return array of strings"},
};

static MobiusPlugin regex_plugin = {
    {
        "regex" /* name */,
        "1.0.0" /* version */,
        "Regular Expression Support" /* description */,
        "Mobius Team" /* author */,
        MOBIUS_PLUGIN_API_VERSION /* api_version */,
        "MIT" /* license */,
        nullptr /* depends_on */,
        0 /* depends_on_count */
    },
    regex_functions /* functions */,
    sizeof(regex_functions) / sizeof(regex_functions[0]) /* function_count */,
    init_regex_plugin /* init_plugin */,
    cleanup_regex_plugin /* cleanup_plugin */,
    nullptr /* post_init */
};

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &regex_plugin;
}
