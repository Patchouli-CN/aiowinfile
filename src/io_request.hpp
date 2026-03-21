#pragma once
#include "globals.hpp"
#include "pool.hpp"
#include "loop_handle.hpp"
#include <windows.h>
#include <cstdint>

// ════════════════════════════════════════════════════════════════════════════
// §5  IORequest
// ════════════════════════════════════════════════════════════════════════════

class FileHandle;

enum class ReqType : uint8_t { Read, Write, Other };

struct IORequest {
    OVERLAPPED   ov{};
    FileHandle  *file          = nullptr;
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
