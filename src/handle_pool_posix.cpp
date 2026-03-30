#include "handle_pool.hpp"
#include <nanobind/nanobind.h>
#include <atomic>
#include <algorithm>
#include <cctype>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <shared_mutex>
#include <unistd.h>

namespace py = nanobind;

static std::atomic_size_t g_hpMaxPerKey{HANDLE_POOL_DEFAULT_MAX_PER_KEY};
static std::atomic_size_t g_hpMaxTotal{HANDLE_POOL_DEFAULT_MAX_TOTAL};

static std::shared_mutex                                           g_hpMtx;
static std::unordered_map<PoolKey, std::vector<HANDLE>, PoolKeyHash> g_hpMap;
static size_t                                                        g_hpTotal = 0;

HANDLE handle_pool_acquire(const PoolKey &key) {
    std::shared_lock<std::shared_mutex> lk(g_hpMtx);
    auto it = g_hpMap.find(key);
    if (it == g_hpMap.end() || it->second.empty()) return INVALID_HANDLE_VALUE;
    HANDLE h = it->second.back();
    it->second.pop_back();
    --g_hpTotal;
    return h;
}

void handle_pool_release(const PoolKey &key, HANDLE h) {
    if (h == INVALID_HANDLE_VALUE) return;
    std::unique_lock<std::shared_mutex> lk(g_hpMtx);
    auto &vec = g_hpMap[key];
    size_t maxPerKey = g_hpMaxPerKey.load(std::memory_order_relaxed);
    size_t maxTotal = g_hpMaxTotal.load(std::memory_order_relaxed);
    if (vec.size() < maxPerKey && g_hpTotal < maxTotal) {
        vec.push_back(h);
        ++g_hpTotal;
    } else {
#ifndef _WIN32
        ::close(h);
#else
        CloseHandle(h);
#endif
    }
}

void set_handle_pool_limits(size_t max_per_key, size_t max_total) {
    if (max_per_key == 0 || max_total == 0) {
        throw py::value_error("handle pool limits must be > 0");
    }
    if (max_per_key > (1ull << 31) || max_total > (1ull << 31)) {
        throw py::value_error("handle pool limits are too large");
    }
    g_hpMaxPerKey.store(max_per_key, std::memory_order_relaxed);
    g_hpMaxTotal.store(max_total, std::memory_order_relaxed);
}

std::pair<size_t, size_t> get_handle_pool_limits() {
    return {g_hpMaxPerKey.load(std::memory_order_relaxed), g_hpMaxTotal.load(std::memory_order_relaxed)};
}

void handle_pool_drain() {
    std::unique_lock<std::shared_mutex> lk(g_hpMtx);
    for (auto &kv : g_hpMap)
        for (HANDLE h : kv.second) {
#ifndef _WIN32
            ::close(h);
#else
            CloseHandle(h);
#endif
        }
    g_hpMap.clear();
    g_hpTotal = 0;
}

PoolKey make_pool_key(const std::string &path, DWORD access, DWORD disp) {
    // On POSIX there is no HANDLE pooling; still return canonicalized path
    std::string canon;
    canon.reserve(path.size());
    for (char c : path) {
        canon += (c == '\\') ? '/' : (char)std::tolower((unsigned char)c);
    }
    return PoolKey{std::move(canon), access, disp};
}
