#include <mobius/mobius_plugin.h>
#include "state/mobius_state.h"

#include <sqlite3.h>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

static const char* SQLITE_DATABASE_TYPE = "sqlite_database";
static const char* SQLITE_STATEMENT_TYPE = "sqlite_statement";

struct StatementObject;

struct DatabaseObject {
    sqlite3* handle = nullptr;
    bool closed = false;
    std::mutex mutex;
    std::unordered_set<StatementObject*> statements;
};

struct StatementObject {
    DatabaseObject* owner = nullptr;
    sqlite3_stmt* stmt = nullptr;
    bool finalized = false;
    bool has_row = false;
};

static bool is_callable_type(MobiusValueType type) {
    return type == MOBIUS_VAL_FUNCTION || type == MOBIUS_VAL_NATIVE_FUNCTION;
}

static void push_string_field(MobiusState* state, int table_idx, const char* key, const std::string& value) {
    mobius_stack_pushStringLength(state, value.data(), value.size());
    mobius_stack_setTableField(state, table_idx, key);
}

static void push_int_field(MobiusState* state, int table_idx, const char* key, int64_t value) {
    mobius_stack_pushInt64(state, value);
    mobius_stack_setTableField(state, table_idx, key);
}

static void push_bool_field(MobiusState* state, int table_idx, const char* key, bool value) {
    mobius_stack_pushBool(state, value);
    mobius_stack_setTableField(state, table_idx, key);
}

static std::string sqlite_error_from_handle(sqlite3* db, const char* fallback) {
    if (db) {
        const char* msg = sqlite3_errmsg(db);
        if (msg && msg[0] != '\0') return std::string(msg);
    }
    return std::string(fallback);
}

static DatabaseObject* get_database_object(MobiusState* state, int idx, const char* context) {
    const char* type_name = nullptr;
    void* ptr = mobius_stack_getUserdata(state, idx, &type_name);
    if (!ptr || !type_name || strcmp(type_name, SQLITE_DATABASE_TYPE) != 0) {
        mobius_error(state, context);
        return nullptr;
    }
    return static_cast<DatabaseObject*>(ptr);
}

static StatementObject* get_statement_object(MobiusState* state, int idx, const char* context) {
    const char* type_name = nullptr;
    void* ptr = mobius_stack_getUserdata(state, idx, &type_name);
    if (!ptr || !type_name || strcmp(type_name, SQLITE_STATEMENT_TYPE) != 0) {
        mobius_error(state, context);
        return nullptr;
    }
    return static_cast<StatementObject*>(ptr);
}

static void finalize_statement_locked(StatementObject* stmt_obj) {
    if (!stmt_obj || stmt_obj->finalized) return;
    if (stmt_obj->owner) {
        stmt_obj->owner->statements.erase(stmt_obj);
    }
    if (stmt_obj->stmt) {
        sqlite3_finalize(stmt_obj->stmt);
        stmt_obj->stmt = nullptr;
    }
    stmt_obj->has_row = false;
    stmt_obj->finalized = true;
    stmt_obj->owner = nullptr;
}

static int close_database_locked(DatabaseObject* db_obj, std::string* error_out) {
    if (!db_obj || db_obj->closed) return SQLITE_OK;

    std::vector<StatementObject*> statements(db_obj->statements.begin(), db_obj->statements.end());
    for (StatementObject* stmt_obj : statements) {
        if (!stmt_obj) continue;
        if (stmt_obj->stmt) {
            sqlite3_finalize(stmt_obj->stmt);
            stmt_obj->stmt = nullptr;
        }
        stmt_obj->has_row = false;
        stmt_obj->finalized = true;
        stmt_obj->owner = nullptr;
    }
    db_obj->statements.clear();

    sqlite3* handle = db_obj->handle;
    db_obj->handle = nullptr;
    db_obj->closed = true;

    if (!handle) return SQLITE_OK;

    int rc = sqlite3_close(handle);
    if (rc != SQLITE_OK && error_out) {
        *error_out = sqlite_error_from_handle(handle, "sqlite close failed");
    }
    return rc;
}

static void database_object_destructor(void* ptr) {
    DatabaseObject* db_obj = static_cast<DatabaseObject*>(ptr);
    if (!db_obj) return;
    {
        std::lock_guard<std::mutex> lock(db_obj->mutex);
        close_database_locked(db_obj, nullptr);
    }
    delete db_obj;
}

static void statement_object_destructor(void* ptr) {
    StatementObject* stmt_obj = static_cast<StatementObject*>(ptr);
    if (!stmt_obj) return;
    if (stmt_obj->owner) {
        std::lock_guard<std::mutex> lock(stmt_obj->owner->mutex);
        finalize_statement_locked(stmt_obj);
    } else if (stmt_obj->stmt) {
        sqlite3_finalize(stmt_obj->stmt);
        stmt_obj->stmt = nullptr;
        stmt_obj->has_row = false;
        stmt_obj->finalized = true;
    }
    delete stmt_obj;
}

static int resolve_parameter_index(sqlite3_stmt* stmt, MobiusState* state, int idx, int* out_index, const char* context) {
    if (mobius_stack_isString(state, idx)) {
        const char* name = mobius_stack_asString(state, idx);
        int resolved = sqlite3_bind_parameter_index(stmt, name);
        if (resolved <= 0) {
            return mobius_error(state, (std::string(context) + " unknown named parameter").c_str());
        }
        *out_index = resolved;
        return 0;
    }

    if (!mobius_stack_isInteger(state, idx)) {
        return mobius_error(state, (std::string(context) + " parameter index must be an integer or string").c_str());
    }

    int64_t value = mobius_stack_asInt64(state, idx);
    if (value < 1 || value > sqlite3_bind_parameter_count(stmt)) {
        return mobius_error(state, (std::string(context) + " parameter index out of range").c_str());
    }

    *out_index = (int)value;
    return 0;
}

static int bind_stack_value(sqlite3_stmt* stmt, MobiusState* state, int param_idx, int value_idx, const char* context) {
    int rc = SQLITE_OK;
    MobiusValueType type = mobius_stack_type(state, value_idx);
    switch (type) {
        case MOBIUS_VAL_NIL:
            rc = sqlite3_bind_null(stmt, param_idx);
            break;
        case MOBIUS_VAL_BOOL:
            rc = sqlite3_bind_int64(stmt, param_idx, mobius_stack_asBool(state, value_idx) ? 1 : 0);
            break;
        case MOBIUS_VAL_INT64:
        case MOBIUS_VAL_UINT64:
            rc = sqlite3_bind_int64(stmt, param_idx, (sqlite3_int64)mobius_stack_asInt64(state, value_idx));
            break;
        case MOBIUS_VAL_FLOAT64:
            rc = sqlite3_bind_double(stmt, param_idx, mobius_stack_asFloat64(state, value_idx));
            break;
        case MOBIUS_VAL_STRING: {
            size_t len = 0;
            const char* text = mobius_stack_getStringData(state, value_idx, &len);
            rc = sqlite3_bind_text(stmt, param_idx, text ? text : "", (int)len, SQLITE_TRANSIENT);
            break;
        }
        case MOBIUS_VAL_BUFFER: {
            size_t len = 0;
            void* data = mobius_stack_getBufferData(state, value_idx, &len);
            rc = sqlite3_bind_blob(stmt, param_idx, data, (int)len, SQLITE_TRANSIENT);
            break;
        }
        default:
            return mobius_error(state, (std::string(context) + " unsupported value type").c_str());
    }

    if (rc != SQLITE_OK) {
        return mobius_error(state, sqlite3_errmsg(sqlite3_db_handle(stmt)));
    }
    return 0;
}

static void push_database_metrics(MobiusState* state, sqlite3* db) {
    mobius_stack_pushNewTable(state, 5);
    int tbl = mobius_stack_size(state) - 1;
    push_bool_field(state, tbl, "ok", true);
    push_int_field(state, tbl, "changes", (int64_t)sqlite3_changes64(db));
    push_int_field(state, tbl, "total_changes", (int64_t)sqlite3_total_changes64(db));
    push_int_field(state, tbl, "last_insert_rowid", (int64_t)sqlite3_last_insert_rowid(db));
    push_bool_field(state, tbl, "autocommit", sqlite3_get_autocommit(db) != 0);
}

static void push_column_value(MobiusState* state, sqlite3_stmt* stmt, int column_index) {
    switch (sqlite3_column_type(stmt, column_index)) {
        case SQLITE_INTEGER:
            mobius_stack_pushInt64(state, (int64_t)sqlite3_column_int64(stmt, column_index));
            break;
        case SQLITE_FLOAT:
            mobius_stack_pushFloat64(state, sqlite3_column_double(stmt, column_index));
            break;
        case SQLITE_TEXT: {
            const unsigned char* text = sqlite3_column_text(stmt, column_index);
            int bytes = sqlite3_column_bytes(stmt, column_index);
            mobius_stack_pushStringLength(state, reinterpret_cast<const char*>(text ? text : reinterpret_cast<const unsigned char*>("")), (size_t)bytes);
            break;
        }
        case SQLITE_BLOB: {
            const void* blob = sqlite3_column_blob(stmt, column_index);
            int bytes = sqlite3_column_bytes(stmt, column_index);
            mobius_stack_pushBufferCopy(state, blob, (size_t)bytes);
            break;
        }
        case SQLITE_NULL:
        default:
            mobius_stack_pushNil(state);
            break;
    }
}

static const char* column_type_name(int type) {
    switch (type) {
        case SQLITE_INTEGER: return "integer";
        case SQLITE_FLOAT: return "float";
        case SQLITE_TEXT: return "text";
        case SQLITE_BLOB: return "blob";
        case SQLITE_NULL:
        default: return "null";
    }
}

static void push_current_row(MobiusState* state, sqlite3_stmt* stmt) {
    int column_count = sqlite3_column_count(stmt);
    mobius_stack_pushNewTable(state, (size_t)column_count);
    int row_idx = mobius_stack_size(state) - 1;
    for (int i = 0; i < column_count; i++) {
        const char* name = sqlite3_column_name(stmt, i);
        push_column_value(state, stmt, i);
        mobius_stack_setTableField(state, row_idx, name ? name : "");
    }
}

static int sqlite_open(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite.open() expects 1 argument");
    if (!mobius_stack_isString(state, 0)) return mobius_error(state, "sqlite.open() expects a string path");

    const char* path = mobius_stack_asString(state, 0);
    mobius_stack_pop(state, 1);

    sqlite3* db = nullptr;
    int rc = sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        std::string error = sqlite_error_from_handle(db, "sqlite.open() failed");
        if (db) sqlite3_close(db);
        return mobius_error(state, error.c_str());
    }

    DatabaseObject* db_obj = new (std::nothrow) DatabaseObject();
    if (!db_obj) {
        sqlite3_close(db);
        return mobius_error(state, "sqlite.open() failed to allocate database object");
    }
    db_obj->handle = db;
    db_obj->closed = false;

    mobius_stack_pushUserdata(state, db_obj, database_object_destructor, SQLITE_DATABASE_TYPE, sizeof(DatabaseObject));
    return 1;
}

static int sqlite_database_close(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite:close() expects 0 arguments");
    DatabaseObject* db_obj = get_database_object(state, 0, "sqlite:close() self is not a database");
    if (!db_obj) return -1;
    mobius_stack_pop(state, 1);

    std::string error;
    {
        std::lock_guard<std::mutex> lock(db_obj->mutex);
        int rc = close_database_locked(db_obj, &error);
        if (rc != SQLITE_OK) return mobius_error(state, error.c_str());
    }

    mobius_stack_pushBool(state, true);
    return 1;
}

static int sqlite_database_is_closed(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite:is_closed() expects 0 arguments");
    DatabaseObject* db_obj = get_database_object(state, 0, "sqlite:is_closed() self is not a database");
    if (!db_obj) return -1;
    mobius_stack_pop(state, 1);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    mobius_stack_pushBool(state, db_obj->closed);
    return 1;
}

static int sqlite_database_prepare(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "sqlite:prepare() expects 1 argument");
    DatabaseObject* db_obj = get_database_object(state, 0, "sqlite:prepare() self is not a database");
    if (!db_obj) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "sqlite:prepare() expects a SQL string");

    const char* sql = mobius_stack_asString(state, 1);
    mobius_stack_pop(state, 2);

    sqlite3_stmt* stmt = nullptr;
    {
        std::lock_guard<std::mutex> lock(db_obj->mutex);
        if (db_obj->closed || !db_obj->handle) return mobius_error(state, "sqlite database is closed");
        int rc = sqlite3_prepare_v2(db_obj->handle, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            return mobius_error(state, sqlite3_errmsg(db_obj->handle));
        }
    }

    StatementObject* stmt_obj = new (std::nothrow) StatementObject();
    if (!stmt_obj) {
        sqlite3_finalize(stmt);
        return mobius_error(state, "sqlite:prepare() failed to allocate statement object");
    }

    stmt_obj->owner = db_obj;
    stmt_obj->stmt = stmt;
    stmt_obj->finalized = false;
    stmt_obj->has_row = false;

    {
        std::lock_guard<std::mutex> lock(db_obj->mutex);
        if (db_obj->closed || !db_obj->handle) {
            sqlite3_finalize(stmt);
            delete stmt_obj;
            return mobius_error(state, "sqlite database is closed");
        }
        db_obj->statements.insert(stmt_obj);
    }

    mobius_stack_pushUserdata(state, stmt_obj, statement_object_destructor, SQLITE_STATEMENT_TYPE, sizeof(StatementObject));
    return 1;
}

static int sqlite_database_interrupt(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite:interrupt() expects 0 arguments");
    DatabaseObject* db_obj = get_database_object(state, 0, "sqlite:interrupt() self is not a database");
    if (!db_obj) return -1;
    mobius_stack_pop(state, 1);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (db_obj->closed || !db_obj->handle) return mobius_error(state, "sqlite database is closed");
    sqlite3_interrupt(db_obj->handle);
    mobius_stack_pushBool(state, true);
    return 1;
}

static int sqlite_database_changes(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite:changes() expects 0 arguments");
    DatabaseObject* db_obj = get_database_object(state, 0, "sqlite:changes() self is not a database");
    if (!db_obj) return -1;
    mobius_stack_pop(state, 1);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (db_obj->closed || !db_obj->handle) return mobius_error(state, "sqlite database is closed");
    mobius_stack_pushInt64(state, (int64_t)sqlite3_changes64(db_obj->handle));
    return 1;
}

static int sqlite_database_total_changes(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite:total_changes() expects 0 arguments");
    DatabaseObject* db_obj = get_database_object(state, 0, "sqlite:total_changes() self is not a database");
    if (!db_obj) return -1;
    mobius_stack_pop(state, 1);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (db_obj->closed || !db_obj->handle) return mobius_error(state, "sqlite database is closed");
    mobius_stack_pushInt64(state, (int64_t)sqlite3_total_changes64(db_obj->handle));
    return 1;
}

static int sqlite_database_last_insert_rowid(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite:last_insert_rowid() expects 0 arguments");
    DatabaseObject* db_obj = get_database_object(state, 0, "sqlite:last_insert_rowid() self is not a database");
    if (!db_obj) return -1;
    mobius_stack_pop(state, 1);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (db_obj->closed || !db_obj->handle) return mobius_error(state, "sqlite database is closed");
    mobius_stack_pushInt64(state, (int64_t)sqlite3_last_insert_rowid(db_obj->handle));
    return 1;
}

static int sqlite_database_error_code(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite:error_code() expects 0 arguments");
    DatabaseObject* db_obj = get_database_object(state, 0, "sqlite:error_code() self is not a database");
    if (!db_obj) return -1;
    mobius_stack_pop(state, 1);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (db_obj->closed || !db_obj->handle) return mobius_error(state, "sqlite database is closed");
    mobius_stack_pushInt64(state, sqlite3_errcode(db_obj->handle));
    return 1;
}

static int sqlite_database_extended_error_code(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite:extended_error_code() expects 0 arguments");
    DatabaseObject* db_obj = get_database_object(state, 0, "sqlite:extended_error_code() self is not a database");
    if (!db_obj) return -1;
    mobius_stack_pop(state, 1);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (db_obj->closed || !db_obj->handle) return mobius_error(state, "sqlite database is closed");
    mobius_stack_pushInt64(state, sqlite3_extended_errcode(db_obj->handle));
    return 1;
}

static int sqlite_database_errmsg(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite:errmsg() expects 0 arguments");
    DatabaseObject* db_obj = get_database_object(state, 0, "sqlite:errmsg() self is not a database");
    if (!db_obj) return -1;
    mobius_stack_pop(state, 1);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (db_obj->closed || !db_obj->handle) return mobius_error(state, "sqlite database is closed");
    mobius_stack_pushString(state, sqlite3_errmsg(db_obj->handle));
    return 1;
}

static int sqlite_database_autocommit(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite:autocommit() expects 0 arguments");
    DatabaseObject* db_obj = get_database_object(state, 0, "sqlite:autocommit() self is not a database");
    if (!db_obj) return -1;
    mobius_stack_pop(state, 1);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (db_obj->closed || !db_obj->handle) return mobius_error(state, "sqlite database is closed");
    mobius_stack_pushBool(state, sqlite3_get_autocommit(db_obj->handle) != 0);
    return 1;
}

static int sqlite_database_metrics(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite:metrics() expects 0 arguments");
    DatabaseObject* db_obj = get_database_object(state, 0, "sqlite:metrics() self is not a database");
    if (!db_obj) return -1;
    mobius_stack_pop(state, 1);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (db_obj->closed || !db_obj->handle) return mobius_error(state, "sqlite database is closed");
    push_database_metrics(state, db_obj->handle);
    return 1;
}

static int sqlite_statement_bind(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "sqlite_stmt:bind() expects 2 arguments");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:bind() self is not a statement");
    if (!stmt_obj) return -1;
    DatabaseObject* db_obj = stmt_obj->owner;
    if (!db_obj) return mobius_error(state, "sqlite statement is finalized");

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (stmt_obj->finalized || !stmt_obj->stmt || !db_obj->handle || db_obj->closed) {
        return mobius_error(state, "sqlite statement is finalized");
    }

    int param_idx = 0;
    int rc = resolve_parameter_index(stmt_obj->stmt, state, 1, &param_idx, "sqlite_stmt:bind()");
    if (rc < 0) return rc;
    rc = bind_stack_value(stmt_obj->stmt, state, param_idx, 2, "sqlite_stmt:bind()");
    if (rc < 0) return rc;

    mobius_stack_pop(state, 3);
    mobius_stack_pushBool(state, true);
    return 1;
}

static int sqlite_statement_bind_null(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "sqlite_stmt:bind_null() expects 1 argument");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:bind_null() self is not a statement");
    if (!stmt_obj) return -1;
    DatabaseObject* db_obj = stmt_obj->owner;
    if (!db_obj) return mobius_error(state, "sqlite statement is finalized");

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (stmt_obj->finalized || !stmt_obj->stmt || !db_obj->handle || db_obj->closed) {
        return mobius_error(state, "sqlite statement is finalized");
    }

    int param_idx = 0;
    int rc = resolve_parameter_index(stmt_obj->stmt, state, 1, &param_idx, "sqlite_stmt:bind_null()");
    if (rc < 0) return rc;
    rc = sqlite3_bind_null(stmt_obj->stmt, param_idx);
    if (rc != SQLITE_OK) return mobius_error(state, sqlite3_errmsg(db_obj->handle));

    mobius_stack_pop(state, 2);
    mobius_stack_pushBool(state, true);
    return 1;
}

static int sqlite_statement_bind_int(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "sqlite_stmt:bind_int() expects 2 arguments");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:bind_int() self is not a statement");
    if (!stmt_obj) return -1;
    DatabaseObject* db_obj = stmt_obj->owner;
    if (!db_obj) return mobius_error(state, "sqlite statement is finalized");

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (stmt_obj->finalized || !stmt_obj->stmt || !db_obj->handle || db_obj->closed) {
        return mobius_error(state, "sqlite statement is finalized");
    }

    int param_idx = 0;
    int rc = resolve_parameter_index(stmt_obj->stmt, state, 1, &param_idx, "sqlite_stmt:bind_int()");
    if (rc < 0) return rc;
    rc = sqlite3_bind_int64(stmt_obj->stmt, param_idx, (sqlite3_int64)mobius_stack_asInt64(state, 2));
    if (rc != SQLITE_OK) return mobius_error(state, sqlite3_errmsg(db_obj->handle));

    mobius_stack_pop(state, 3);
    mobius_stack_pushBool(state, true);
    return 1;
}

static int sqlite_statement_bind_float(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "sqlite_stmt:bind_float() expects 2 arguments");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:bind_float() self is not a statement");
    if (!stmt_obj) return -1;
    DatabaseObject* db_obj = stmt_obj->owner;
    if (!db_obj) return mobius_error(state, "sqlite statement is finalized");

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (stmt_obj->finalized || !stmt_obj->stmt || !db_obj->handle || db_obj->closed) {
        return mobius_error(state, "sqlite statement is finalized");
    }

    int param_idx = 0;
    int rc = resolve_parameter_index(stmt_obj->stmt, state, 1, &param_idx, "sqlite_stmt:bind_float()");
    if (rc < 0) return rc;
    rc = sqlite3_bind_double(stmt_obj->stmt, param_idx, mobius_stack_asFloat64(state, 2));
    if (rc != SQLITE_OK) return mobius_error(state, sqlite3_errmsg(db_obj->handle));

    mobius_stack_pop(state, 3);
    mobius_stack_pushBool(state, true);
    return 1;
}

static int sqlite_statement_bind_text(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "sqlite_stmt:bind_text() expects 2 arguments");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:bind_text() self is not a statement");
    if (!stmt_obj) return -1;
    DatabaseObject* db_obj = stmt_obj->owner;
    if (!db_obj) return mobius_error(state, "sqlite statement is finalized");
    if (!mobius_stack_isString(state, 2)) return mobius_error(state, "sqlite_stmt:bind_text() expects a string value");

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (stmt_obj->finalized || !stmt_obj->stmt || !db_obj->handle || db_obj->closed) {
        return mobius_error(state, "sqlite statement is finalized");
    }

    int param_idx = 0;
    int rc = resolve_parameter_index(stmt_obj->stmt, state, 1, &param_idx, "sqlite_stmt:bind_text()");
    if (rc < 0) return rc;

    size_t len = 0;
    const char* text = mobius_stack_getStringData(state, 2, &len);
    rc = sqlite3_bind_text(stmt_obj->stmt, param_idx, text ? text : "", (int)len, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) return mobius_error(state, sqlite3_errmsg(db_obj->handle));

    mobius_stack_pop(state, 3);
    mobius_stack_pushBool(state, true);
    return 1;
}

static int sqlite_statement_bind_blob(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "sqlite_stmt:bind_blob() expects 2 arguments");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:bind_blob() self is not a statement");
    if (!stmt_obj) return -1;
    DatabaseObject* db_obj = stmt_obj->owner;
    if (!db_obj) return mobius_error(state, "sqlite statement is finalized");

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (stmt_obj->finalized || !stmt_obj->stmt || !db_obj->handle || db_obj->closed) {
        return mobius_error(state, "sqlite statement is finalized");
    }

    int param_idx = 0;
    int rc = resolve_parameter_index(stmt_obj->stmt, state, 1, &param_idx, "sqlite_stmt:bind_blob()");
    if (rc < 0) return rc;

    if (mobius_stack_isBuffer(state, 2)) {
        size_t len = 0;
        void* data = mobius_stack_getBufferData(state, 2, &len);
        rc = sqlite3_bind_blob(stmt_obj->stmt, param_idx, data, (int)len, SQLITE_TRANSIENT);
    } else if (mobius_stack_isString(state, 2)) {
        size_t len = 0;
        const char* data = mobius_stack_getStringData(state, 2, &len);
        rc = sqlite3_bind_blob(stmt_obj->stmt, param_idx, data ? data : "", (int)len, SQLITE_TRANSIENT);
    } else {
        return mobius_error(state, "sqlite_stmt:bind_blob() expects a buffer or string value");
    }

    if (rc != SQLITE_OK) return mobius_error(state, sqlite3_errmsg(db_obj->handle));

    mobius_stack_pop(state, 3);
    mobius_stack_pushBool(state, true);
    return 1;
}

static int sqlite_statement_bind_bool(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "sqlite_stmt:bind_bool() expects 2 arguments");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:bind_bool() self is not a statement");
    if (!stmt_obj) return -1;
    DatabaseObject* db_obj = stmt_obj->owner;
    if (!db_obj) return mobius_error(state, "sqlite statement is finalized");

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (stmt_obj->finalized || !stmt_obj->stmt || !db_obj->handle || db_obj->closed) {
        return mobius_error(state, "sqlite statement is finalized");
    }

    int param_idx = 0;
    int rc = resolve_parameter_index(stmt_obj->stmt, state, 1, &param_idx, "sqlite_stmt:bind_bool()");
    if (rc < 0) return rc;
    rc = sqlite3_bind_int64(stmt_obj->stmt, param_idx, mobius_stack_asBool(state, 2) ? 1 : 0);
    if (rc != SQLITE_OK) return mobius_error(state, sqlite3_errmsg(db_obj->handle));

    mobius_stack_pop(state, 3);
    mobius_stack_pushBool(state, true);
    return 1;
}

static int sqlite_statement_step(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite_stmt:step() expects 0 arguments");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:step() self is not a statement");
    if (!stmt_obj) return -1;
    DatabaseObject* db_obj = stmt_obj->owner;
    if (!db_obj) return mobius_error(state, "sqlite statement is finalized");
    mobius_stack_pop(state, 1);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (stmt_obj->finalized || !stmt_obj->stmt || !db_obj->handle || db_obj->closed) {
        return mobius_error(state, "sqlite statement is finalized");
    }

    int rc = sqlite3_step(stmt_obj->stmt);
    if (rc == SQLITE_ROW) {
        stmt_obj->has_row = true;
        push_current_row(state, stmt_obj->stmt);
        return 1;
    }
    stmt_obj->has_row = false;
    if (rc == SQLITE_DONE) {
        mobius_stack_pushNil(state);
        return 1;
    }
    return mobius_error(state, sqlite3_errmsg(db_obj->handle));
}

static int sqlite_statement_reset(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite_stmt:reset() expects 0 arguments");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:reset() self is not a statement");
    if (!stmt_obj) return -1;
    DatabaseObject* db_obj = stmt_obj->owner;
    if (!db_obj) return mobius_error(state, "sqlite statement is finalized");
    mobius_stack_pop(state, 1);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (stmt_obj->finalized || !stmt_obj->stmt || !db_obj->handle || db_obj->closed) {
        return mobius_error(state, "sqlite statement is finalized");
    }

    int rc = sqlite3_reset(stmt_obj->stmt);
    stmt_obj->has_row = false;
    if (rc != SQLITE_OK) return mobius_error(state, sqlite3_errmsg(db_obj->handle));
    mobius_stack_pushBool(state, true);
    return 1;
}

static int sqlite_statement_clear_bindings(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite_stmt:clear_bindings() expects 0 arguments");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:clear_bindings() self is not a statement");
    if (!stmt_obj) return -1;
    DatabaseObject* db_obj = stmt_obj->owner;
    if (!db_obj) return mobius_error(state, "sqlite statement is finalized");
    mobius_stack_pop(state, 1);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (stmt_obj->finalized || !stmt_obj->stmt || !db_obj->handle || db_obj->closed) {
        return mobius_error(state, "sqlite statement is finalized");
    }

    int rc = sqlite3_clear_bindings(stmt_obj->stmt);
    if (rc != SQLITE_OK) return mobius_error(state, sqlite3_errmsg(db_obj->handle));
    mobius_stack_pushBool(state, true);
    return 1;
}

static int sqlite_statement_finalize(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite_stmt:finalize() expects 0 arguments");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:finalize() self is not a statement");
    if (!stmt_obj) return -1;
    DatabaseObject* db_obj = stmt_obj->owner;
    mobius_stack_pop(state, 1);

    if (db_obj) {
        std::lock_guard<std::mutex> lock(db_obj->mutex);
        finalize_statement_locked(stmt_obj);
    } else if (stmt_obj->stmt) {
        sqlite3_finalize(stmt_obj->stmt);
        stmt_obj->stmt = nullptr;
        stmt_obj->has_row = false;
        stmt_obj->finalized = true;
    }

    mobius_stack_pushBool(state, true);
    return 1;
}

static int sqlite_statement_is_finalized(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite_stmt:is_finalized() expects 0 arguments");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:is_finalized() self is not a statement");
    if (!stmt_obj) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, stmt_obj->finalized || !stmt_obj->stmt || !stmt_obj->owner);
    return 1;
}

static int sqlite_statement_parameter_count(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite_stmt:parameter_count() expects 0 arguments");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:parameter_count() self is not a statement");
    if (!stmt_obj) return -1;
    DatabaseObject* db_obj = stmt_obj->owner;
    if (!db_obj) return mobius_error(state, "sqlite statement is finalized");
    mobius_stack_pop(state, 1);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (stmt_obj->finalized || !stmt_obj->stmt || !db_obj->handle || db_obj->closed) return mobius_error(state, "sqlite statement is finalized");
    mobius_stack_pushInt64(state, sqlite3_bind_parameter_count(stmt_obj->stmt));
    return 1;
}

static int sqlite_statement_parameter_name(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "sqlite_stmt:parameter_name() expects 1 argument");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:parameter_name() self is not a statement");
    if (!stmt_obj) return -1;
    DatabaseObject* db_obj = stmt_obj->owner;
    if (!db_obj) return mobius_error(state, "sqlite statement is finalized");
    int64_t index = mobius_stack_asInt64(state, 1);
    mobius_stack_pop(state, 2);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (stmt_obj->finalized || !stmt_obj->stmt || !db_obj->handle || db_obj->closed) return mobius_error(state, "sqlite statement is finalized");
    if (index < 1 || index > sqlite3_bind_parameter_count(stmt_obj->stmt)) return mobius_error(state, "sqlite_stmt:parameter_name() index out of range");
    const char* name = sqlite3_bind_parameter_name(stmt_obj->stmt, (int)index);
    if (name) mobius_stack_pushString(state, name);
    else mobius_stack_pushNil(state);
    return 1;
}

static int sqlite_statement_column_count(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite_stmt:column_count() expects 0 arguments");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:column_count() self is not a statement");
    if (!stmt_obj) return -1;
    DatabaseObject* db_obj = stmt_obj->owner;
    if (!db_obj) return mobius_error(state, "sqlite statement is finalized");
    mobius_stack_pop(state, 1);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (stmt_obj->finalized || !stmt_obj->stmt || !db_obj->handle || db_obj->closed) return mobius_error(state, "sqlite statement is finalized");
    mobius_stack_pushInt64(state, sqlite3_column_count(stmt_obj->stmt));
    return 1;
}

static int sqlite_statement_column_name(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "sqlite_stmt:column_name() expects 1 argument");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:column_name() self is not a statement");
    if (!stmt_obj) return -1;
    DatabaseObject* db_obj = stmt_obj->owner;
    if (!db_obj) return mobius_error(state, "sqlite statement is finalized");
    int64_t index = mobius_stack_asInt64(state, 1);
    mobius_stack_pop(state, 2);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (stmt_obj->finalized || !stmt_obj->stmt || !db_obj->handle || db_obj->closed) return mobius_error(state, "sqlite statement is finalized");
    int count = sqlite3_column_count(stmt_obj->stmt);
    if (index < 0 || index >= count) return mobius_error(state, "sqlite_stmt:column_name() index out of range");
    const char* name = sqlite3_column_name(stmt_obj->stmt, (int)index);
    mobius_stack_pushString(state, name ? name : "");
    return 1;
}

static int sqlite_statement_column_type(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "sqlite_stmt:column_type() expects 1 argument");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:column_type() self is not a statement");
    if (!stmt_obj) return -1;
    DatabaseObject* db_obj = stmt_obj->owner;
    if (!db_obj) return mobius_error(state, "sqlite statement is finalized");
    int64_t index = mobius_stack_asInt64(state, 1);
    mobius_stack_pop(state, 2);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (stmt_obj->finalized || !stmt_obj->stmt || !db_obj->handle || db_obj->closed) return mobius_error(state, "sqlite statement is finalized");
    int count = sqlite3_column_count(stmt_obj->stmt);
    if (index < 0 || index >= count) return mobius_error(state, "sqlite_stmt:column_type() index out of range");
    if (!stmt_obj->has_row) return mobius_error(state, "sqlite_stmt:column_type() requires a current row");
    mobius_stack_pushString(state, column_type_name(sqlite3_column_type(stmt_obj->stmt, (int)index)));
    return 1;
}

static int sqlite_statement_column_value(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "sqlite_stmt:column_value() expects 1 argument");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:column_value() self is not a statement");
    if (!stmt_obj) return -1;
    DatabaseObject* db_obj = stmt_obj->owner;
    if (!db_obj) return mobius_error(state, "sqlite statement is finalized");
    int64_t index = mobius_stack_asInt64(state, 1);
    mobius_stack_pop(state, 2);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (stmt_obj->finalized || !stmt_obj->stmt || !db_obj->handle || db_obj->closed) return mobius_error(state, "sqlite statement is finalized");
    int count = sqlite3_column_count(stmt_obj->stmt);
    if (index < 0 || index >= count) return mobius_error(state, "sqlite_stmt:column_value() index out of range");
    if (!stmt_obj->has_row) return mobius_error(state, "sqlite_stmt:column_value() requires a current row");
    push_column_value(state, stmt_obj->stmt, (int)index);
    return 1;
}

static int sqlite_statement_columns(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite_stmt:columns() expects 0 arguments");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:columns() self is not a statement");
    if (!stmt_obj) return -1;
    DatabaseObject* db_obj = stmt_obj->owner;
    if (!db_obj) return mobius_error(state, "sqlite statement is finalized");
    mobius_stack_pop(state, 1);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (stmt_obj->finalized || !stmt_obj->stmt || !db_obj->handle || db_obj->closed) return mobius_error(state, "sqlite statement is finalized");

    int count = sqlite3_column_count(stmt_obj->stmt);
    mobius_stack_pushNewArray(state, (size_t)count);
    int arr = mobius_stack_size(state) - 1;
    for (int i = 0; i < count; i++) {
        const char* name = sqlite3_column_name(stmt_obj->stmt, i);
        mobius_stack_pushString(state, name ? name : "");
        mobius_stack_arrayPush(state, arr);
    }
    return 1;
}

static int sqlite_statement_read_only(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite_stmt:read_only() expects 0 arguments");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:read_only() self is not a statement");
    if (!stmt_obj) return -1;
    DatabaseObject* db_obj = stmt_obj->owner;
    if (!db_obj) return mobius_error(state, "sqlite statement is finalized");
    mobius_stack_pop(state, 1);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (stmt_obj->finalized || !stmt_obj->stmt || !db_obj->handle || db_obj->closed) return mobius_error(state, "sqlite statement is finalized");
    mobius_stack_pushBool(state, sqlite3_stmt_readonly(stmt_obj->stmt) != 0);
    return 1;
}

static int sqlite_statement_sql(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite_stmt:sql() expects 0 arguments");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:sql() self is not a statement");
    if (!stmt_obj) return -1;
    DatabaseObject* db_obj = stmt_obj->owner;
    if (!db_obj) return mobius_error(state, "sqlite statement is finalized");
    mobius_stack_pop(state, 1);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (stmt_obj->finalized || !stmt_obj->stmt || !db_obj->handle || db_obj->closed) return mobius_error(state, "sqlite statement is finalized");
    const char* sql = sqlite3_sql(stmt_obj->stmt);
    mobius_stack_pushString(state, sql ? sql : "");
    return 1;
}

static int sqlite_statement_expanded_sql(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite_stmt:expanded_sql() expects 0 arguments");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:expanded_sql() self is not a statement");
    if (!stmt_obj) return -1;
    DatabaseObject* db_obj = stmt_obj->owner;
    if (!db_obj) return mobius_error(state, "sqlite statement is finalized");
    mobius_stack_pop(state, 1);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (stmt_obj->finalized || !stmt_obj->stmt || !db_obj->handle || db_obj->closed) return mobius_error(state, "sqlite statement is finalized");
    char* expanded = sqlite3_expanded_sql(stmt_obj->stmt);
    if (!expanded) {
        mobius_stack_pushNil(state);
        return 1;
    }
    mobius_stack_pushString(state, expanded);
    sqlite3_free(expanded);
    return 1;
}

static int sqlite_statement_metrics(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite_stmt:metrics() expects 0 arguments");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:metrics() self is not a statement");
    if (!stmt_obj) return -1;
    DatabaseObject* db_obj = stmt_obj->owner;
    if (!db_obj) return mobius_error(state, "sqlite statement is finalized");
    mobius_stack_pop(state, 1);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (stmt_obj->finalized || !stmt_obj->stmt || !db_obj->handle || db_obj->closed) return mobius_error(state, "sqlite statement is finalized");
    push_database_metrics(state, db_obj->handle);
    return 1;
}

static int resolve_parameter_name(sqlite3_stmt* stmt, const std::string& name, int* out_index, const char* context) {
    int resolved = sqlite3_bind_parameter_index(stmt, name.c_str());
    if (resolved <= 0) return -1;
    *out_index = resolved;
    return 0;
}

static int bind_params_collection(MobiusState* state, sqlite3_stmt* stmt, int params_idx, const char* context) {
    if (mobius_stack_isNil(state, params_idx)) return 0;

    if (mobius_stack_isArray(state, params_idx)) {
        size_t len = mobius_stack_getArrayLength(state, params_idx);
        for (size_t i = 0; i < len; i++) {
            mobius_stack_getArrayElement(state, params_idx, i);
            int rc = bind_stack_value(stmt, state, (int)i + 1, -1, context);
            mobius_stack_pop(state, 1);
            if (rc < 0) return rc;
        }
        return 0;
    }

    if (mobius_stack_isTable(state, params_idx)) {
        mobius_stack_getTableKeys(state, params_idx);
        int keys_idx = mobius_stack_size(state) - 1;
        size_t len = mobius_stack_getArrayLength(state, keys_idx);
        for (size_t i = 0; i < len; i++) {
            mobius_stack_getArrayElement(state, keys_idx, i);
            if (!mobius_stack_isString(state, -1)) {
                mobius_stack_pop(state, 2);
                return mobius_error(state, (std::string(context) + " named params table must use string keys").c_str());
            }
            std::string key = mobius_stack_asString(state, -1);
            mobius_stack_pop(state, 1);

            int param_idx = 0;
            if (resolve_parameter_name(stmt, key, &param_idx, context) != 0) {
                mobius_stack_pop(state, 1);
                return mobius_error(state, (std::string(context) + " unknown named parameter").c_str());
            }

            mobius_stack_getTableField(state, params_idx, key.c_str());
            int rc = bind_stack_value(stmt, state, param_idx, -1, context);
            mobius_stack_pop(state, 1);
            if (rc < 0) {
                mobius_stack_pop(state, 1);
                return rc;
            }
        }
        mobius_stack_pop(state, 1);
        return 0;
    }

    return mobius_error(state, (std::string(context) + " params must be an array, table, or nil").c_str());
}

static int sqlite_statement_bind_all(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "sqlite_stmt:bind_all() expects 1 argument");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:bind_all() self is not a statement");
    if (!stmt_obj) return -1;
    DatabaseObject* db_obj = stmt_obj->owner;
    if (!db_obj) return mobius_error(state, "sqlite statement is finalized");

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (stmt_obj->finalized || !stmt_obj->stmt || !db_obj->handle || db_obj->closed) {
        return mobius_error(state, "sqlite statement is finalized");
    }
    int rc = bind_params_collection(state, stmt_obj->stmt, 1, "sqlite_stmt:bind_all()");
    if (rc < 0) return rc;

    mobius_stack_pop(state, 2);
    mobius_stack_pushBool(state, true);
    return 1;
}

static int sqlite_statement_all(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite_stmt:all() expects 0 arguments");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:all() self is not a statement");
    if (!stmt_obj) return -1;
    DatabaseObject* db_obj = stmt_obj->owner;
    if (!db_obj) return mobius_error(state, "sqlite statement is finalized");
    mobius_stack_pop(state, 1);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (stmt_obj->finalized || !stmt_obj->stmt || !db_obj->handle || db_obj->closed) return mobius_error(state, "sqlite statement is finalized");

    mobius_stack_pushNewArray(state, 8);
    int arr_idx = mobius_stack_size(state) - 1;
    while (true) {
        int rc = sqlite3_step(stmt_obj->stmt);
        if (rc == SQLITE_ROW) {
            stmt_obj->has_row = true;
            push_current_row(state, stmt_obj->stmt);
            mobius_stack_arrayPush(state, arr_idx);
            continue;
        }
        stmt_obj->has_row = false;
        if (rc == SQLITE_DONE) return 1;
        return mobius_error(state, sqlite3_errmsg(db_obj->handle));
    }
}

static int sqlite_statement_one(MobiusState* state, int arg_count) {
    return sqlite_statement_step(state, arg_count);
}

static int sqlite_statement_run(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "sqlite_stmt:run() expects 0 arguments");
    StatementObject* stmt_obj = get_statement_object(state, 0, "sqlite_stmt:run() self is not a statement");
    if (!stmt_obj) return -1;
    DatabaseObject* db_obj = stmt_obj->owner;
    if (!db_obj) return mobius_error(state, "sqlite statement is finalized");
    mobius_stack_pop(state, 1);

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (stmt_obj->finalized || !stmt_obj->stmt || !db_obj->handle || db_obj->closed) return mobius_error(state, "sqlite statement is finalized");

    int rc = sqlite3_step(stmt_obj->stmt);
    stmt_obj->has_row = false;
    if (rc == SQLITE_ROW) return mobius_error(state, "sqlite statement produced rows; use all() or one() instead");
    if (rc != SQLITE_DONE) return mobius_error(state, sqlite3_errmsg(db_obj->handle));
    push_database_metrics(state, db_obj->handle);
    return 1;
}

static int sqlite_database_exec(MobiusState* state, int arg_count) {
    if (arg_count != 2 && arg_count != 3) return mobius_error(state, "sqlite:exec() expects 1 or 2 arguments");
    DatabaseObject* db_obj = get_database_object(state, 0, "sqlite:exec() self is not a database");
    if (!db_obj) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "sqlite:exec() expects a SQL string");

    const char* sql = mobius_stack_asString(state, 1);
    sqlite3_stmt* stmt = nullptr;
    {
        std::lock_guard<std::mutex> lock(db_obj->mutex);
        if (db_obj->closed || !db_obj->handle) return mobius_error(state, "sqlite database is closed");
        int rc = sqlite3_prepare_v2(db_obj->handle, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return mobius_error(state, sqlite3_errmsg(db_obj->handle));
        if (arg_count == 3) {
            rc = bind_params_collection(state, stmt, 2, "sqlite:exec()");
            if (rc < 0) {
                sqlite3_finalize(stmt);
                return rc;
            }
        }
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            sqlite3_finalize(stmt);
            return mobius_error(state, "sqlite statement produced rows; use query() instead");
        }
        if (rc != SQLITE_DONE) {
            std::string error = sqlite3_errmsg(db_obj->handle);
            sqlite3_finalize(stmt);
            return mobius_error(state, error.c_str());
        }
        sqlite3_finalize(stmt);
        stmt = nullptr;
        mobius_stack_pop(state, arg_count);
        push_database_metrics(state, db_obj->handle);
        return 1;
    }
}

static int sqlite_database_query_all(MobiusState* state, int arg_count) {
    if (arg_count != 2 && arg_count != 3) return mobius_error(state, "sqlite:query() expects 1 or 2 arguments");
    DatabaseObject* db_obj = get_database_object(state, 0, "sqlite:query() self is not a database");
    if (!db_obj) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "sqlite:query() expects a SQL string");

    const char* sql = mobius_stack_asString(state, 1);
    sqlite3_stmt* stmt = nullptr;

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (db_obj->closed || !db_obj->handle) return mobius_error(state, "sqlite database is closed");
    int rc = sqlite3_prepare_v2(db_obj->handle, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return mobius_error(state, sqlite3_errmsg(db_obj->handle));
    if (arg_count == 3) {
        rc = bind_params_collection(state, stmt, 2, "sqlite:query()");
        if (rc < 0) {
            sqlite3_finalize(stmt);
            return rc;
        }
    }

    mobius_stack_pop(state, arg_count);
    mobius_stack_pushNewArray(state, 8);
    int arr_idx = mobius_stack_size(state) - 1;
    while (true) {
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            push_current_row(state, stmt);
            mobius_stack_arrayPush(state, arr_idx);
            continue;
        }
        if (rc == SQLITE_DONE) {
            sqlite3_finalize(stmt);
            return 1;
        }
        std::string error = sqlite3_errmsg(db_obj->handle);
        sqlite3_finalize(stmt);
        return mobius_error(state, error.c_str());
    }
}

static int sqlite_database_query_one(MobiusState* state, int arg_count) {
    if (arg_count != 2 && arg_count != 3) return mobius_error(state, "sqlite:query_one() expects 1 or 2 arguments");
    DatabaseObject* db_obj = get_database_object(state, 0, "sqlite:query_one() self is not a database");
    if (!db_obj) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "sqlite:query_one() expects a SQL string");

    const char* sql = mobius_stack_asString(state, 1);
    sqlite3_stmt* stmt = nullptr;

    std::lock_guard<std::mutex> lock(db_obj->mutex);
    if (db_obj->closed || !db_obj->handle) return mobius_error(state, "sqlite database is closed");
    int rc = sqlite3_prepare_v2(db_obj->handle, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return mobius_error(state, sqlite3_errmsg(db_obj->handle));
    if (arg_count == 3) {
        rc = bind_params_collection(state, stmt, 2, "sqlite:query_one()");
        if (rc < 0) {
            sqlite3_finalize(stmt);
            return rc;
        }
    }

    mobius_stack_pop(state, arg_count);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        push_current_row(state, stmt);
        sqlite3_finalize(stmt);
        return 1;
    }
    if (rc == SQLITE_DONE) {
        sqlite3_finalize(stmt);
        mobius_stack_pushNil(state);
        return 1;
    }
    std::string error = sqlite3_errmsg(db_obj->handle);
    sqlite3_finalize(stmt);
    return mobius_error(state, error.c_str());
}

static int sqlite_database_transaction(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "sqlite:transaction() expects 1 argument");
    DatabaseObject* db_obj = get_database_object(state, 0, "sqlite:transaction() self is not a database");
    if (!db_obj) return -1;
    if (!is_callable_type(mobius_stack_type(state, 1))) return mobius_error(state, "sqlite:transaction() expects a function");

    {
        std::lock_guard<std::mutex> lock(db_obj->mutex);
        if (db_obj->closed || !db_obj->handle) return mobius_error(state, "sqlite database is closed");
        char* errmsg = nullptr;
        int rc = sqlite3_exec(db_obj->handle, "BEGIN", nullptr, nullptr, &errmsg);
        if (rc != SQLITE_OK) {
            std::string error = errmsg ? errmsg : sqlite3_errmsg(db_obj->handle);
            if (errmsg) sqlite3_free(errmsg);
            return mobius_error(state, error.c_str());
        }
    }

    MobiusValueRef callback_ref = mobius_ref_value(state, 1);
    MobiusValueRef self_ref = mobius_ref_value(state, 0);
    mobius_stack_pop(state, 2);
    int call_rc = mobius_call_ref(state, callback_ref, &self_ref, 1, 1);
    mobius_unref_value(state, self_ref);
    mobius_unref_value(state, callback_ref);
    if (call_rc < 0) {
        std::lock_guard<std::mutex> lock(db_obj->mutex);
        if (!db_obj->closed && db_obj->handle) {
            sqlite3_exec(db_obj->handle, "ROLLBACK", nullptr, nullptr, nullptr);
        }
        return -1;
    }

    {
        std::lock_guard<std::mutex> lock(db_obj->mutex);
        if (db_obj->closed || !db_obj->handle) return mobius_error(state, "sqlite database is closed");
        char* errmsg = nullptr;
        int rc = sqlite3_exec(db_obj->handle, "COMMIT", nullptr, nullptr, &errmsg);
        if (rc != SQLITE_OK) {
            std::string error = errmsg ? errmsg : sqlite3_errmsg(db_obj->handle);
            if (errmsg) sqlite3_free(errmsg);
            return mobius_error(state, error.c_str());
        }
    }

    return 1;
}

static void copy_module_function(MobiusState* state, int module_idx, const char* from_key, int target_idx, const char* to_key) {
    mobius_stack_getTableField(state, module_idx, from_key);
    mobius_stack_setTableField(state, target_idx, to_key);
}

static int sqlite_post_init(MobiusState* state) {
    const int module_idx = 0;

    mobius_stack_pushNewTable(state, 16);
    int db_proto_idx = mobius_stack_size(state) - 1;
    copy_module_function(state, module_idx, "__close", db_proto_idx, "close");
    copy_module_function(state, module_idx, "__is_closed", db_proto_idx, "is_closed");
    copy_module_function(state, module_idx, "__prepare", db_proto_idx, "prepare");
    copy_module_function(state, module_idx, "__db_exec", db_proto_idx, "exec");
    copy_module_function(state, module_idx, "__db_query_all", db_proto_idx, "query");
    copy_module_function(state, module_idx, "__db_query_all", db_proto_idx, "query_all");
    copy_module_function(state, module_idx, "__db_query_one", db_proto_idx, "query_one");
    copy_module_function(state, module_idx, "__db_transaction", db_proto_idx, "transaction");
    copy_module_function(state, module_idx, "__interrupt", db_proto_idx, "interrupt");
    copy_module_function(state, module_idx, "__changes", db_proto_idx, "changes");
    copy_module_function(state, module_idx, "__total_changes", db_proto_idx, "total_changes");
    copy_module_function(state, module_idx, "__last_insert_rowid", db_proto_idx, "last_insert_rowid");
    copy_module_function(state, module_idx, "__error_code", db_proto_idx, "error_code");
    copy_module_function(state, module_idx, "__extended_error_code", db_proto_idx, "extended_error_code");
    copy_module_function(state, module_idx, "__errmsg", db_proto_idx, "errmsg");
    copy_module_function(state, module_idx, "__autocommit", db_proto_idx, "autocommit");
    copy_module_function(state, module_idx, "__metrics", db_proto_idx, "metrics");
    mobius_set_userdata_type_metatable(state, SQLITE_DATABASE_TYPE);

    mobius_stack_pushNewTable(state, 24);
    int stmt_proto_idx = mobius_stack_size(state) - 1;
    copy_module_function(state, module_idx, "__stmt_bind", stmt_proto_idx, "bind");
    copy_module_function(state, module_idx, "__stmt_bind_all", stmt_proto_idx, "bind_all");
    copy_module_function(state, module_idx, "__stmt_bind_null", stmt_proto_idx, "bind_null");
    copy_module_function(state, module_idx, "__stmt_bind_int", stmt_proto_idx, "bind_int");
    copy_module_function(state, module_idx, "__stmt_bind_float", stmt_proto_idx, "bind_float");
    copy_module_function(state, module_idx, "__stmt_bind_text", stmt_proto_idx, "bind_text");
    copy_module_function(state, module_idx, "__stmt_bind_blob", stmt_proto_idx, "bind_blob");
    copy_module_function(state, module_idx, "__stmt_bind_bool", stmt_proto_idx, "bind_bool");
    copy_module_function(state, module_idx, "__stmt_step", stmt_proto_idx, "step");
    copy_module_function(state, module_idx, "__stmt_all", stmt_proto_idx, "all");
    copy_module_function(state, module_idx, "__stmt_one", stmt_proto_idx, "one");
    copy_module_function(state, module_idx, "__stmt_run", stmt_proto_idx, "run");
    copy_module_function(state, module_idx, "__stmt_reset", stmt_proto_idx, "reset");
    copy_module_function(state, module_idx, "__stmt_clear_bindings", stmt_proto_idx, "clear_bindings");
    copy_module_function(state, module_idx, "__stmt_finalize", stmt_proto_idx, "finalize");
    copy_module_function(state, module_idx, "__stmt_is_finalized", stmt_proto_idx, "is_finalized");
    copy_module_function(state, module_idx, "__stmt_parameter_count", stmt_proto_idx, "parameter_count");
    copy_module_function(state, module_idx, "__stmt_parameter_name", stmt_proto_idx, "parameter_name");
    copy_module_function(state, module_idx, "__stmt_column_count", stmt_proto_idx, "column_count");
    copy_module_function(state, module_idx, "__stmt_column_name", stmt_proto_idx, "column_name");
    copy_module_function(state, module_idx, "__stmt_column_type", stmt_proto_idx, "column_type");
    copy_module_function(state, module_idx, "__stmt_column_value", stmt_proto_idx, "column_value");
    copy_module_function(state, module_idx, "__stmt_columns", stmt_proto_idx, "columns");
    copy_module_function(state, module_idx, "__stmt_read_only", stmt_proto_idx, "read_only");
    copy_module_function(state, module_idx, "__stmt_sql", stmt_proto_idx, "sql");
    copy_module_function(state, module_idx, "__stmt_expanded_sql", stmt_proto_idx, "expanded_sql");
    copy_module_function(state, module_idx, "__stmt_metrics", stmt_proto_idx, "metrics");
    mobius_set_userdata_type_metatable(state, SQLITE_STATEMENT_TYPE);

    const char* hidden_keys[] = {
        "__close", "__is_closed", "__prepare", "__interrupt", "__changes", "__total_changes",
        "__db_exec", "__db_query_all", "__db_query_one", "__db_transaction",
        "__last_insert_rowid", "__error_code", "__extended_error_code", "__errmsg",
        "__autocommit", "__metrics", "__stmt_bind", "__stmt_bind_null", "__stmt_bind_int",
        "__stmt_bind_all", "__stmt_bind_float", "__stmt_bind_text", "__stmt_bind_blob", "__stmt_bind_bool",
        "__stmt_step", "__stmt_all", "__stmt_one", "__stmt_run", "__stmt_reset", "__stmt_clear_bindings", "__stmt_finalize",
        "__stmt_is_finalized", "__stmt_parameter_count", "__stmt_parameter_name",
        "__stmt_column_count", "__stmt_column_name", "__stmt_column_type",
        "__stmt_column_value", "__stmt_columns", "__stmt_read_only", "__stmt_sql",
        "__stmt_expanded_sql", "__stmt_metrics", "__install_helpers"
    };

    for (const char* key : hidden_keys) {
        mobius_stack_pushNil(state);
        mobius_stack_setTableField(state, module_idx, key);
    }
    return 0;
}

static MobiusPluginFunction sqlite_functions[] = {
    {"open", sqlite_open, 1, MOBIUS_VAL_USERDATA, "Open a SQLite database"},
    {"__close", sqlite_database_close, 1, MOBIUS_VAL_BOOL, "Internal database close method"},
    {"__is_closed", sqlite_database_is_closed, 1, MOBIUS_VAL_BOOL, "Internal database state method"},
    {"__prepare", sqlite_database_prepare, 2, MOBIUS_VAL_USERDATA, "Internal database prepare method"},
    {"__db_exec", sqlite_database_exec, SIZE_MAX, MOBIUS_VAL_TABLE, "Internal database exec method"},
    {"__db_query_all", sqlite_database_query_all, SIZE_MAX, MOBIUS_VAL_ARRAY, "Internal database query method"},
    {"__db_query_one", sqlite_database_query_one, SIZE_MAX, MOBIUS_VAL_TABLE, "Internal database query_one method"},
    {"__db_transaction", sqlite_database_transaction, 2, MOBIUS_VAL_UNKNOWN, "Internal database transaction method"},
    {"__interrupt", sqlite_database_interrupt, 1, MOBIUS_VAL_BOOL, "Internal database interrupt method"},
    {"__changes", sqlite_database_changes, 1, MOBIUS_VAL_INT64, "Internal database changes method"},
    {"__total_changes", sqlite_database_total_changes, 1, MOBIUS_VAL_INT64, "Internal database total changes method"},
    {"__last_insert_rowid", sqlite_database_last_insert_rowid, 1, MOBIUS_VAL_INT64, "Internal database rowid method"},
    {"__error_code", sqlite_database_error_code, 1, MOBIUS_VAL_INT64, "Internal database error code method"},
    {"__extended_error_code", sqlite_database_extended_error_code, 1, MOBIUS_VAL_INT64, "Internal database extended error code method"},
    {"__errmsg", sqlite_database_errmsg, 1, MOBIUS_VAL_STRING, "Internal database error message method"},
    {"__autocommit", sqlite_database_autocommit, 1, MOBIUS_VAL_BOOL, "Internal database autocommit method"},
    {"__metrics", sqlite_database_metrics, 1, MOBIUS_VAL_TABLE, "Internal database metrics method"},
    {"__stmt_bind", sqlite_statement_bind, 3, MOBIUS_VAL_BOOL, "Internal statement bind method"},
    {"__stmt_bind_all", sqlite_statement_bind_all, 2, MOBIUS_VAL_BOOL, "Internal statement bind_all method"},
    {"__stmt_bind_null", sqlite_statement_bind_null, 2, MOBIUS_VAL_BOOL, "Internal statement bind_null method"},
    {"__stmt_bind_int", sqlite_statement_bind_int, 3, MOBIUS_VAL_BOOL, "Internal statement bind_int method"},
    {"__stmt_bind_float", sqlite_statement_bind_float, 3, MOBIUS_VAL_BOOL, "Internal statement bind_float method"},
    {"__stmt_bind_text", sqlite_statement_bind_text, 3, MOBIUS_VAL_BOOL, "Internal statement bind_text method"},
    {"__stmt_bind_blob", sqlite_statement_bind_blob, 3, MOBIUS_VAL_BOOL, "Internal statement bind_blob method"},
    {"__stmt_bind_bool", sqlite_statement_bind_bool, 3, MOBIUS_VAL_BOOL, "Internal statement bind_bool method"},
    {"__stmt_step", sqlite_statement_step, 1, MOBIUS_VAL_TABLE, "Internal statement step method"},
    {"__stmt_all", sqlite_statement_all, 1, MOBIUS_VAL_ARRAY, "Internal statement all method"},
    {"__stmt_one", sqlite_statement_one, 1, MOBIUS_VAL_TABLE, "Internal statement one method"},
    {"__stmt_run", sqlite_statement_run, 1, MOBIUS_VAL_TABLE, "Internal statement run method"},
    {"__stmt_reset", sqlite_statement_reset, 1, MOBIUS_VAL_BOOL, "Internal statement reset method"},
    {"__stmt_clear_bindings", sqlite_statement_clear_bindings, 1, MOBIUS_VAL_BOOL, "Internal statement clear_bindings method"},
    {"__stmt_finalize", sqlite_statement_finalize, 1, MOBIUS_VAL_BOOL, "Internal statement finalize method"},
    {"__stmt_is_finalized", sqlite_statement_is_finalized, 1, MOBIUS_VAL_BOOL, "Internal statement finalized state method"},
    {"__stmt_parameter_count", sqlite_statement_parameter_count, 1, MOBIUS_VAL_INT64, "Internal statement parameter_count method"},
    {"__stmt_parameter_name", sqlite_statement_parameter_name, 2, MOBIUS_VAL_STRING, "Internal statement parameter_name method"},
    {"__stmt_column_count", sqlite_statement_column_count, 1, MOBIUS_VAL_INT64, "Internal statement column_count method"},
    {"__stmt_column_name", sqlite_statement_column_name, 2, MOBIUS_VAL_STRING, "Internal statement column_name method"},
    {"__stmt_column_type", sqlite_statement_column_type, 2, MOBIUS_VAL_STRING, "Internal statement column_type method"},
    {"__stmt_column_value", sqlite_statement_column_value, 2, MOBIUS_VAL_UNKNOWN, "Internal statement column_value method"},
    {"__stmt_columns", sqlite_statement_columns, 1, MOBIUS_VAL_ARRAY, "Internal statement columns method"},
    {"__stmt_read_only", sqlite_statement_read_only, 1, MOBIUS_VAL_BOOL, "Internal statement read_only method"},
    {"__stmt_sql", sqlite_statement_sql, 1, MOBIUS_VAL_STRING, "Internal statement sql method"},
    {"__stmt_expanded_sql", sqlite_statement_expanded_sql, 1, MOBIUS_VAL_STRING, "Internal statement expanded_sql method"},
    {"__stmt_metrics", sqlite_statement_metrics, 1, MOBIUS_VAL_TABLE, "Internal statement metrics method"},
};

static MobiusPlugin sqlite_plugin = {
    .metadata = {
        .name = "sqlite",
        .version = "0.2.0",
        .description = "SQLite database bindings for Mobius",
        .author = "Mobius Team",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license = "MIT"
    },
    .functions = sqlite_functions,
    .function_count = sizeof(sqlite_functions) / sizeof(sqlite_functions[0]),
    .init_plugin = nullptr,
    .cleanup_plugin = nullptr,
    .post_init = sqlite_post_init,
};

} // namespace

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &sqlite_plugin;
}
