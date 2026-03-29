#include "handle_pool.hpp"
#include <nanobind/nanobind.h>
#include <atomic>

namespace py = nanobind;

static std::atomic_size_t g_hpMaxPerKey{HANDLE_POOL_DEFAULT_MAX_PER_KEY};
static std::atomic_size_t g_hpMaxTotal{HANDLE_POOL_DEFAULT_MAX_TOTAL};

HANDLE handle_pool_acquire(const PoolKey &key) {
    (void)key;
    return INVALID_HANDLE_VALUE;
}

void handle_pool_release(const PoolKey &key, HANDLE h) {
    (void)key; (void)h;
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
    // no-op on non-Windows
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
