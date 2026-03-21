#pragma once
#include <windows.h>
#include <string>
#include <shared_mutex>
#include <vector>
#include <unordered_map>

// ════════════════════════════════════════════════════════════════════════════
// §9  Handle Pool  –  reuse HANDLE + IOCP association across open/close cycles
//
// Problem: CreateFileA + CreateIoCompletionPort are expensive syscalls.
// For workloads that open/read/close many files (scenario 1/3/5), each
// open pays this cost even if the same path was recently used.
//
// Solution: When a FileHandle is closed, instead of CloseHandle(), donate
// the HANDLE back to the pool keyed by (canonical_path, access_flags).
// The next open() for the same path+mode retrieves the cached HANDLE,
// skipping both CreateFileA and CreateIoCompletionPort entirely.
//
// Constraints:
//   - Pool is bounded (MAX_PER_KEY per key, MAX_TOTAL total).
//   - Entries are evicted (CloseHandle) when the pool is full.
//   - On process shutdown, drain_pool() closes all cached handles.
//   - Thread-safe: use shared_mutex for better concurrency (readers can proceed in parallel).
// ════════════════════════════════════════════════════════════════════════════

struct PoolKey {
    std::string path;   // canonical (lowercased) path
    DWORD       access; // GENERIC_READ | GENERIC_WRITE combination
    DWORD       disp;   // creation disposition

    bool operator==(const PoolKey &o) const noexcept {
        return access == o.access && disp == o.disp && path == o.path;
    }
};

struct PoolKeyHash {
    size_t operator()(const PoolKey &k) const noexcept {
        size_t h = std::hash<std::string>{}(k.path);
        h ^= std::hash<DWORD>{}(k.access) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<DWORD>{}(k.disp)   + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

// Default values can be overridden via runtime API.
static constexpr size_t HANDLE_POOL_DEFAULT_MAX_PER_KEY = 64;
static constexpr size_t HANDLE_POOL_DEFAULT_MAX_TOTAL   = 2048;

// Try to acquire a cached HANDLE. Returns INVALID_HANDLE_VALUE on miss.
HANDLE handle_pool_acquire(const PoolKey &key);

// Return a HANDLE to the pool. Closes it immediately if pool is full.
void handle_pool_release(const PoolKey &key, HANDLE h);

// Close all pooled handles (called at shutdown).
void handle_pool_drain();

// Build a canonical PoolKey from open() parameters.
PoolKey make_pool_key(const std::string &path, DWORD access, DWORD disp);

// 运行时控制池大小
void set_handle_pool_limits(size_t max_per_key, size_t max_total);
std::pair<size_t, size_t> get_handle_pool_limits();
