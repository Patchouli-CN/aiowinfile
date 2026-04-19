#ifdef HAVE_IO_URING

#include "io_uring_backend.hpp"
#include "../globals.hpp"
#include "../config.hpp"
#include "./utils/file_mode.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include <sys/eventfd.h>
#include <cstring>
#include <chrono>
#include <thread>

// ════════════════════════════════════════════════════════════════════════════
// 全局缓存（线程安全）
// ════════════════════════════════════════════════════════════════════════════

static PyObject*   g_cachedLoop       = nullptr;
static PyObject*   g_cachedFutureFn   = nullptr;
static LoopHandle* g_cachedLoopHandle = nullptr;
static std::mutex  g_cacheMtx;

static void refresh_loop_cache(PyObject* loop) {
    std::lock_guard<std::mutex> lk(g_cacheMtx);
    if (loop == g_cachedLoop) return;
    
    Py_XDECREF(g_cachedFutureFn);
    g_cachedLoop = loop;
    
    g_cachedFutureFn = PyObject_GetAttr(loop, g_str_create_future);
    if (g_cachedFutureFn) {
        Py_INCREF(g_cachedFutureFn);
    }
    
    g_cachedLoopHandle = get_or_create_loop_handle(loop);
}

// ════════════════════════════════════════════════════════════════════════════
// 构造函数 / 析构函数
// ════════════════════════════════════════════════════════════════════════════

IOUringBackend::IOUringBackend(const std::string& path, const std::string& mode) {
    // 解析模式
    int flags = O_RDONLY;
    ModeInfo mi;
    try {
        mi = parse_mode(mode);
    } catch (const std::invalid_argument& e) {
        throw py::value_error(e.what());
    }
    
    if (mi.hasW) {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    } else if (mi.hasA) {
        flags = O_WRONLY | O_CREAT | O_APPEND;
    } else if (mi.hasX) {
        flags = O_WRONLY | O_CREAT | O_EXCL;
    }
    if (mi.plus) {
        flags = O_RDWR;
    }
    
    m_appendMode = mi.appendMode;
    
    // 打开文件
    m_fd = open(path.c_str(), flags, 0644);
    if (m_fd == -1) {
        throw_os_error("Failed to open file", path.c_str());
    }
    
    // 缓存配置值（性能优化：避免频繁加锁读取配置）
    auto& cfg = ayafileio::config();
    m_cached_buffer_size = cfg.buffer_size();
    m_cached_buffer_pool_max = cfg.buffer_pool_max();
    m_cached_close_timeout_ms = cfg.close_timeout_ms();
    m_cached_io_uring_queue_depth = cfg.io_uring_queue_depth();
    m_cached_io_uring_flags = cfg.io_uring_flags();
    m_cached_io_uring_sqpoll = cfg.io_uring_sqpoll();
    
    m_running.store(true, std::memory_order_release);
    m_pending.store(0, std::memory_order_relaxed);
    m_filePos = 0;
    
    if (m_appendMode) {
        struct stat st;
        if (fstat(m_fd, &st) == 0) {
            m_filePos = static_cast<uint64_t>(st.st_size);
        }
    }
    
    // 关键：不在构造函数中调用任何 Python API
    // 事件循环初始化和 io_uring 启动都延迟到第一次 I/O 操作
}

IOUringBackend::~IOUringBackend() {
    close_impl();
    if (m_loop_initialized) {
        Py_XDECREF(m_create_future);
        Py_XDECREF(m_loop);
    }
}

// ════════════════════════════════════════════════════════════════════════════
// 初始化方法
// ════════════════════════════════════════════════════════════════════════════

void IOUringBackend::ensure_loop_initialized() {
    // 快速路径：已经初始化
    if (m_loop_initialized) return;
    
    // 关键修复：先获取事件循环（持有 GIL，安全），再加锁
    PyObject* loop = PyObject_CallNoArgs(g_get_running_loop);
    if (!loop) {
        PyErr_Clear();
        throw std::runtime_error("No running event loop");
    }
    
    std::lock_guard<std::mutex> lk(m_loop_init_mtx);
    if (m_loop_initialized) {
        // 其他线程已经初始化了
        Py_DECREF(loop);
        return;
    }
    
    // 更新全局缓存
    refresh_loop_cache(loop);
    
    // 保存到成员变量
    m_loop = loop;
    Py_INCREF(m_loop);
    
    m_create_future = g_cachedFutureFn;
    Py_INCREF(m_create_future);
    
    m_loop_handle = g_cachedLoopHandle;
    
    // 启动 io_uring（如果还没启动）
    if (!m_uring_started) {
        start_uring();
    }
    
    m_loop_initialized = true;
}

void IOUringBackend::start_uring() {
    if (m_uring_started) return;
    std::lock_guard<std::mutex> lk(m_loop_init_mtx);
    if (m_uring_started) return;
    
    // 创建 eventfd 用于唤醒 reaper 线程
    m_event_fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (m_event_fd == -1) {
        throw_os_error("Failed to create eventfd");
    }
    
    // 设置 io_uring
    if (!setup_uring()) {
        ::close(m_event_fd);
        m_event_fd = -1;
        throw std::runtime_error("Failed to setup io_uring");
    }
    
    m_uring_started = true;
}

bool IOUringBackend::setup_uring() {
    unsigned flags = m_cached_io_uring_flags;
    if (m_cached_io_uring_sqpoll) {
        flags |= IORING_SETUP_SQPOLL;
    }
    
    // 性能优化：使用 IORING_SETUP_SINGLE_ISSUER 减少锁竞争（Linux 6.0+）
#ifdef IORING_SETUP_SINGLE_ISSUER
    flags |= IORING_SETUP_SINGLE_ISSUER;
#endif
    
    // 性能优化：使用 IORING_SETUP_DEFER_TASKRUN 减少不必要的唤醒
#ifdef IORING_SETUP_DEFER_TASKRUN
    flags |= IORING_SETUP_DEFER_TASKRUN;
#endif
    
    int ret = io_uring_queue_init(m_cached_io_uring_queue_depth, &m_ring, flags);
    if (ret < 0) {
        return false;
    }
    
    m_reaper_stop.store(false);
    m_reaper_thread = std::thread(&IOUringBackend::reaper_loop, this);
    return true;
}

void IOUringBackend::teardown_uring() {
    if (!m_uring_started) return;
    
    m_reaper_stop.store(true);
    
    // 唤醒 reaper 线程
    if (m_event_fd != -1) {
        uint64_t val = 1;
        ssize_t written = ::write(m_event_fd, &val, sizeof(val));
        (void)written;  // 忽略返回值
    }
    
    if (m_reaper_thread.joinable()) {
        m_reaper_thread.join();
    }
    
    io_uring_queue_exit(&m_ring);
    
    if (m_event_fd != -1) {
        ::close(m_event_fd);
        m_event_fd = -1;
    }
    
    m_uring_started = false;
}

// ════════════════════════════════════════════════════════════════════════════
// Reaper 线程（处理 I/O 完成事件）
// ════════════════════════════════════════════════════════════════════════════

void IOUringBackend::reaper_loop() {
    uint64_t event_val;
    struct io_uring_sqe* sqe;
    struct io_uring_cqe* cqe;
    
    // 将 eventfd 添加到 io_uring 中（用于唤醒）
    sqe = io_uring_get_sqe(&m_ring);
    if (sqe) {
        io_uring_prep_read(sqe, m_event_fd, &event_val, sizeof(event_val), 0);
        io_uring_sqe_set_data(sqe, nullptr);
        io_uring_submit(&m_ring);
    }
    
    while (!m_reaper_stop.load(std::memory_order_relaxed)) {
        // 等待完成事件
        int ret = io_uring_wait_cqe(&m_ring, &cqe);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        
        // 批量处理完成事件（性能优化）
        unsigned head;
        unsigned count = 0;
        
        io_uring_for_each_cqe(&m_ring, head, cqe) {
            IORequest* req = static_cast<IORequest*>(io_uring_cqe_get_data(cqe));
            if (req) {
                if (cqe->res >= 0) {
                    complete_ok(req, static_cast<size_t>(cqe->res));
                } else {
                    complete_error(req, static_cast<DWORD>(-cqe->res));
                }
            } else {
                // eventfd 唤醒事件，需要重新提交
                if (!m_reaper_stop.load(std::memory_order_relaxed)) {
                    sqe = io_uring_get_sqe(&m_ring);
                    if (sqe) {
                        io_uring_prep_read(sqe, m_event_fd, &event_val, sizeof(event_val), 0);
                        io_uring_sqe_set_data(sqe, nullptr);
                        io_uring_submit(&m_ring);
                    }
                }
            }
            count++;
        }
        
        // 批量标记为已处理
        if (count > 0) {
            io_uring_cq_advance(&m_ring, count);
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════
// I/O 提交
// ════════════════════════════════════════════════════════════════════════════

void IOUringBackend::submit_io(IORequest* req, int op, int fd, 
                                const void* buf, size_t len, off_t offset) {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        // 队列满，同步处理错误
        complete_error(req, EBUSY);
        return;
    }
    
    // 注意：io_uring_prep_read/write 需要 void*，const_cast 是安全的
    // 因为内核不会修改用户空间缓冲区
    void* writeable_buf = const_cast<void*>(buf);
    
    if (op == IORING_OP_READ) {
        io_uring_prep_read(sqe, fd, writeable_buf, static_cast<unsigned>(len), offset);
    } else if (op == IORING_OP_WRITE) {
        io_uring_prep_write(sqe, fd, writeable_buf, static_cast<unsigned>(len), offset);
    } else if (op == IORING_OP_FSYNC) {
        io_uring_prep_fsync(sqe, fd, 0);
    } else {
        complete_error(req, EINVAL);
        return;
    }
    
    io_uring_sqe_set_data(sqe, req);
    
    // 提交请求
    io_uring_submit(&m_ring);
}

// ════════════════════════════════════════════════════════════════════════════
// 公共 I/O 接口
// ════════════════════════════════════════════════════════════════════════════

PyObject* IOUringBackend::read(int64_t size) {
    ensure_loop_initialized();
    
    PyObject* future = PyObject_CallNoArgs(m_create_future);
    if (!future) return nullptr;
    
    if (!m_running.load(std::memory_order_relaxed) || m_fd == -1) {
        resolve_exc(future, g_ValueError, 0, "I/O operation on closed file.");
        return future;
    }
    
    uint64_t offset;
    size_t readSize;
    {
        std::lock_guard<std::mutex> lk(m_posMtx);
        
        struct stat st;
        if (fstat(m_fd, &st) != 0) {
            set_os_error("fstat failed");
            resolve_exc(future, g_OSError, errno, "fstat failed");
            return future;
        }
        
        int64_t rem = static_cast<int64_t>(st.st_size) - static_cast<int64_t>(m_filePos);
        if (rem <= 0) {
            resolve_bytes(future, nullptr, 0);
            return future;
        }
        
        if (size < 0) {
            readSize = static_cast<size_t>(rem);
        } else {
            size_t sz = static_cast<size_t>(size);
            size_t r = static_cast<size_t>(rem);
            readSize = (sz > r) ? r : sz;
        }
        
        if (readSize == 0) {
            resolve_bytes(future, nullptr, 0);
            return future;
        }
        
        offset = m_filePos;
        m_filePos += readSize;
    }
    
    IORequest* req = make_req(readSize, future, ReqType::Read);
    m_pending.fetch_add(1, std::memory_order_relaxed);
    submit_io(req, IORING_OP_READ, m_fd, req->buf(), readSize, 
              static_cast<off_t>(offset));
    
    return future;
}

PyObject* IOUringBackend::write(Py_buffer* view) {
    ensure_loop_initialized();
    
    size_t size = static_cast<size_t>(view->len);
    PyObject* future = PyObject_CallNoArgs(m_create_future);
    if (!future) return nullptr;
    
    if (!m_running.load(std::memory_order_relaxed) || m_fd == -1) {
        resolve_exc(future, g_ValueError, 0, "I/O operation on closed file.");
        return future;
    }
    
    if (size == 0) {
        PyObject* z = PyLong_FromLong(0);
        resolve_ok(future, z);
        Py_DECREF(z);
        return future;
    }
    
    uint64_t offset;
    {
        std::lock_guard<std::mutex> lk(m_posMtx);
        
        if (m_appendMode) {
            struct stat st;
            if (fstat(m_fd, &st) != 0) {
                set_os_error("fstat failed");
                resolve_exc(future, g_OSError, errno, "fstat failed");
                return future;
            }
            offset = static_cast<uint64_t>(st.st_size);
        } else {
            offset = m_filePos;
        }
        m_filePos = offset + size;
    }
    
    IORequest* req = make_req(size, future, ReqType::Write);
    std::memcpy(req->buf(), view->buf, size);
    
    m_pending.fetch_add(1, std::memory_order_relaxed);
    submit_io(req, IORING_OP_WRITE, m_fd, req->buf(), size, 
              static_cast<off_t>(offset));
    
    return future;
}

PyObject* IOUringBackend::seek(int64_t offset, int whence) {
    ensure_loop_initialized();
    
    PyObject* future = PyObject_CallNoArgs(m_create_future);
    if (!future) return nullptr;
    
    {
        std::lock_guard<std::mutex> lk(m_posMtx);
        
        if (whence == 0) {
            m_filePos = static_cast<uint64_t>(offset);
        } else if (whence == 1) {
            m_filePos = static_cast<uint64_t>(static_cast<int64_t>(m_filePos) + offset);
        } else if (whence == 2) {
            struct stat st;
            if (fstat(m_fd, &st) != 0) {
                set_os_error("fstat failed");
                resolve_exc(future, g_OSError, errno, "fstat failed");
                return future;
            }
            m_filePos = static_cast<uint64_t>(static_cast<int64_t>(st.st_size) + offset);
        } else {
            resolve_exc(future, g_ValueError, 0, "Invalid whence value");
            return future;
        }
    }
    
    PyObject* pos = PyLong_FromUnsignedLongLong(m_filePos);
    resolve_ok(future, pos);
    Py_DECREF(pos);
    return future;
}

PyObject* IOUringBackend::flush() {
    ensure_loop_initialized();
    
    PyObject* future = PyObject_CallNoArgs(m_create_future);
    if (!future) return nullptr;
    
    if (!m_running.load(std::memory_order_relaxed) || m_fd == -1) {
        resolve_exc(future, g_OSError, 0, "flush on closed file");
        return future;
    }
    
    IORequest* req = make_req(0, future, ReqType::Other);
    m_pending.fetch_add(1, std::memory_order_relaxed);
    submit_io(req, IORING_OP_FSYNC, m_fd, nullptr, 0, 0);
    
    return future;
}

PyObject* IOUringBackend::close() {
    if (!m_loop_initialized) {
        close_impl();
        Py_RETURN_NONE;
    }
    
    ensure_loop_initialized();
    PyObject* future = PyObject_CallNoArgs(m_create_future);
    if (!future) return nullptr;
    
    close_impl();
    resolve_ok(future, Py_None);
    return future;
}

void IOUringBackend::close_impl() {
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false)) return;
    
    // 等待 pending I/O 完成（使用配置的超时时间）
    int elapsed = 0;
    int wait_time = 1;
    while (elapsed < static_cast<int>(m_cached_close_timeout_ms) && 
           m_pending.load(std::memory_order_acquire) > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(wait_time));
        elapsed += wait_time;
        wait_time = std::min(wait_time * 2, 32);  // 指数退避
    }
    
    teardown_uring();
    
    if (m_fd != -1) {
        ::close(m_fd);
        m_fd = -1;
    }
}

// ════════════════════════════════════════════════════════════════════════════
// 完成处理（工作线程中调用）
// ════════════════════════════════════════════════════════════════════════════

void IOUringBackend::complete_ok(IORequest* req, size_t bytes) {
    m_pending.fetch_sub(1, std::memory_order_release);
    
    // 关键修复：在工作线程中获取 GIL
    PyGILState_STATE gs = PyGILState_Ensure();
    
    PyObject* val = nullptr;
    if (req->type == ReqType::Read) {
        val = PyBytes_FromStringAndSize(req->buf(), static_cast<Py_ssize_t>(bytes));
    } else if (req->type == ReqType::Write) {
        val = PyLong_FromSsize_t(static_cast<Py_ssize_t>(bytes));
    } else {
        val = Py_None;
        Py_INCREF(Py_None);
    }
    
    PyObject* set_fn = req->set_result;
    req->set_result = nullptr;
    Py_DECREF(req->future);
    req->future = nullptr;
    Py_XDECREF(req->set_exception);
    req->set_exception = nullptr;
    
    req->loop_handle->push(set_fn, val);
    delete req;
    
    PyGILState_Release(gs);
}

void IOUringBackend::complete_error(IORequest* req, DWORD err) {
    m_pending.fetch_sub(1, std::memory_order_release);
    
    // 关键修复：在工作线程中获取 GIL
    PyGILState_STATE gs = PyGILState_Ensure();
    
    PyObject* exc_class = map_posix_error(static_cast<int>(err));
    PyObject* exc = PyObject_CallFunction(exc_class, "is", 
                                           static_cast<int>(err), 
                                           "I/O operation failed");
    
    PyObject* set_fn = req->set_exception;
    req->set_exception = nullptr;
    Py_DECREF(req->future);
    req->future = nullptr;
    Py_XDECREF(req->set_result);
    req->set_result = nullptr;
    
    req->loop_handle->push(set_fn, exc);
    delete req;
    
    PyGILState_Release(gs);
}

// ════════════════════════════════════════════════════════════════════════════
// 辅助方法
// ════════════════════════════════════════════════════════════════════════════

IORequest* IOUringBackend::make_req(size_t size, PyObject* future, ReqType type) {
    auto* req = new IORequest();
    req->file = this;
    req->loop_handle = m_loop_handle;
    req->future = future;
    Py_INCREF(future);
    req->set_result = PyObject_GetAttr(future, g_str_set_result);
    req->set_exception = PyObject_GetAttr(future, g_str_set_exception);
    req->reqSize = size;
    req->type = type;
    
    // 性能优化：使用缓冲区池
    if (size <= m_cached_buffer_size) {
        req->poolBuf = pool_acquire_with_size(size);
    } else {
        req->heapBuf = new char[size];
    }
    return req;
}

void IOUringBackend::complete_error_inline(IORequest* req, DWORD err) {
    // 注意：此函数在持有 GIL 的上下文中调用（同步错误）
    m_pending.fetch_sub(1, std::memory_order_relaxed);
    
    PyObject* exc_class = map_posix_error(static_cast<int>(err));
    PyObject* exc = PyObject_CallFunction(exc_class, "is", 
                                           static_cast<int>(err), 
                                           "I/O operation failed");
    PyObject* set_fn = req->set_exception;
    req->set_exception = nullptr;
    PyObject* r = PyObject_CallFunctionObjArgs(set_fn, exc, nullptr);
    Py_XDECREF(r);
    Py_DECREF(set_fn);
    Py_DECREF(exc);
    delete req;
}

#endif // HAVE_IO_URING