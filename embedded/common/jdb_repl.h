// The prompt every board shares: the command set, the line editor, the
// history and the file verbs. A port brings the four things that are
// genuinely its own and calls jdb_repl_run.

#pragma once

#include <stddef.h>
#include "jdb_embed_api.h"

#ifdef __cplusplus
extern "C" {
#endif

struct JdbReplPort {
    // One byte, or -1 once the line has been quiet for that long. RECV
    // and the autorun window are the only readers that need a deadline.
    int (*read_byte_ms)(int timeout_ms);

    // A command this board has and the others do not. The word arrives
    // upper-cased and the rest of the line unparsed; answer 1 to claim
    // the line. May be null.
    int (*board_command)(const char* cmd, char* arg);

    // Called before the editor opens, for a board that has something to
    // say about its screen first. May be null.
    void (*before_edit)(void);

    // The page the board shows at power-on: what it is and what it has.
    // Called once, before the autorun window. May be null.
    void (*hello)(void);

    // Where the name of the power-on program is kept.
    const char* autorun_path;
};

// The four hints every board's welcome page ends with, in the two columns
// they read as. A port calls this from its own hello.
void jdb_repl_hints(void);

// Argument tidying, shared with the board commands: leading and trailing
// blanks off, surrounding quotes off.
const char* jdb_repl_arg(char* s);

// A name without a dot means a program: LOAD spiel finds spiel.jdb,
// unless a file called exactly spiel is already there.
const char* jdb_repl_progname(const char* in, char* out, size_t cap);

void jdb_repl_run(JdbEmbed* vm, const struct JdbReplPort* port);

// The byte every console reader on a board goes through: a byte the
// break poll took and had to keep comes back first. timeout_us 0 answers
// at once with -1 for nothing, -1 waits.
int jdb_stdin_getc(int timeout_us);
// Asked by the VM between instructions: 1 when the console holds a
// Ctrl-C, which ends the running program.
int jdb_break_poll(void);
// 1 while a Ctrl-C waits to be reported, without taking it: a native that
// waits in its own loop leaves on this and lets the VM report it.
int jdb_break_pending(void);
// 1 while a program runs, so a console reader knows a Ctrl-C is a break
// and not a key.
extern int jdb_running;

#ifdef __cplusplus
}
#endif
