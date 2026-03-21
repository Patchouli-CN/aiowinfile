#include "pool.hpp"

static std::mutex            g_poolMtx;
static std::vector<PoolBuf*> g_pool;

PoolBuf *pool_acquire() {
    std::lock_guard<std::mutex> lk(g_poolMtx);
    if (!g_pool.empty()) { auto *p = g_pool.back(); g_pool.pop_back(); return p; }
    return new PoolBuf();
}

void pool_release(PoolBuf *p) {
    std::lock_guard<std::mutex> lk(g_poolMtx);
    if (g_pool.size() < POOL_MAX) g_pool.push_back(p); else delete p;
}
