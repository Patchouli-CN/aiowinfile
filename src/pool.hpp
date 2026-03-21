#pragma once
#include <cstddef>
#include <mutex>
#include <vector>

// ════════════════════════════════════════════════════════════════════════════
// §2  Buffer pool
// ════════════════════════════════════════════════════════════════════════════

static constexpr size_t POOL_BUF_SIZE = 64 * 1024;
static constexpr size_t POOL_MAX      = 512;

struct alignas(64) PoolBuf { char data[POOL_BUF_SIZE]; };

PoolBuf *pool_acquire();
void     pool_release(PoolBuf *p);
