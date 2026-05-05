#include "file_streams.h"

#include <chrono>
#include <thread>
#include <utility>

std::mutex                                       g_files_mutex;
std::map<int64_t, std::shared_ptr<FileHandle>>   g_files;
std::atomic<int64_t>                             g_files_next_id{1};

std::shared_ptr<FileHandle> file_lookup(int64_t handle) {
    std::lock_guard<std::mutex> lock(g_files_mutex);
    auto it = g_files.find(handle);
    if (it == g_files.end()) return nullptr;
    return it->second;
}

int64_t file_register(std::shared_ptr<FileHandle> fh) {
    int64_t id = g_files_next_id++;
    std::lock_guard<std::mutex> lock(g_files_mutex);
    g_files[id] = std::move(fh);
    return id;
}

void file_unregister(int64_t handle) {
    std::shared_ptr<FileHandle> fh;
    {
        std::lock_guard<std::mutex> lock(g_files_mutex);
        auto it = g_files.find(handle);
        if (it == g_files.end()) return;
        fh = std::move(it->second);
        g_files.erase(it);
    }
    if (fh) file_close(*fh);
}

void file_close(FileHandle& fh) {
    if (fh.closed.exchange(true)) return; // idempotent
    std::lock_guard<std::mutex> lock(fh.mtx);
    if (fh.stream.is_open()) fh.stream.close();
}

// Strip a trailing \r left over from CRLF line endings on Windows-written
// files (std::getline cuts at \n only).
static void strip_trailing_cr(std::string& s) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
}

std::string file_readline(FileHandle& fh, int poll_ms) {
    if (fh.closed.load()) return "";

    while (true) {
        std::string line;
        bool got = false;
        {
            std::lock_guard<std::mutex> lock(fh.mtx);
            if (!fh.stream.is_open()) return "";
            if (std::getline(fh.stream, line)) {
                got = true;
            } else {
                // EOF or error. For non-tail handles we stop here. For
                // tail handles we clear EOF, wait a tick, retry — same
                // mechanic as `tail -f`.
                if (!fh.tail_mode) {
                    return "";
                }
                fh.stream.clear();
            }
        }
        if (got) {
            strip_trailing_cr(line);
            return line;
        }
        // Tail-mode wait. Periodically check the closed flag so an
        // external CLOSE can wake the reader within poll_ms.
        if (fh.closed.load()) return "";
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
        if (fh.closed.load()) return "";
        // Loop continues: another getline attempt with cleared eof.
    }
}

bool file_at_eof(FileHandle& fh) {
    if (fh.closed.load()) return true;
    if (fh.tail_mode) return false; // never EOF until explicitly closed
    std::lock_guard<std::mutex> lock(fh.mtx);
    if (!fh.stream.is_open()) return true;
    // peek() advances eof state if we're already past the last line.
    fh.stream.peek();
    return fh.stream.eof();
}
