// SQL.* builtins — embedded SQLite, compiled under /DSQLITE.
// The amalgamation (bridges/sqlitebridge/sqlite3.c) is linked statically,
// so the binary carries its own database engine: no DLL, no install.
//
//   SQL.OPEN(path$)        -> handle (0 on failure; SQL.ERRMSG$(0) explains)
//   SQL.CLOSE(handle)      -> TRUE/FALSE
//   SQL.EXEC(handle, sql$) -> rows affected, -1 on error
//   SQL.QUERY(handle, sql$)   -> array of row maps   (interpreter only)
//   SQL.TABLE(handle, sql$)   -> 2D array of row values
//   SQL.COLUMNS(handle, sql$) -> string array of result column names
//   SQL.ERRMSG$(handle)    -> last error message
//
// Handles are plain integers so they cross the native bridge as i64.
// SQL.QUERY returns maps, which cannot cross the bridge — native -c
// rejects it at compile time; use SQL.TABLE + SQL.COLUMNS there.

#include "vm.h"
#include "value.h"
#include "sql.h"
#include "sqlite3.h"

#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {

std::mutex g_mx;
std::unordered_map<int64_t, sqlite3*> g_dbs;
int64_t g_next_handle = 1;
std::string g_open_error;  // failures that have no live handle yet

sqlite3* db_for(int64_t h) {
    std::lock_guard<std::mutex> lk(g_mx);
    auto it = g_dbs.find(h);
    return it == g_dbs.end() ? nullptr : it->second;
}

Value column_value(sqlite3_stmt* st, int c) {
    switch (sqlite3_column_type(st, c)) {
        case SQLITE_INTEGER: return Value::make_i64(sqlite3_column_int64(st, c));
        case SQLITE_FLOAT:   return Value::make_f64(sqlite3_column_double(st, c));
        case SQLITE_NULL:    return Value::make_none();
        case SQLITE_BLOB: {
            const void* p = sqlite3_column_blob(st, c);
            int n = sqlite3_column_bytes(st, c);
            return Value::make_string(std::string((const char*)p, (size_t)(n > 0 ? n : 0)));
        }
        default: {
            const unsigned char* s = sqlite3_column_text(st, c);
            return Value::make_string(s ? (const char*)s : "");
        }
    }
}

sqlite3_stmt* prepare_or_throw(int64_t h, const std::string& sql) {
    sqlite3* db = db_for(h);
    if (!db) throw std::runtime_error("SQL: invalid handle " + std::to_string(h));
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) {
        std::string msg = sqlite3_errmsg(db);
        throw std::runtime_error("SQL: " + msg);
    }
    return st;
}

}  // namespace

void register_sql_builtins(VM& vm) {
    vm.register_native("SQL.OPEN", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string path = args[0].to_string();
        sqlite3* db = nullptr;
        if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
            g_open_error = db ? sqlite3_errmsg(db) : "cannot open database";
            if (db) sqlite3_close(db);
            return Value::make_i64(0);
        }
        std::lock_guard<std::mutex> lk(g_mx);
        int64_t h = g_next_handle++;
        g_dbs[h] = db;
        return Value::make_i64(h);
    });

    vm.register_native("SQL.CLOSE", 1, 1, [](const std::vector<Value>& args) -> Value {
        int64_t h = args[0].to_int();
        std::lock_guard<std::mutex> lk(g_mx);
        auto it = g_dbs.find(h);
        if (it == g_dbs.end()) return Value::make_bool(false);
        sqlite3_close(it->second);
        g_dbs.erase(it);
        return Value::make_bool(true);
    });

    vm.register_native("SQL.EXEC", 2, 2, [](const std::vector<Value>& args) -> Value {
        sqlite3* db = db_for(args[0].to_int());
        if (!db) throw std::runtime_error("SQL: invalid handle");
        std::string sql = args[1].to_string();
        char* err = nullptr;
        int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
        if (err) sqlite3_free(err);
        if (rc != SQLITE_OK) return Value::make_i64(-1);
        return Value::make_i64(sqlite3_changes(db));
    });

    vm.register_native("SQL.ERRMSG$", 1, 1, [](const std::vector<Value>& args) -> Value {
        int64_t h = args[0].to_int();
        sqlite3* db = db_for(h);
        if (!db) return Value::make_string(g_open_error);
        return Value::make_string(sqlite3_errmsg(db));
    });

    // Row maps preserve column names per row — comfortable in the
    // interpreter, but maps can't cross the native bridge (codegen
    // rejects SQL.QUERY under -c; use SQL.TABLE + SQL.COLUMNS).
    vm.register_native("SQL.QUERY", 2, 2, [](const std::vector<Value>& args) -> Value {
        sqlite3_stmt* st = prepare_or_throw(args[0].to_int(), args[1].to_string());
        Value rows = Value::make_array();
        auto* out = rows.as_array();
        int rc;
        while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
            Value row = Value::make_object();
            auto* ob = row.as_object();
            int cols = sqlite3_column_count(st);
            for (int c = 0; c < cols; c++) {
                const char* name = sqlite3_column_name(st, c);
                ob->set(name ? name : "", column_value(st, c));
            }
            out->elements.push_back(std::move(row));
        }
        std::string err = (rc != SQLITE_DONE) ? sqlite3_errmsg(sqlite3_db_handle(st)) : "";
        sqlite3_finalize(st);
        if (!err.empty()) throw std::runtime_error("SQL: " + err);
        return rows;
    });

    vm.register_native("SQL.TABLE", 2, 2, [](const std::vector<Value>& args) -> Value {
        sqlite3_stmt* st = prepare_or_throw(args[0].to_int(), args[1].to_string());
        Value rows = Value::make_array();
        auto* out = rows.as_array();
        int rc;
        while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
            Value row = Value::make_array();
            auto* ra = row.as_array();
            int cols = sqlite3_column_count(st);
            ra->elements.reserve(cols);
            for (int c = 0; c < cols; c++)
                ra->elements.push_back(column_value(st, c));
            out->elements.push_back(std::move(row));
        }
        std::string err = (rc != SQLITE_DONE) ? sqlite3_errmsg(sqlite3_db_handle(st)) : "";
        sqlite3_finalize(st);
        if (!err.empty()) throw std::runtime_error("SQL: " + err);
        return rows;
    });

    vm.register_native("SQL.COLUMNS", 2, 2, [](const std::vector<Value>& args) -> Value {
        sqlite3_stmt* st = prepare_or_throw(args[0].to_int(), args[1].to_string());
        Value names = Value::make_array();
        auto* out = names.as_array();
        int cols = sqlite3_column_count(st);
        out->elements.reserve(cols);
        for (int c = 0; c < cols; c++) {
            const char* name = sqlite3_column_name(st, c);
            out->elements.push_back(Value::make_string(name ? name : ""));
        }
        sqlite3_finalize(st);
        return names;
    });
}
