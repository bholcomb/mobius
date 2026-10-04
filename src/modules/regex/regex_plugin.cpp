#include <mobius/mobius_plugin.h>

#include <cstring>
#include <string>
#include <vector>

#include "regex_platform.h"

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

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

void append_replacement_template(const char* replacement, const MatchResult& match, std::string& out) {
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
    if (!compile_and_match(pattern, subject, mr, options))
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
    if (!compile_and_match(pattern, subject, mr, options))
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
    if (!find_all_matches(pattern, subject, results, options))
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
    if (!regex_replace_all(pattern, subject, replacement, out, options))
        return mobius_error(state, "regex.replace() invalid regex pattern");

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
    if (!regex_split_impl(pattern, subject, parts, options))
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
