#pragma once
#include "value.h"
#include <thread>
#include <atomic>
#include <map>
#include <mutex>
#include <memory>

struct AsyncTask {
    std::thread thread;
    Value result;
    std::atomic<bool> done{false};
};

extern std::map<int, std::shared_ptr<AsyncTask>> g_async_tasks;
extern std::mutex g_async_mutex;
extern std::atomic<int> g_async_next_id;
