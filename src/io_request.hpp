#pragma once
#include "globals.hpp"
#include "pool.hpp"
#include "loop_handle.hpp"
#ifdef _WIN32
#include <windows.h>
#endif
#include <cstdint>

// ════════════════════════════════════════════════════════════════════════════
// §5  IORequest
// ════════════════════════════════════════════════════════════════════════════

class IOBackendBase;

enum class ReqType : uint8_t { Read, Write, Other };

struct IORequest {
#ifdef _WIN32
    OVERLAPPED   ov{};
#endif
    IOBackendBase  *file          = nullptr;
    LoopHandle  *loop_handle   = nullptr;
    PyObject    *future        = nullptr;  // owned
    PyObject    *set_result    = nullptr;  // owned, stolen at completion
    PyObject    *set_exception = nullptr;  // owned, stolen at completion
    PoolBuf     *poolBuf       = nullptr;
    char        *heapBuf       = nullptr;
    size_t       reqSize       = 0;
    ReqType      type          = ReqType::Other;

    char *buf() noexcept { return poolBuf ? poolBuf->data : heapBuf; }

    // NOTE: Must be called while GIL is held (decrefs Python objects).
    ~IORequest() {
        Py_XDECREF(future);
        Py_XDECREF(set_result);
        Py_XDECREF(set_exception);
        if (poolBuf) pool_release(poolBuf);
        else         delete[] heapBuf;
    }
};
