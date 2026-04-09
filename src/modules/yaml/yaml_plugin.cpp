#include <mobius/mobius_plugin.h>

#include <cctype>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

namespace {

static constexpr int MAX_DEPTH = 128;

struct YamlNode {
    enum Type { NIL, BOOL, INT, FLOAT, STRING, ARRAY, OBJECT } type = NIL;
    bool bool_value = false;
    int64_t int_value = 0;
    double float_value = 0.0;
    std::string string_value;
    std::vector<YamlNode> array_items;
    std::vector<std::pair<std::string, YamlNode>> object_items;
};

struct YamlLine {
    int indent = 0;
    int line_no = 0;
    std::string text;
};

static std::string trim_left(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
    return s.substr(i);
}

static std::string trim_right(const std::string& s) {
    size_t end = s.size();
    while (end > 0 && std::isspace((unsigned char)s[end - 1])) end--;
    return s.substr(0, end);
}

static std::string trim(const std::string& s) {
    return trim_right(trim_left(s));
}

static bool starts_sequence_item(const std::string& text) {
    return !text.empty() && text[0] == '-' &&
           (text.size() == 1 || std::isspace((unsigned char)text[1]));
}

static size_t find_top_level_colon(const std::string& text) {
    int bracket = 0;
    int brace = 0;
    char quote = '\0';
    bool escape = false;
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (quote) {
            if (quote == '"' && escape) {
                escape = false;
                continue;
            }
            if (quote == '"' && c == '\\') {
                escape = true;
                continue;
            }
            if (c == quote) quote = '\0';
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            continue;
        }
        if (c == '[') bracket++;
        else if (c == ']') bracket--;
        else if (c == '{') brace++;
        else if (c == '}') brace--;
        else if (c == ':' && bracket == 0 && brace == 0) return i;
    }
    return std::string::npos;
}

static std::vector<std::string> split_top_level(const std::string& text, char delimiter) {
    std::vector<std::string> parts;
    int bracket = 0;
    int brace = 0;
    char quote = '\0';
    bool escape = false;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (quote) {
            if (quote == '"' && escape) {
                escape = false;
                continue;
            }
            if (quote == '"' && c == '\\') {
                escape = true;
                continue;
            }
            if (c == quote) quote = '\0';
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            continue;
        }
        if (c == '[') bracket++;
        else if (c == ']') bracket--;
        else if (c == '{') brace++;
        else if (c == '}') brace--;
        else if (c == delimiter && bracket == 0 && brace == 0) {
            parts.push_back(trim(text.substr(start, i - start)));
            start = i + 1;
        }
    }
    parts.push_back(trim(text.substr(start)));
    return parts;
}

static bool parse_double_quoted(const std::string& text, std::string& out) {
    out.clear();
    for (size_t i = 1; i + 1 < text.size(); i++) {
        char c = text[i];
        if (c == '\\' && i + 1 < text.size() - 1) {
            char next = text[++i];
            switch (next) {
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case '\\': out.push_back('\\'); break;
                case '"': out.push_back('"'); break;
                default: out.push_back(next); break;
            }
        } else {
            out.push_back(c);
        }
    }
    return true;
}

static bool parse_single_quoted(const std::string& text, std::string& out) {
    out.clear();
    for (size_t i = 1; i + 1 < text.size(); i++) {
        char c = text[i];
        if (c == '\'' && i + 1 < text.size() - 1 && text[i + 1] == '\'') {
            out.push_back('\'');
            i++;
        } else {
            out.push_back(c);
        }
    }
    return true;
}

static bool parse_inline_value(const std::string& text, YamlNode& out, char* error, size_t error_size, int depth);

static bool parse_flow_array(const std::string& text, YamlNode& out, char* error, size_t error_size, int depth) {
    out.type = YamlNode::ARRAY;
    std::string inner = trim(text.substr(1, text.size() - 2));
    if (inner.empty()) return true;
    std::vector<std::string> parts = split_top_level(inner, ',');
    for (const std::string& part : parts) {
        if (part.empty()) {
            snprintf(error, error_size, "yaml.parse() invalid inline array entry");
            return false;
        }
        YamlNode item;
        if (!parse_inline_value(part, item, error, error_size, depth + 1)) return false;
        out.array_items.push_back(std::move(item));
    }
    return true;
}

static bool parse_flow_object(const std::string& text, YamlNode& out, char* error, size_t error_size, int depth) {
    out.type = YamlNode::OBJECT;
    std::string inner = trim(text.substr(1, text.size() - 2));
    if (inner.empty()) return true;
    std::vector<std::string> parts = split_top_level(inner, ',');
    for (const std::string& part : parts) {
        if (part.empty()) {
            snprintf(error, error_size, "yaml.parse() invalid inline object entry");
            return false;
        }
        size_t colon = find_top_level_colon(part);
        if (colon == std::string::npos) {
            snprintf(error, error_size, "yaml.parse() invalid inline object entry '%s'", part.c_str());
            return false;
        }
        std::string key = trim(part.substr(0, colon));
        std::string value = trim(part.substr(colon + 1));
        if (key.size() >= 2 && ((key.front() == '"' && key.back() == '"') || (key.front() == '\'' && key.back() == '\''))) {
            std::string unquoted;
            if (key.front() == '"') parse_double_quoted(key, unquoted);
            else parse_single_quoted(key, unquoted);
            key = unquoted;
        }
        YamlNode item;
        if (!parse_inline_value(value, item, error, error_size, depth + 1)) return false;
        out.object_items.push_back({key, std::move(item)});
    }
    return true;
}

static bool parse_inline_value(const std::string& raw_text, YamlNode& out, char* error, size_t error_size, int depth) {
    if (depth > MAX_DEPTH) {
        snprintf(error, error_size, "yaml.parse() exceeded maximum nesting depth");
        return false;
    }

    std::string text = trim(raw_text);
    if (text.empty() || text == "~" || text == "null" || text == "nil") {
        out.type = YamlNode::NIL;
        return true;
    }
    if (text == "true") {
        out.type = YamlNode::BOOL;
        out.bool_value = true;
        return true;
    }
    if (text == "false") {
        out.type = YamlNode::BOOL;
        out.bool_value = false;
        return true;
    }
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        out.type = YamlNode::STRING;
        return parse_double_quoted(text, out.string_value);
    }
    if (text.size() >= 2 && text.front() == '\'' && text.back() == '\'') {
        out.type = YamlNode::STRING;
        return parse_single_quoted(text, out.string_value);
    }
    if (text.size() >= 2 && text.front() == '[' && text.back() == ']') {
        return parse_flow_array(text, out, error, error_size, depth);
    }
    if (text.size() >= 2 && text.front() == '{' && text.back() == '}') {
        return parse_flow_object(text, out, error, error_size, depth);
    }

    char* end_ptr = nullptr;
    errno = 0;
    long long int_val = strtoll(text.c_str(), &end_ptr, 10);
    if (errno == 0 && end_ptr && *end_ptr == '\0') {
        out.type = YamlNode::INT;
        out.int_value = (int64_t)int_val;
        return true;
    }

    bool maybe_float = text.find('.') != std::string::npos || text.find('e') != std::string::npos || text.find('E') != std::string::npos;
    if (maybe_float) {
        errno = 0;
        char* f_end = nullptr;
        double float_val = strtod(text.c_str(), &f_end);
        if (errno == 0 && f_end && *f_end == '\0') {
            out.type = YamlNode::FLOAT;
            out.float_value = float_val;
            return true;
        }
    }

    out.type = YamlNode::STRING;
    out.string_value = text;
    return true;
}

static bool is_inline_document_line(const std::string& text) {
    if (starts_sequence_item(text)) return false;
    if (find_top_level_colon(text) == std::string::npos) return true;
    if (!text.empty() && (text[0] == '[' || text[0] == '{' || text[0] == '"' || text[0] == '\'')) return true;
    return false;
}

static bool parse_inline_line_node(const std::vector<YamlLine>& lines, size_t& index, YamlNode& out,
                                   char* error, size_t error_size, int depth) {
    if (index >= lines.size()) {
        snprintf(error, error_size, "yaml.parse() unexpected end of input");
        return false;
    }
    bool ok = parse_inline_value(lines[index].text, out, error, error_size, depth);
    if (ok) index++;
    return ok;
}

static bool push_yaml_node(MobiusState* state, const YamlNode& node) {
    switch (node.type) {
        case YamlNode::NIL:
            mobius_stack_pushNil(state);
            return true;
        case YamlNode::BOOL:
            mobius_stack_pushBool(state, node.bool_value);
            return true;
        case YamlNode::INT:
            mobius_stack_pushInt64(state, node.int_value);
            return true;
        case YamlNode::FLOAT:
            mobius_stack_pushFloat64(state, node.float_value);
            return true;
        case YamlNode::STRING:
            mobius_stack_pushString(state, node.string_value.c_str());
            return true;
        case YamlNode::ARRAY: {
            mobius_stack_pushNewArray(state, node.array_items.size());
            int arr = mobius_stack_size(state) - 1;
            for (const YamlNode& item : node.array_items) {
                push_yaml_node(state, item);
                mobius_stack_arrayPush(state, arr);
            }
            return true;
        }
        case YamlNode::OBJECT: {
            mobius_stack_pushNewTable(state, node.object_items.size());
            int tbl = mobius_stack_size(state) - 1;
            for (const auto& entry : node.object_items) {
                push_yaml_node(state, entry.second);
                mobius_stack_setTableField(state, tbl, entry.first.c_str());
            }
            return true;
        }
    }
    return false;
}

static bool parse_mapping_entries(const std::vector<YamlLine>& lines, size_t& index, int indent,
                                  YamlNode& out, char* error, size_t error_size,
                                  int depth, const std::string* first_text = nullptr);

static bool parse_node(const std::vector<YamlLine>& lines, size_t& index, int indent,
                       YamlNode& out, char* error, size_t error_size, int depth) {
    if (depth > MAX_DEPTH) {
        snprintf(error, error_size, "yaml.parse() exceeded maximum nesting depth");
        return false;
    }
    if (index >= lines.size()) {
        snprintf(error, error_size, "yaml.parse() unexpected end of input");
        return false;
    }
    if (lines[index].indent != indent) {
        snprintf(error, error_size, "yaml.parse() invalid indentation near line %d", lines[index].line_no);
        return false;
    }

    if (starts_sequence_item(lines[index].text)) {
        out.type = YamlNode::ARRAY;
        while (index < lines.size() && lines[index].indent == indent && starts_sequence_item(lines[index].text)) {
            std::string content = trim(lines[index].text.substr(1));
            index++;
            YamlNode item;
            if (content.empty()) {
                if (index < lines.size() && lines[index].indent > indent) {
                    if (is_inline_document_line(lines[index].text)) {
                        if (!parse_inline_line_node(lines, index, item, error, error_size, depth + 1)) return false;
                    } else {
                        if (!parse_node(lines, index, lines[index].indent, item, error, error_size, depth + 1)) return false;
                    }
                } else {
                    item.type = YamlNode::NIL;
                }
            } else if (find_top_level_colon(content) != std::string::npos) {
                item.type = YamlNode::OBJECT;
                int child_indent = indent + 2;
                if (index < lines.size() && lines[index].indent > indent) {
                    child_indent = lines[index].indent;
                }
                if (!parse_mapping_entries(lines, index, child_indent, item, error, error_size, depth + 1, &content)) return false;
            } else {
                if (!parse_inline_value(content, item, error, error_size, depth + 1)) return false;
            }
            out.array_items.push_back(std::move(item));
        }
        return true;
    }

    out.type = YamlNode::OBJECT;
    return parse_mapping_entries(lines, index, indent, out, error, error_size, depth, nullptr);
}

static bool parse_mapping_entries(const std::vector<YamlLine>& lines, size_t& index, int indent,
                                  YamlNode& out, char* error, size_t error_size,
                                  int depth, const std::string* first_text) {
    bool used_first = false;
    while (true) {
        std::string text;
        int line_no = 0;
        if (first_text && !used_first) {
            text = *first_text;
            used_first = true;
        } else {
            if (index >= lines.size() || lines[index].indent != indent || starts_sequence_item(lines[index].text)) break;
            text = lines[index].text;
            line_no = lines[index].line_no;
            index++;
        }

        size_t colon = find_top_level_colon(text);
        if (colon == std::string::npos) {
            snprintf(error, error_size, "yaml.parse() expected key: value near line %d", line_no);
            return false;
        }

        std::string key = trim(text.substr(0, colon));
        if (key.empty()) {
            snprintf(error, error_size, "yaml.parse() empty mapping key near line %d", line_no);
            return false;
        }
        if (key.size() >= 2 && ((key.front() == '"' && key.back() == '"') || (key.front() == '\'' && key.back() == '\''))) {
            std::string unquoted;
            if (key.front() == '"') parse_double_quoted(key, unquoted);
            else parse_single_quoted(key, unquoted);
            key = unquoted;
        }

        std::string value_text = trim(text.substr(colon + 1));
        YamlNode value;
        if (value_text.empty()) {
            if (index < lines.size() && lines[index].indent > indent) {
                if (is_inline_document_line(lines[index].text)) {
                    if (!parse_inline_line_node(lines, index, value, error, error_size, depth + 1)) return false;
                } else {
                    if (!parse_node(lines, index, lines[index].indent, value, error, error_size, depth + 1)) return false;
                }
            } else {
                value.type = YamlNode::NIL;
            }
        } else {
            if (!parse_inline_value(value_text, value, error, error_size, depth + 1)) return false;
        }

        out.object_items.push_back({key, std::move(value)});
    }
    return true;
}

static bool preprocess_yaml_lines(const std::string& input, std::vector<YamlLine>& lines, char* error, size_t error_size) {
    std::istringstream stream(input);
    std::string raw;
    int line_no = 0;
    while (std::getline(stream, raw)) {
        line_no++;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        int indent = 0;
        while (indent < (int)raw.size() && raw[(size_t)indent] == ' ') indent++;
        if (indent < (int)raw.size() && raw[(size_t)indent] == '\t') {
            snprintf(error, error_size, "yaml.parse() tabs are not supported for indentation (line %d)", line_no);
            return false;
        }

        char quote = '\0';
        bool escape = false;
        size_t comment_pos = std::string::npos;
        for (size_t i = (size_t)indent; i < raw.size(); i++) {
            char c = raw[i];
            if (quote) {
                if (quote == '"' && escape) {
                    escape = false;
                    continue;
                }
                if (quote == '"' && c == '\\') {
                    escape = true;
                    continue;
                }
                if (c == quote) quote = '\0';
                continue;
            }
            if (c == '"' || c == '\'') {
                quote = c;
                continue;
            }
            if (c == '#') {
                comment_pos = i;
                break;
            }
        }

        std::string text = comment_pos == std::string::npos ? raw.substr((size_t)indent)
                                                            : raw.substr((size_t)indent, comment_pos - (size_t)indent);
        text = trim_right(text);
        if (trim(text).empty()) continue;
        if (trim(text) == "---" || trim(text) == "...") continue;

        YamlLine line;
        line.indent = indent;
        line.line_no = line_no;
        line.text = trim(text);
        lines.push_back(std::move(line));
    }
    return true;
}

static bool yaml_parse_document(const std::string& input, YamlNode& root, char* error, size_t error_size) {
    std::vector<YamlLine> lines;
    if (!preprocess_yaml_lines(input, lines, error, error_size)) return false;
    if (lines.empty()) {
        root.type = YamlNode::OBJECT;
        return true;
    }

    if (lines.size() == 1 && is_inline_document_line(lines[0].text)) {
        return parse_inline_value(lines[0].text, root, error, error_size, 0);
    }

    size_t index = 0;
    if (!parse_node(lines, index, lines[0].indent, root, error, error_size, 0)) return false;
    if (index != lines.size()) {
        snprintf(error, error_size, "yaml.parse() unexpected trailing content near line %d", lines[index].line_no);
        return false;
    }
    return true;
}

struct YamlSerializer {
    MobiusState* state;
    std::string out;
    char error[256];

    static bool is_scalar_type(MobiusValueType type) {
        return type == MOBIUS_VAL_NIL || type == MOBIUS_VAL_BOOL || type == MOBIUS_VAL_INT64 ||
               type == MOBIUS_VAL_UINT64 || type == MOBIUS_VAL_FLOAT64 || type == MOBIUS_VAL_STRING;
    }

    void write_indent(int indent) {
        for (int i = 0; i < indent; i++) out.push_back(' ');
    }

    std::string quote_string(const char* value) {
        std::string s = value ? value : "";
        bool needs_quotes = s.empty();
        for (char c : s) {
            if (std::isspace((unsigned char)c) || c == ':' || c == '#' || c == '-' ||
                c == '[' || c == ']' || c == '{' || c == '}' || c == ',' || c == '\'' || c == '"') {
                needs_quotes = true;
                break;
            }
        }
        if (s == "true" || s == "false" || s == "null" || s == "nil" || s == "~") needs_quotes = true;
        if (!needs_quotes) return s;
        std::string out_s = "'";
        for (char c : s) {
            if (c == '\'') out_s.append("''");
            else out_s.push_back(c);
        }
        out_s.push_back('\'');
        return out_s;
    }

    std::string scalar_to_string(int idx) {
        switch (mobius_stack_type(state, idx)) {
            case MOBIUS_VAL_NIL:
                return "null";
            case MOBIUS_VAL_BOOL:
                return mobius_stack_asBool(state, idx) ? "true" : "false";
            case MOBIUS_VAL_INT64: {
                char buf[32];
                snprintf(buf, sizeof(buf), "%" PRId64, mobius_stack_asInt64(state, idx));
                return buf;
            }
            case MOBIUS_VAL_UINT64: {
                char buf[32];
                snprintf(buf, sizeof(buf), "%" PRIu64, mobius_stack_asUInt64(state, idx));
                return buf;
            }
            case MOBIUS_VAL_FLOAT64: {
                char buf[64];
                snprintf(buf, sizeof(buf), "%.17g", mobius_stack_asFloat64(state, idx));
                return buf;
            }
            case MOBIUS_VAL_STRING:
                return quote_string(mobius_stack_asString(state, idx));
            default:
                return "";
        }
    }

    void collect_sorted_keys(int tbl_idx, std::vector<std::string>& keys) {
        mobius_stack_getTableKeys(state, tbl_idx);
        int keys_idx = mobius_stack_size(state) - 1;
        size_t count = mobius_stack_getArrayLength(state, keys_idx);
        keys.clear();
        keys.reserve(count);
        for (size_t i = 0; i < count; i++) {
            mobius_stack_getArrayElement(state, keys_idx, i);
            if (mobius_stack_isString(state, -1)) keys.emplace_back(mobius_stack_asString(state, -1));
            mobius_stack_pop(state, 1);
        }
        mobius_stack_pop(state, 1);
        std::sort(keys.begin(), keys.end());
    }

    bool serialize_value(int idx, int indent, bool root = false) {
        if (!root && is_scalar_type(mobius_stack_type(state, idx))) {
            out.append(scalar_to_string(idx));
            return true;
        }

        if (mobius_stack_type(state, idx) == MOBIUS_VAL_ARRAY) {
            size_t len = mobius_stack_getArrayLength(state, idx);
            if (len == 0) {
                if (!root && indent > 0) write_indent(indent);
                out.append("[]");
                if (!root && indent > 0) out.push_back('\n');
                return true;
            }
            for (size_t i = 0; i < len; i++) {
                mobius_stack_getArrayElement(state, idx, i);
                int elem = mobius_stack_size(state) - 1;
                write_indent(indent);
                if (is_scalar_type(mobius_stack_type(state, elem))) {
                    out.append("- ");
                    out.append(scalar_to_string(elem));
                    out.push_back('\n');
                } else {
                    out.append("-\n");
                    if (!serialize_value(elem, indent + 2)) {
                        mobius_stack_pop(state, 1);
                        return false;
                    }
                }
                mobius_stack_pop(state, 1);
            }
            return true;
        }

        if (mobius_stack_type(state, idx) == MOBIUS_VAL_TABLE) {
            std::vector<std::string> keys;
            collect_sorted_keys(idx, keys);
            if (keys.empty()) {
                if (!root && indent > 0) write_indent(indent);
                out.append("{}");
                if (!root && indent > 0) out.push_back('\n');
                return true;
            }
            for (const std::string& key : keys) {
                mobius_stack_getTableField(state, idx, key.c_str());
                int value = mobius_stack_size(state) - 1;
                write_indent(indent);
                out.append(quote_string(key.c_str()));
                out.push_back(':');
                if (is_scalar_type(mobius_stack_type(state, value))) {
                    out.push_back(' ');
                    out.append(scalar_to_string(value));
                    out.push_back('\n');
                } else {
                    out.push_back('\n');
                    if (!serialize_value(value, indent + 2)) {
                        mobius_stack_pop(state, 1);
                        return false;
                    }
                }
                mobius_stack_pop(state, 1);
            }
            return true;
        }

        snprintf(error, sizeof(error), "yaml.stringify() unsupported value type");
        return false;
    }
};

} // namespace

static int yaml_parse(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "yaml.parse() expects 1 argument (string)");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "yaml.parse() argument must be a string");

    std::string input = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);

    YamlNode root;
    char error[256];
    if (!yaml_parse_document(input, root, error, sizeof(error))) {
        return mobius_error(state, error);
    }
    push_yaml_node(state, root);
    return 1;
}

static int yaml_parsefile(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "yaml.parsefile() expects 1 argument (path)");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "yaml.parsefile() argument must be a string");

    const char* path = mobius_stack_asString(state, -1);
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return mobius_error(state, "yaml.parsefile() could not open file");
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    std::string input = ss.str();
    mobius_stack_pop(state, 1);

    YamlNode root;
    char error[256];
    if (!yaml_parse_document(input, root, error, sizeof(error))) {
        return mobius_error(state, error);
    }
    push_yaml_node(state, root);
    return 1;
}

static int yaml_stringify(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "yaml.stringify() expects 1 argument");

    int idx = mobius_stack_size(state) - 1;
    YamlSerializer ser;
    ser.state = state;
    ser.error[0] = '\0';
    if (!ser.serialize_value(idx, 0, true)) {
        return mobius_error(state, ser.error);
    }
    if (!ser.out.empty() && ser.out.back() == '\n') {
        // keep a single trailing newline for documents
    } else if (!ser.out.empty()) {
        ser.out.push_back('\n');
    }
    mobius_stack_pop(state, 1);
    mobius_stack_pushString(state, ser.out.c_str());
    return 1;
}

static int init_yaml_plugin(MobiusState* /*state*/) { return 0; }
static void cleanup_yaml_plugin(void) {}

static MobiusPluginFunction yaml_functions[] = {
    {"parse",     yaml_parse,     1, MOBIUS_VAL_UNKNOWN, "Parse a YAML string into Mobius values"},
    {"parsefile", yaml_parsefile, 1, MOBIUS_VAL_UNKNOWN, "Parse a YAML file into Mobius values"},
    {"stringify", yaml_stringify, 1, MOBIUS_VAL_STRING,  "Serialize a Mobius value to YAML"},
};

static MobiusPlugin yaml_plugin = {
    .metadata = {
        .name = "yaml",
        .version = "1.0.0",
        .description = "Dependency-free YAML subset parser and serializer",
        .author = "Mobius Team",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license = "MIT"
    },
    .functions = yaml_functions,
    .function_count = sizeof(yaml_functions) / sizeof(yaml_functions[0]),
    .init_plugin = init_yaml_plugin,
    .cleanup_plugin = cleanup_yaml_plugin,
    .post_init = nullptr,
};

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &yaml_plugin;
}
