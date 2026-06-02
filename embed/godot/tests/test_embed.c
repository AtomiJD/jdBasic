/* test_embed.c - standalone sanity test for the jdb_embed C-ABI.
 *
 * Builds against jdbrt.lib + jdbrt.dll. No Godot involved - this is the
 * "does my DLL actually work" check before we drag the GDExtension layer
 * on top.
 *
 * Build: see build_test_embed.bat in this folder.
 * Run  : test_embed.exe  (jdbrt.dll + its sat DLLs must be alongside)
 */

#include <stdio.h>
#include <stdlib.h>
#include "../../../src/jdb_embed_api.h"

static void run(JdbEmbed* e, const char* code) {
    char* out = jdb_embed_eval(e, code);
    if (out) {
        printf("--- eval %s\n%s", code, out);
        if (out[0] == '\0' || out[strlen(out)-1] != '\n') printf("\n");
        jdb_embed_free(out);
    } else {
        printf("--- eval %s\nERR: %s\n", code, jdb_embed_last_error(e));
    }
}

static int g_break_hits = 0;

/* Debugger hook (the Godot model): fires synchronously when the VM hits a
 * breakpoint. We inspect the paused VM and then continue. */
static void on_break(JdbEmbed* e, int line, const char* reason, void* ud) {
    (void)ud;
    g_break_hits++;
    printf("[break] line=%d reason=%s\n", line, reason);
    int sc = jdb_embed_debug_stack_count(e);
    printf("  stack=%d top=%s@%d\n", sc,
           jdb_embed_debug_stack_function(e, 0), jdb_embed_debug_stack_line(e, 0));
    int lc = jdb_embed_debug_locals_count(e);
    printf("  locals(%d):", lc);
    for (int i = 0; i < lc; i++)
        printf(" %s=%s", jdb_embed_debug_local_name(e, i), jdb_embed_debug_local_value(e, i));
    printf("\n");
    int gc = jdb_embed_debug_globals_count(e);
    printf("  globals(%d):", gc);
    for (int i = 0; i < gc; i++)
        printf(" %s=%s", jdb_embed_debug_global_name(e, i), jdb_embed_debug_global_value(e, i));
    printf("\n");
    jdb_embed_debug_continue(e);
}

/* Per-line breakpoint predicate (the Godot model): break at line 2 only. */
static int line_is_break(JdbEmbed* e, int line, void* ud) {
    (void)e; (void)ud;
    return line == 2 ? 1 : 0;
}

int main(void) {
    printf("jdb_embed smoke test\n");

    JdbEmbed* e = jdb_embed_init();
    if (!e) { printf("init failed\n"); return 1; }

    run(e, "PRINT 3 * 7");
    run(e, "DIM x = 10\nDIM y = 32\nPRINT x + y");
    run(e, "PRINT \"hello from \" + \"jdBasic\"");
    run(e, "FOR i = 1 TO 5\nPRINT i, i * i\nNEXT i");

    /* Persistence across calls: x and y must survive into the next eval. */
    run(e, "PRINT \"x is still \"; x");

    /* Map mutation, the live-tweak pattern from Stellar Drift: */
    run(e, "DIM cfg = {\"speed\": 1.0, \"colour\": \"teal\"}");
    run(e, "PRINT cfg{\"speed\"}, cfg{\"colour\"}");
    run(e, "cfg{\"speed\"} = 42.0");
    run(e, "PRINT cfg{\"speed\"}");

    /* Deliberate error to exercise the error path. */
    run(e, "PRINT undefined_symbol_xyz");

    /* T7 debugger ABI. */
    printf("\n=== debugger ===\n");
    jdb_embed_debug_enable(e);
    jdb_embed_debug_set_hook(e, on_break, NULL);

    /* Scenario 1: breakpoint at global scope - vars are globals, no frame. */
    printf("-- global breakpoint (line 3) --\n");
    jdb_embed_debug_set_breakpoint(e, 3);
    run(e, "DIM gg1 = 5\nDIM gg2 = 7\nPRINT gg1 + gg2\nPRINT gg1 * gg2");

    /* Scenario 2: breakpoint inside a SUB - a real call frame + locals. */
    printf("-- function breakpoint (line 3, inside SUB) --\n");
    jdb_embed_debug_clear_all(e);
    jdb_embed_debug_set_breakpoint(e, 3);
    run(e, "SUB add_them(p, q)\n  DIM r = p + q\n  PRINT r\nENDSUB\nadd_them(5, 7)");

    /* Scenario 3: no map breakpoint - a per-line predicate decides (this is
     * how Godot editor breakpoints will be polled via is_breakpoint). */
    printf("-- line-hook breakpoint (predicate: line 2) --\n");
    jdb_embed_debug_clear_all(e);
    jdb_embed_debug_set_line_hook(e, line_is_break, NULL);
    run(e, "DIM h1 = 1\nDIM h2 = 2\nPRINT h1 + h2");
    jdb_embed_debug_set_line_hook(e, NULL, NULL);  /* detach */

    printf("breakpoint hits = %d (expected 3)\n", g_break_hits);

    jdb_embed_shutdown(e);
    printf("done\n");
    return 0;
}
