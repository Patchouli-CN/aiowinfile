#pragma once
#include "../io_backend.hpp"
#include <string>
#include <atomic>
#include <mutex>
#include <thread>
#include <queue>
#include <functional>
#include <cstdint>
#include <condition_variable>

class ThreadIOBackend : public IOBackendBase {
public:
    ThreadIOBackend(const std::string& path, const std::string& mode);
    ~ThreadIOBackend() override;

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
    
    // 事件循环相关成员 - 延迟初始化，但在第一次 I/O 前必须完成
    bool m_loop_initialized = false;
    std::mutex m_loop_init_mtx;
    PyObject* m_loop = nullptr;
    PyObject* m_create_future = nullptr;
    LoopHandle* m_loop_handle = nullptr;

    // 线程池
    std::vector<std::thread> m_workers;
    unsigned m_num_workers = 0;
    std::mutex m_queueMtx;
    std::queue<std::function<void()>> m_taskQueue;
    std::condition_variable m_cv;
    bool m_stop = false;

    // 初始化方法
    void ensure_loop_initialized();  // I/O 操作前调用，持有 GIL
    void start_workers();
    void worker_thread();
    void enqueue_task(std::function<void()> task);

    IORequest* make_req(size_t size, PyObject* future, ReqType type) override;
    void complete_error_inline(IORequest* req, DWORD err) override;
    
    // 缓存的配置值
    size_t m_cached_buffer_size = 65536;
    size_t m_cached_buffer_pool_max = 512;
    unsigned m_cached_close_timeout_ms = 4000;
};