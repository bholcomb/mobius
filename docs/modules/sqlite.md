# `sqlite` Module

```mobius
import "sqlite"
```

[← Module reference](index.md)

SQLite bindings for Mobius: open databases, run statements, use prepared
statements with bound parameters, and wrap work in transactions. `sqlite` is a
**package** — it must be installed into a `modules/` root (see
[Modules and Packages](../guide/modules-and-packages.md#packages)) before
`import "sqlite"` will resolve.

Database and statement operations raise an [exception](../guide/error-handling.md)
on SQL or binding errors, so wrap fallible calls in `try`/`catch`.

---

## Opening a database

| Function                  | Returns  | Description                                  |
|---------------------------|----------|----------------------------------------------|
| `sqlite.open(path)`       | database | Open (or create) a database file.            |
| `sqlite.open_memory()`    | database | Open a private in-memory database.           |

Use `":memory:"` as the path for an in-memory database, or any filesystem path
for a persistent one.

The module also provides thin function wrappers that forward to the database
methods below: `sqlite.close(db)`, `sqlite.prepare(db, sql)`,
`sqlite.exec(db, sql, params)`, `sqlite.query(db, sql, params)`,
`sqlite.query_all(db, sql, params)`, `sqlite.query_one(db, sql, params)`, and
`sqlite.transaction(db, fn)`. The method forms shown below are equivalent and
usually read better.

---

## Database methods

| Method                        | Description                                                    |
|-------------------------------|----------------------------------------------------------------|
| `db:exec(sql [, params])`     | Execute a statement; returns a result table (`changes`, etc.). |
| `db:query(sql [, params])`    | Run a query and return **all** rows as an array of tables.     |
| `db:query_all(sql [, params])`| Alias of `db:query`.                                           |
| `db:query_one(sql [, params])`| Run a query and return the first row table (or `nil`).         |
| `db:prepare(sql)`             | Compile a [prepared statement](#prepared-statements).          |
| `db:transaction(fn)`          | Run `fn(db)` inside a transaction; commit on success, roll back on error. |
| `db:changes()`                | Rows changed by the most recent statement.                     |
| `db:total_changes()`          | Total rows changed over the connection's lifetime.             |
| `db:last_insert_rowid()`      | Rowid of the most recent insert.                               |
| `db:interrupt()`              | Interrupt a long-running query.                                |
| `db:autocommit()`             | `true` if the connection is in autocommit mode.                |
| `db:error_code()`             | Most recent primary SQLite result code.                        |
| `db:extended_error_code()`    | Most recent extended result code.                              |
| `db:errmsg()`                 | Most recent error message.                                     |
| `db:metrics()`                | A table of connection metrics.                                 |
| `db:is_closed()`              | Whether the database has been closed.                          |
| `db:close()`                  | Close the database and release its resources.                  |

`params` may be an array (positional `?` parameters) or a table (named
parameters). Row results map SQLite columns to table fields keyed by column name.

```mobius
import "sqlite"

var db = sqlite.open_memory()

db:exec("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)")
db:exec("INSERT INTO users (name, age) VALUES (?, ?)", ["Alice", 30])
db:exec("INSERT INTO users (name, age) VALUES (?, ?)", ["Bob", 25])

var rows = db:query("SELECT name, age FROM users WHERE age > ? ORDER BY age", [20])
for (var row in rows) {
    print(row.name, row.age)        // Bob 25, Alice 30
}

var one = db:query_one("SELECT count(*) AS n FROM users")
print(one.n)                         // 2

db:close()
```

### Transactions

```mobius
db:transaction(func(db) {
    db:exec("INSERT INTO users (name, age) VALUES (?, ?)", ["Carol", 41])
    db:exec("INSERT INTO users (name, age) VALUES (?, ?)", ["Dave", 38])
})
```

If `fn` throws, the transaction is rolled back and the error propagates.

---

## Prepared statements

`db:prepare(sql)` compiles a statement you can bind and execute repeatedly.

| Method                         | Description                                                  |
|--------------------------------|---------------------------------------------------------------|
| `stmt:bind(index_or_name, v)`  | Bind one parameter (type inferred from `v`).                  |
| `stmt:bind_all(params)`        | Bind all parameters from an array or table.                   |
| `stmt:bind_int / bind_float / bind_text / bind_blob / bind_bool / bind_null` | Type-specific binds. |
| `stmt:step()`                  | Advance one row; returns a row table or signals completion.   |
| `stmt:all()`                   | Execute and return all remaining rows as an array.            |
| `stmt:one()`                   | Execute and return the next row (or `nil`).                   |
| `stmt:run()`                   | Execute a non-query statement; returns a result table.        |
| `stmt:reset()`                 | Reset the statement to run again.                             |
| `stmt:clear_bindings()`        | Clear bound parameters.                                       |
| `stmt:columns()` / `stmt:column_count()` | Column metadata.                                    |
| `stmt:column_name(i)` / `stmt:column_type(i)` / `stmt:column_value(i)` | Per-column access. |
| `stmt:parameter_count()` / `stmt:parameter_name(i)` | Parameter metadata.                      |
| `stmt:sql()` / `stmt:expanded_sql()` | The statement's SQL text.                               |
| `stmt:read_only()`             | Whether the statement only reads.                             |
| `stmt:metrics()`               | A table of statement metrics.                                 |
| `stmt:finalize()` / `stmt:is_finalized()` | Release the statement.                            |

```mobius
var ins = db:prepare("INSERT INTO users (name, age) VALUES (?, ?)")
for (var u in [["Eve", 22], ["Frank", 55]]) {
    ins:bind_all(u)
    ins:run()
    ins:reset()
}
ins:finalize()
```
