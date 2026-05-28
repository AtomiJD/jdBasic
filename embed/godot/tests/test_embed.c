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

    jdb_embed_shutdown(e);
    printf("done\n");
    return 0;
}
