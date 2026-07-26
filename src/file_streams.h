#pragma once
#include <atomic>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>

// jdBasic FILE streaming handle - line-by-line text reader with optional
// tail-follow mode (analog zu `tail -f`). Lives in a process-global
// registry indexed by an i64 handle so it can be passed across ASYNC
// FUNC boundaries (each ASYNC FUNC is a fresh VM, see vm.cpp:1811).

struct FileHandle {
    std::mutex            mtx;
    std::ifstream         stream;
    std::string           path;            // for diagnostics + tail re-arm
    bool                  tail_mode = false;
    std::atomic<bool>     closed{false};
};

extern std::mutex                                       g_files_mutex;
extern std::map<int64_t, std::shared_ptr<FileHandle>>   g_files;
extern std::atomic<int64_t>                             g_files_next_id;

std::shared_ptr<FileHandle> file_lookup(int64_t handle);
int64_t                     file_register(std::shared_ptr<FileHandle> fh);
void                        file_unregister(int64_t handle);

// Synchronous APIs used by the natives.
// Returns next line (without trailing \n or \r). For tail-mode handles:
// blocks polling the file every poll_ms milliseconds until either (a) a
// line is available, or (b) the handle has been closed (returns empty).
std::string file_readline(FileHandle& fh, int poll_ms = 50);

// Whether READLINE$ can still produce data.
//   non-tail: true once the stream is past the last \n
//   tail:     only true after CLOSE
bool file_at_eof(FileHandle& fh);

// Idempotent close + wake. Sets the closed flag.
void file_close(FileHandle& fh);
