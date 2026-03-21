#pragma once
#include "globals.hpp"
#include "iocp.hpp"
#include "io_request.hpp"
#include "handle_pool.hpp"
#include <string>
#include <atomic>
#include <mutex>
#include <cstdint>

// ════════════════════════════════════════════════════════════════════════════
// §6  FileHandle
// ════════════════════════════════════════════════════════════════════════════

class FileHandle {
public:
    FileHandle(const std::string &path, const std::string &mode);
    ~FileHandle();

    PyObject *read(int64_t size = -1);
    PyObject *write(Py_buffer *view);
    PyObject *seek(int64_t offset, int whence = 0);
    PyObject *flush();
    PyObject *close();
    void      close_impl();

    // Called from IOCP worker threads (no GIL).
    void complete_ok(IORequest *req, DWORD bytes);
    void complete_error(IORequest *req, DWORD err);

private:
    HANDLE            m_handle     = INVALID_HANDLE_VALUE;
    PoolKey           m_poolKey;           // key for handle_pool_release on close
    std::atomic<bool> m_running{false};
    std::atomic<long> m_pending{0};
    std::mutex        m_posMtx;
    uint64_t          m_filePos    = 0;
    bool              m_appendMode = false;
    PyObject         *m_loop          = nullptr;  // owned
    PyObject         *m_create_future = nullptr;  // owned
    LoopHandle       *m_loop_handle   = nullptr;  // not owned

    IORequest *make_req(size_t size, PyObject *future, ReqType type);
    void complete_error_inline(IORequest *req, DWORD err);

    static void resolve_ok(PyObject *future, PyObject *val);
    static void resolve_bytes(PyObject *future, const char *buf, Py_ssize_t n);
    static void resolve_exc(PyObject *future, PyObject *cls, DWORD err, const char *msg);
};
