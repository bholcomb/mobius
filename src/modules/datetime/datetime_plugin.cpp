#include <mobius/mobius_plugin.h>

#include <ctime>
#include <cstdio>
#include <cstring>
#include <string>
#include <cctype>

#ifdef _WIN32
  #include <time.h>
#endif

static bool localtime_safe(time_t value, struct tm* out) {
#ifdef _WIN32
    return localtime_s(out, &value) == 0;
#else
    return localtime_r(&value, out) != nullptr;
#endif
}

static bool gmtime_safe(time_t value, struct tm* out) {
#ifdef _WIN32
    return gmtime_s(out, &value) == 0;
#else
    return gmtime_r(&value, out) != nullptr;
#endif
}

static time_t timegm_safe(struct tm* tm_value) {
#ifdef _WIN32
    return _mkgmtime(tm_value);
#else
    return timegm(tm_value);
#endif
}

static void push_datetime_table(MobiusState* state, const struct tm& tm_value,
                                int64_t timestamp, bool utc,
                                bool has_offset = false, int offset_minutes = 0) {
    mobius_stack_pushNewTable(state, 12);
    int tbl = mobius_stack_size(state) - 1;

    mobius_stack_pushInt64(state, (int64_t)tm_value.tm_year + 1900);
    mobius_stack_setTableField(state, tbl, "year");
    mobius_stack_pushInt64(state, (int64_t)tm_value.tm_mon + 1);
    mobius_stack_setTableField(state, tbl, "month");
    mobius_stack_pushInt64(state, (int64_t)tm_value.tm_mday);
    mobius_stack_setTableField(state, tbl, "day");
    mobius_stack_pushInt64(state, (int64_t)tm_value.tm_hour);
    mobius_stack_setTableField(state, tbl, "hour");
    mobius_stack_pushInt64(state, (int64_t)tm_value.tm_min);
    mobius_stack_setTableField(state, tbl, "min");
    mobius_stack_pushInt64(state, (int64_t)tm_value.tm_sec);
    mobius_stack_setTableField(state, tbl, "sec");
    mobius_stack_pushInt64(state, (int64_t)tm_value.tm_wday);
    mobius_stack_setTableField(state, tbl, "wday");
    mobius_stack_pushInt64(state, (int64_t)tm_value.tm_yday + 1);
    mobius_stack_setTableField(state, tbl, "yday");
    mobius_stack_pushBool(state, tm_value.tm_isdst > 0);
    mobius_stack_setTableField(state, tbl, "isdst");
    mobius_stack_pushBool(state, utc);
    mobius_stack_setTableField(state, tbl, "utc");
    mobius_stack_pushInt64(state, timestamp);
    mobius_stack_setTableField(state, tbl, "timestamp");
    if (has_offset) {
        mobius_stack_pushInt64(state, offset_minutes);
        mobius_stack_setTableField(state, tbl, "offset_minutes");
    }
}

static bool read_required_int_field(MobiusState* state, int tbl_idx, const char* key,
                                    int* out, char* error, size_t error_size) {
    mobius_stack_getTableField(state, tbl_idx, key);
    if (!mobius_stack_isInteger(state, -1)) {
        snprintf(error, error_size, "datetime value missing integer field '%s'", key);
        mobius_stack_pop(state, 1);
        return false;
    }
    *out = (int)mobius_stack_asInt64(state, -1);
    mobius_stack_pop(state, 1);
    return true;
}

static bool read_datetime_table(MobiusState* state, int tbl_idx, struct tm& out_tm,
                                bool& utc, bool& has_offset, int& offset_minutes,
                                char* error, size_t error_size) {
    memset(&out_tm, 0, sizeof(out_tm));
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (!read_required_int_field(state, tbl_idx, "year", &year, error, error_size) ||
        !read_required_int_field(state, tbl_idx, "month", &month, error, error_size) ||
        !read_required_int_field(state, tbl_idx, "day", &day, error, error_size) ||
        !read_required_int_field(state, tbl_idx, "hour", &hour, error, error_size) ||
        !read_required_int_field(state, tbl_idx, "min", &minute, error, error_size) ||
        !read_required_int_field(state, tbl_idx, "sec", &second, error, error_size)) {
        return false;
    }

    mobius_stack_getTableField(state, tbl_idx, "utc");
    utc = !mobius_stack_isNil(state, -1) && mobius_stack_asBool(state, -1);
    mobius_stack_pop(state, 1);

    has_offset = false;
    offset_minutes = 0;
    mobius_stack_getTableField(state, tbl_idx, "offset_minutes");
    if (!mobius_stack_isNil(state, -1)) {
        if (!mobius_stack_isInteger(state, -1)) {
            snprintf(error, error_size, "datetime value field 'offset_minutes' must be an integer");
            mobius_stack_pop(state, 1);
            return false;
        }
        has_offset = true;
        offset_minutes = (int)mobius_stack_asInt64(state, -1);
    }
    mobius_stack_pop(state, 1);

    out_tm.tm_year = year - 1900;
    out_tm.tm_mon = month - 1;
    out_tm.tm_mday = day;
    out_tm.tm_hour = hour;
    out_tm.tm_min = minute;
    out_tm.tm_sec = second;
    out_tm.tm_isdst = utc ? 0 : -1;
    return true;
}

static void append_iso_offset(std::string& out, int offset_minutes) {
    if (offset_minutes == 0) {
        out.push_back('Z');
        return;
    }
    char sign = '+';
    int total = offset_minutes;
    if (total < 0) {
        sign = '-';
        total = -total;
    }
    int hours = total / 60;
    int minutes = total % 60;
    char buf[8];
    snprintf(buf, sizeof(buf), "%c%02d:%02d", sign, hours, minutes);
    out.append(buf);
}

static bool parse_fixed_digits(const char* text, size_t offset, size_t count, int* out) {
    int value = 0;
    for (size_t i = 0; i < count; i++) {
        unsigned char c = (unsigned char)text[offset + i];
        if (!std::isdigit(c)) return false;
        value = (value * 10) + (c - '0');
    }
    *out = value;
    return true;
}

static int datetime_now(MobiusState* state, int arg_count) {
    (void)arg_count;
    time_t now = ::time(nullptr);
    struct tm tm_value;
    if (!localtime_safe(now, &tm_value)) {
        mobius_stack_pushNil(state);
        return 1;
    }
    push_datetime_table(state, tm_value, (int64_t)now, false);
    return 1;
}

static int datetime_utc_now(MobiusState* state, int arg_count) {
    (void)arg_count;
    time_t now = ::time(nullptr);
    struct tm tm_value;
    if (!gmtime_safe(now, &tm_value)) {
        mobius_stack_pushNil(state);
        return 1;
    }
    push_datetime_table(state, tm_value, (int64_t)now, true, true, 0);
    return 1;
}

static int datetime_from_unix(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "datetime.from_unix() expects 1 argument");
    if (!mobius_stack_isNumber(state, -1))
        return mobius_error(state, "datetime.from_unix() expects a numeric argument");
    time_t value = (time_t)mobius_stack_asInt64(state, -1);
    mobius_stack_pop(state, 1);
    struct tm tm_value;
    if (!localtime_safe(value, &tm_value)) {
        mobius_stack_pushNil(state);
        return 1;
    }
    push_datetime_table(state, tm_value, (int64_t)value, false);
    return 1;
}

static int datetime_from_unix_utc(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "datetime.from_unix_utc() expects 1 argument");
    if (!mobius_stack_isNumber(state, -1))
        return mobius_error(state, "datetime.from_unix_utc() expects a numeric argument");
    time_t value = (time_t)mobius_stack_asInt64(state, -1);
    mobius_stack_pop(state, 1);
    struct tm tm_value;
    if (!gmtime_safe(value, &tm_value)) {
        mobius_stack_pushNil(state);
        return 1;
    }
    push_datetime_table(state, tm_value, (int64_t)value, true, true, 0);
    return 1;
}

static int datetime_to_unix(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "datetime.to_unix() expects 1 argument");
    if (!mobius_stack_isTable(state, -1))
        return mobius_error(state, "datetime.to_unix() expects a datetime table");

    struct tm tm_value;
    bool utc = false;
    bool has_offset = false;
    int offset_minutes = 0;
    char error[256];
    if (!read_datetime_table(state, mobius_stack_size(state) - 1, tm_value, utc,
                             has_offset, offset_minutes, error, sizeof(error))) {
        return mobius_error(state, error);
    }

    time_t result;
    if (has_offset || utc) {
        result = timegm_safe(&tm_value);
        if (result == (time_t)-1) return mobius_error(state, "datetime.to_unix() conversion failed");
        if (has_offset) result -= (time_t)offset_minutes * 60;
    } else {
        result = ::mktime(&tm_value);
        if (result == (time_t)-1) return mobius_error(state, "datetime.to_unix() conversion failed");
    }

    mobius_stack_pop(state, 1);
    mobius_stack_pushInt64(state, (int64_t)result);
    return 1;
}

static int datetime_format(MobiusState* state, int arg_count) {
    if (arg_count != 2)
        return mobius_error(state, "datetime.format() expects 2 arguments (format, value)");
    if (!mobius_stack_isString(state, -2))
        return mobius_error(state, "datetime.format() first argument must be a string");

    const char* fmt = mobius_stack_asString(state, -2);
    struct tm tm_value;

    if (mobius_stack_isTable(state, -1)) {
        bool utc = false;
        bool has_offset = false;
        int offset_minutes = 0;
        char error[256];
        if (!read_datetime_table(state, mobius_stack_size(state) - 1, tm_value, utc,
                                 has_offset, offset_minutes, error, sizeof(error))) {
            return mobius_error(state, error);
        }
    } else if (mobius_stack_isNumber(state, -1)) {
        time_t value = (time_t)mobius_stack_asInt64(state, -1);
        if (!localtime_safe(value, &tm_value)) {
            mobius_stack_pushNil(state);
            return 1;
        }
    } else {
        return mobius_error(state, "datetime.format() second argument must be a datetime table or timestamp");
    }

    char buf[256];
    size_t written = ::strftime(buf, sizeof(buf), fmt, &tm_value);
    mobius_stack_pop(state, 2);
    if (written == 0) {
        mobius_stack_pushString(state, "");
    } else {
        mobius_stack_pushString(state, buf);
    }
    return 1;
}

static int datetime_isoformat(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "datetime.isoformat() expects 1 argument");
    if (!mobius_stack_isTable(state, -1))
        return mobius_error(state, "datetime.isoformat() expects a datetime table");

    struct tm tm_value;
    bool utc = false;
    bool has_offset = false;
    int offset_minutes = 0;
    char error[256];
    if (!read_datetime_table(state, mobius_stack_size(state) - 1, tm_value, utc,
                             has_offset, offset_minutes, error, sizeof(error))) {
        return mobius_error(state, error);
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d",
             tm_value.tm_year + 1900, tm_value.tm_mon + 1, tm_value.tm_mday,
             tm_value.tm_hour, tm_value.tm_min, tm_value.tm_sec);
    std::string out(buf);
    if (has_offset) append_iso_offset(out, offset_minutes);
    else if (utc) out.push_back('Z');

    mobius_stack_pop(state, 1);
    mobius_stack_pushString(state, out.c_str());
    return 1;
}

static int datetime_parse_iso(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "datetime.parse_iso() expects 1 argument");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "datetime.parse_iso() expects a string argument");

    const char* text = mobius_stack_asString(state, -1);
    size_t len = strlen(text);
    mobius_stack_pop(state, 1);

    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    int offset_minutes = 0;
    bool has_time = false;
    bool has_offset = false;
    bool utc = false;

    if (len < 10 ||
        !parse_fixed_digits(text, 0, 4, &year) ||
        text[4] != '-' ||
        !parse_fixed_digits(text, 5, 2, &month) ||
        text[7] != '-' ||
        !parse_fixed_digits(text, 8, 2, &day)) {
        return mobius_error(state, "datetime.parse_iso() expected YYYY-MM-DD or ISO datetime");
    }

    size_t pos = 10;
    if (pos < len) {
        if (text[pos] != 'T' && text[pos] != ' ') {
            return mobius_error(state, "datetime.parse_iso() invalid date/time separator");
        }
        has_time = true;
        if (len < pos + 9 ||
            !parse_fixed_digits(text, pos + 1, 2, &hour) ||
            text[pos + 3] != ':' ||
            !parse_fixed_digits(text, pos + 4, 2, &minute) ||
            text[pos + 6] != ':' ||
            !parse_fixed_digits(text, pos + 7, 2, &second)) {
            return mobius_error(state, "datetime.parse_iso() expected HH:MM:SS time component");
        }
        pos += 9;
    }

    if (pos < len) {
        if (text[pos] == 'Z' && pos + 1 == len) {
            utc = true;
            has_offset = true;
            offset_minutes = 0;
        } else if ((text[pos] == '+' || text[pos] == '-') && pos + 6 == len &&
                   std::isdigit((unsigned char)text[pos + 1]) &&
                   std::isdigit((unsigned char)text[pos + 2]) &&
                   text[pos + 3] == ':' &&
                   std::isdigit((unsigned char)text[pos + 4]) &&
                   std::isdigit((unsigned char)text[pos + 5])) {
            int hours = 0;
            int minutes = 0;
            parse_fixed_digits(text, pos + 1, 2, &hours);
            parse_fixed_digits(text, pos + 4, 2, &minutes);
            offset_minutes = (hours * 60) + minutes;
            if (text[pos] == '-') offset_minutes = -offset_minutes;
            has_offset = true;
        } else {
            return mobius_error(state, "datetime.parse_iso() unsupported timezone suffix");
        }
    }

    struct tm tm_value;
    memset(&tm_value, 0, sizeof(tm_value));
    tm_value.tm_year = year - 1900;
    tm_value.tm_mon = month - 1;
    tm_value.tm_mday = day;
    tm_value.tm_hour = has_time ? hour : 0;
    tm_value.tm_min = has_time ? minute : 0;
    tm_value.tm_sec = has_time ? second : 0;
    tm_value.tm_isdst = -1;

    time_t timestamp;
    if (has_offset || utc) {
        timestamp = timegm_safe(&tm_value);
        if (timestamp == (time_t)-1) return mobius_error(state, "datetime.parse_iso() conversion failed");
        timestamp -= (time_t)offset_minutes * 60;
    } else {
        timestamp = ::mktime(&tm_value);
        if (timestamp == (time_t)-1) return mobius_error(state, "datetime.parse_iso() conversion failed");
    }

    push_datetime_table(state, tm_value, (int64_t)timestamp, utc, has_offset, offset_minutes);
    return 1;
}

static int init_datetime_plugin(MobiusState* /*state*/) { return 0; }
static void cleanup_datetime_plugin(void) {}

static MobiusPluginFunction datetime_functions[] = {
    {"now",            datetime_now,            0, MOBIUS_VAL_TABLE,  "Get the current local datetime"},
    {"utc_now",        datetime_utc_now,        0, MOBIUS_VAL_TABLE,  "Get the current UTC datetime"},
    {"from_unix",      datetime_from_unix,      1, MOBIUS_VAL_TABLE,  "Convert a Unix timestamp to local datetime"},
    {"from_unix_utc",  datetime_from_unix_utc,  1, MOBIUS_VAL_TABLE,  "Convert a Unix timestamp to UTC datetime"},
    {"to_unix",        datetime_to_unix,        1, MOBIUS_VAL_INT64,  "Convert a datetime table to Unix timestamp"},
    {"format",         datetime_format,         2, MOBIUS_VAL_STRING, "Format a datetime table or timestamp with strftime syntax"},
    {"isoformat",      datetime_isoformat,      1, MOBIUS_VAL_STRING, "Render a datetime table as ISO-8601"},
    {"parse_iso",      datetime_parse_iso,      1, MOBIUS_VAL_TABLE,  "Parse an ISO-8601 date or datetime string"},
};

static MobiusPlugin datetime_plugin = {
    .metadata = {
        .name = "datetime",
        .version = "1.0.0",
        .description = "Structured date and time helpers",
        .author = "Mobius Team",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license = "MIT"
    },
    .functions = datetime_functions,
    .function_count = sizeof(datetime_functions) / sizeof(datetime_functions[0]),
    .init_plugin = init_datetime_plugin,
    .cleanup_plugin = cleanup_datetime_plugin,
    .post_init = nullptr,
};

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &datetime_plugin;
}
