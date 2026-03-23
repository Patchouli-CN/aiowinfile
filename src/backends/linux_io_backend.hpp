#pragma once
#include "io_backend.hpp"
#include <string>
#include <atomic>
#include <mutex>
#include <thread>
#include <queue>
#include <functional>
#include <cstdint>
#include <condition_variable>

// ════════════════════════════════════════════════════════════════════════════
// §8  Linux IO Backend (using epoll and threads)
// ════════════════════════════════════════════════════════════════════════════

class LinuxIOBackend : public IOBackendBase {
public:
    LinuxIOBackend(const std::string& path, const std::string& mode);
    ~LinuxIOBackend() override;

    PyObject* read(int64_t size = -1) override;
    PyObject* write(Py_buffer* view) override;
    PyObject* seek(int64_t offset, int whence = 0) override;
    PyObject* flush() override;
    PyObject* close() override;
    void close_impl() override;

    void complete_ok(IORequest* req, size_t bytes) override;
    void complete_error(IORequest* req, DWORD err) override;

private:
    int m_fd = -1;
    std::atomic<bool> m_running{false};
    std::atomic<long> m_pending{0};
    std::mutex m_posMtx;
    uint64_t m_filePos = 0;
    bool m_appendMode = false;
    PyObject* m_loop = nullptr;
    PyObject* m_create_future = nullptr;
    LoopHandle* m_loop_handle = nullptr;

    // Thread pool for async operations
    std::vector<std::thread> m_workers;
    std::mutex m_queueMtx;
    std::queue<std::function<void()>> m_taskQueue;
    std::condition_variable m_cv;
    bool m_stop = false;

    void worker_thread();
    void enqueue_task(std::function<void()> task);

    IORequest* make_req(size_t size, PyObject* future, ReqType type) override;
    void complete_error_inline(IORequest* req, DWORD err) override;
};