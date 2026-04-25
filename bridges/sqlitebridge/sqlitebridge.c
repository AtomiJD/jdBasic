/*
 * sqlitebridge — minimal jdBasic FFI bridge for SQLite.
 *
 * Exposes a flat C API (no jdBasic types) that jdBasic can call via
 * DECLARE FUNC. Build into a DLL/.so/.dylib and place next to the
 * jdBasic interpreter (or in the working directory of the program).
 *
 * Requires the SQLite amalgamation (sqlite3.c + sqlite3.h) in the
 * same directory at build time. Download from https://sqlite.org/.
 *
 * Build (Windows):  build.bat
 * Build (POSIX, future):  cc -O2 -fPIC -shared sqlitebridge.c sqlite3.c -o libsqlitebridge.so -lpthread -ldl -lm
 */

#include "sqlite3.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #define SQLB_API __declspec(dllexport)
#else
  #define SQLB_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ── Lifecycle ───────────────────────────────────────────────── */

SQLB_API intptr_t sqlb_open(const char* path) {
    if (!path) return 0;
    sqlite3* db = NULL;
    if (sqlite3_open(path, &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return 0;
    }
    return (intptr_t)db;
}

SQLB_API int sqlb_close(intptr_t handle) {
    sqlite3* db = (sqlite3*)handle;
    if (!db) return SQLITE_OK;
    return sqlite3_close(db);
}

/* ── Direct execution ────────────────────────────────────────── */

/* Returns rows affected (>=0) on success, -1 on error.            */
SQLB_API int sqlb_exec(intptr_t handle, const char* sql) {
    sqlite3* db = (sqlite3*)handle;
    if (!db || !sql) return -1;
    char* err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (err) sqlite3_free(err);
    if (rc != SQLITE_OK) return -1;
    return sqlite3_changes(db);
}

/* ── Errors ──────────────────────────────────────────────────── */

/* Writes the last error message into out (NUL-terminated). Returns
 * bytes written (excluding NUL).                                    */
SQLB_API int sqlb_errmsg(intptr_t handle, char* out, int outLen) {
    if (!out || outLen <= 0) return 0;
    out[0] = 0;
    sqlite3* db = (sqlite3*)handle;
    if (!db) return 0;
    const char* msg = sqlite3_errmsg(db);
    if (!msg) return 0;
    int n = (int)strlen(msg);
    if (n >= outLen) n = outLen - 1;
    memcpy(out, msg, (size_t)n);
    out[n] = 0;
    return n;
}

/* ── Tiny JSON writer ────────────────────────────────────────── */

typedef struct {
    char* buf;
    int   cap;
    int   pos;
    int   overflow;
} JBuf;

static void jb_init(JBuf* j, char* out, int outLen) {
    j->buf = out; j->cap = outLen; j->pos = 0; j->overflow = 0;
    if (outLen > 0) out[0] = 0;
}

static void jb_putc(JBuf* j, char c) {
    if (j->overflow) return;
    /* Need room for c plus a trailing NUL. */
    if (j->pos + 1 >= j->cap) { j->overflow = 1; return; }
    j->buf[j->pos++] = c;
}

static void jb_puts(JBuf* j, const char* s) {
    while (*s && !j->overflow) jb_putc(j, *s++);
}

static void jb_string(JBuf* j, const char* s) {
    jb_putc(j, '"');
    if (s) {
        for (; *s && !j->overflow; s++) {
            unsigned char c = (unsigned char)*s;
            switch (c) {
                case '"':  jb_puts(j, "\\\""); break;
                case '\\': jb_puts(j, "\\\\"); break;
                case '\b': jb_puts(j, "\\b"); break;
                case '\f': jb_puts(j, "\\f"); break;
                case '\n': jb_puts(j, "\\n"); break;
                case '\r': jb_puts(j, "\\r"); break;
                case '\t': jb_puts(j, "\\t"); break;
                default:
                    if (c < 0x20) {
                        char esc[8];
                        snprintf(esc, sizeof(esc), "\\u%04x", c);
                        jb_puts(j, esc);
                    } else {
                        jb_putc(j, (char)c);
                    }
            }
        }
    }
    jb_putc(j, '"');
}

/* ── Query → JSON array of objects ───────────────────────────── */

/* Writes a JSON array such as [{"id":1,"name":"Alice"},...] into out.
 * Returns:
 *   >= 0   number of bytes written (excluding NUL)
 *   -1     SQL / prepare / step error (use sqlb_errmsg for details)
 *   -2     output buffer too small — re-call with a larger buffer    */
SQLB_API int sqlb_query_json(intptr_t handle, const char* sql, char* out, int outLen) {
    if (!out || outLen < 4) return -2;
    out[0] = 0;
    sqlite3* db = (sqlite3*)handle;
    if (!db || !sql) return -1;

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }

    JBuf j; jb_init(&j, out, outLen);
    jb_putc(&j, '[');
    int row_index = 0;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (row_index++ > 0) jb_putc(&j, ',');
        jb_putc(&j, '{');
        int cols = sqlite3_column_count(stmt);
        for (int c = 0; c < cols; c++) {
            if (c > 0) jb_putc(&j, ',');
            const char* name = sqlite3_column_name(stmt, c);
            jb_string(&j, name ? name : "");
            jb_putc(&j, ':');
            int t = sqlite3_column_type(stmt, c);
            if (t == SQLITE_INTEGER) {
                char tmp[32];
                snprintf(tmp, sizeof(tmp), "%lld", (long long)sqlite3_column_int64(stmt, c));
                jb_puts(&j, tmp);
            } else if (t == SQLITE_FLOAT) {
                char tmp[32];
                snprintf(tmp, sizeof(tmp), "%.17g", sqlite3_column_double(stmt, c));
                jb_puts(&j, tmp);
            } else if (t == SQLITE_NULL) {
                jb_puts(&j, "null");
            } else if (t == SQLITE_TEXT) {
                const char* s = (const char*)sqlite3_column_text(stmt, c);
                jb_string(&j, s ? s : "");
            } else if (t == SQLITE_BLOB) {
                int n = sqlite3_column_bytes(stmt, c);
                char hdr[40];
                snprintf(hdr, sizeof(hdr), "\"<BLOB %d bytes>\"", n);
                jb_puts(&j, hdr);
            } else {
                jb_puts(&j, "null");
            }
        }
        jb_putc(&j, '}');
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) return -1;
    jb_putc(&j, ']');

    if (j.overflow) {
        out[0] = 0;
        return -2;
    }
    out[j.pos] = 0;
    return j.pos;
}

#ifdef __cplusplus
}
#endif
