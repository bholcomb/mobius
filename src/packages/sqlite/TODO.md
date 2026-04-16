# SQLite Package TODO

This file tracks missing SQLite package APIs from the point of view of the
Mobius package surface, not a byte-for-byte mirror of every symbol in
`sqlite3.h`.

Already covered:

- `open()` with fixed `sqlite3_open_v2(..., READWRITE | CREATE | FULLMUTEX, ...)`
- `prepare()`
- `exec()` without row callbacks
- `query()`, `query_all()`, `query_one()`
- `transaction(fn)`
- `interrupt()`
- `changes()`, `total_changes()`, `last_insert_rowid()`
- `error_code()`, `extended_error_code()`, `errmsg()`
- `autocommit()`
- statement binding, stepping, reset/finalize, and column metadata/value access

## Callback Evaluation

Status:

- The callback runtime work from the other packages is partially reusable here.
- `sqlite:transaction(fn)` already proves that `MobiusValueRef` plus
  `mobius_call_ref()` works for a synchronous callback invoked from native code.
- SQLite callbacks are different from GLFW callbacks:
  they fire synchronously during `sqlite3_*` calls on the same thread, not from a
  host event queue.

What still needs attention before exposing SQLite hook/function APIs:

- The current wrapper often holds `DatabaseObject::mutex` across SQLite calls.
- Many SQLite callbacks can fire while that lock is held.
- If a Mobius callback re-enters the same database object, the current locking
  shape can deadlock.
- Before adding callback-heavy APIs, audit and tighten the lock boundaries so we
  do not call back into Mobius while holding the database mutex.
- Store rooted refs for callback function and optional userdata on the database
  object and release them on database close.

Conclusion:

- The callback/runtime work does not need a brand-new VM feature for SQLite.
- It does need a SQLite-specific reentrancy/locking pass before the real
  callback APIs are safe to expose.

## Enum Evaluation

Status:

- The current ergonomic subset does not urgently need enum support.
- If we expand toward the lower-level SQLite C API, the enum/constant work done
  for the other packages should be applied here too.

Recommended script enum namespaces:

- `sqlite.Result`
- `sqlite.ExtendedResult`
- `sqlite.OpenFlags`
- `sqlite.DataType`
- `sqlite.TraceEvent`
- `sqlite.AuthorizerAction`
- `sqlite.AuthorizerDecision`
- `sqlite.CheckpointMode`
- `sqlite.PrepareFlags`
- `sqlite.DBConfig`
- `sqlite.LimitId`

Reason:

- SQLite uses a large number of integer constants and bitflags.
- Once `open_v2`, tracing, authorizer hooks, WAL, prepare flags, and config
  APIs are exposed, raw integers will become hard to use and error-prone.

## High Priority Missing APIs

These are the most useful gaps for real applications and examples.

### Connection / Open Modes

- [ ] `sqlite3_open_v2()` with explicit flags and optional VFS name
- [ ] `sqlite3_open16()`
- [ ] `sqlite3_close_v2()`
- [ ] `sqlite3_extended_result_codes()`
- [ ] `sqlite3_errstr()`
- [ ] `sqlite3_db_filename()`
- [ ] `sqlite3_db_readonly()`
- [ ] `sqlite3_limit()`
- [ ] `sqlite3_db_config()`
- [ ] `sqlite3_enable_load_extension()`
- [ ] `sqlite3_load_extension()`
- [ ] `sqlite3_uri_parameter()`
- [ ] `sqlite3_uri_boolean()`
- [ ] `sqlite3_uri_int64()`

### Prepare / Statement Control

- [ ] `sqlite3_prepare_v3()`
- [ ] `sqlite3_prepare16_v2()`
- [ ] `sqlite3_prepare16_v3()`
- [ ] `sqlite3_normalized_sql()`
- [ ] `sqlite3_stmt_status()`
- [ ] `sqlite3_next_stmt()`
- [ ] `sqlite3_sql()` is already covered
- [ ] `sqlite3_expanded_sql()` is already covered

### Execution Helpers

- [ ] `sqlite3_exec()` callback form
- [ ] `sqlite3_complete()`
- [ ] `sqlite3_complete16()`

### Busy / Progress / Trace / Authorizer Hooks

- [ ] `sqlite3_busy_handler()`
- [ ] `sqlite3_busy_timeout()`
- [ ] `sqlite3_progress_handler()`
- [ ] `sqlite3_trace_v2()`
- [ ] `sqlite3_set_authorizer()`

### User Functions / Aggregates / Collations

- [ ] `sqlite3_create_function()`
- [ ] `sqlite3_create_function_v2()`
- [ ] `sqlite3_create_function16()`
- [ ] aggregate callback support (`xStep`, `xFinal`)
- [ ] window-function callback support if we want full modern parity
- [ ] `sqlite3_create_collation()`
- [ ] `sqlite3_create_collation_v2()`
- [ ] `sqlite3_create_collation16()`

### Transaction / WAL / Change Hooks

- [ ] `sqlite3_commit_hook()`
- [ ] `sqlite3_rollback_hook()`
- [ ] `sqlite3_update_hook()`
- [ ] `sqlite3_wal_hook()`
- [ ] `sqlite3_wal_autocheckpoint()`
- [ ] `sqlite3_wal_checkpoint_v2()`
- [ ] savepoint helpers on top of SQL (`SAVEPOINT`, `RELEASE`, `ROLLBACK TO`)

### Incremental BLOB I/O

- [ ] `sqlite3_blob_open()`
- [ ] `sqlite3_blob_reopen()`
- [ ] `sqlite3_blob_bytes()`
- [ ] `sqlite3_blob_read()`
- [ ] `sqlite3_blob_write()`
- [ ] `sqlite3_blob_close()`

### Backup / Serialize / Deserialize

- [ ] `sqlite3_backup_init()`
- [ ] `sqlite3_backup_step()`
- [ ] `sqlite3_backup_finish()`
- [ ] `sqlite3_backup_remaining()`
- [ ] `sqlite3_backup_pagecount()`
- [ ] `sqlite3_serialize()`
- [ ] `sqlite3_deserialize()`

## Medium Priority Missing APIs

Useful, but not blockers for typical CRUD-style package usage.

### Status / Introspection

- [ ] `sqlite3_db_status()`
- [ ] `sqlite3_status64()`
- [ ] `sqlite3_table_column_metadata()`
- [ ] `sqlite3_file_control()`

### Convenience / Misc

- [ ] `sqlite3_get_table()` / `sqlite3_free_table()`
- [ ] `sqlite3_release_memory()`
- [ ] `sqlite3_soft_heap_limit64()`
- [ ] `sqlite3_hard_heap_limit64()`

## Likely Out Of Scope Or Deferred

These are public SQLite APIs, but probably should not be first-pass package work
unless the goal changes to full header coverage.

- global config APIs (`sqlite3_config()`, logging setup, memory allocators)
- VFS registration and custom VFS authoring
- virtual table module authoring APIs
- session / changeset / patchset APIs
- unlock-notify and shared-cache-global tuning
- deprecated APIs and UTF-16-only duplicates where UTF-8 coverage is enough

## Notes For Implementation

- Prefer Mobius-native ergonomic wrappers where they help, but expose the lower
  level C-shape when examples or real integration work depend on it.
- For callback-based APIs, do not call into Mobius while holding the database
  mutex.
- When low-level flag/result/config APIs are added, install script enums rather
  than shipping raw integer constants.
