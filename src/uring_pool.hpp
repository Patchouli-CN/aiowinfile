#pragma once
#ifdef HAVE_IO_URING

#include <liburing.h>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <vector>
#include <Python.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <poll.h>
#include "debug_log.hpp"

// 辅助宏：安全打印 std::thread::id
#define THREAD_ID_HASH() std::hash<std::thread::id>{}(std::this_thread::get_id())

struct UringInstance {
    struct io_uring ring;
    std::atomic<int> ref_count{0};
    std::atomic<bool> running{true};
    std::thread reaper_thread;
    std::atomic<bool> reaper_stop{false};
    PyObject* loop = nullptr;
    int event_fd = -1;
    
    unsigned queue_depth = 256;
    unsigned flags = 0;
    bool sqpoll = false;
    
    using ReaperFunc = void (*)(UringInstance*);
    ReaperFunc reaper_func = nullptr;
    
    ~UringInstance() {
        // 安全清理：先通知停止，等待极短时间，然后 detach
        running.store(false);
        reaper_stop.store(true);
        
        if (event_fd != -1) {
            uint64_t val = 1;
            write(event_fd, &val, sizeof(val));
        }
        
        // 给 reaper 线程 50ms 时间退出
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        if (reaper_thread.joinable()) {
            reaper_thread.detach();
        }
        
        io_uring_queue_exit(&ring);
        
        if (event_fd != -1) {
            ::close(event_fd);
        }
        
        // 不在这里释放 loop，避免 GIL 问题
        // Py_XDECREF(loop);
    }
    
    void stop_reaper() {
        if (!running.exchange(false)) return;
        UR_LOG("UringInstance::stop_reaper: stopping reaper, inst=%p, event_fd=%d", (void*)this, event_fd);
        
        reaper_stop.store(true, std::memory_order_release);
        
        if (event_fd != -1) {
            uint64_t val = 1;
            ssize_t ret = write(event_fd, &val, sizeof(val));
            UR_LOG("UringInstance::stop_reaper: wrote to eventfd, ret=%zd", ret);
        }
        
        // 等待 10ms 让 reaper 有机会退出
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        if (reaper_thread.joinable()) {
            reaper_thread.detach();
            UR_LOG("UringInstance::stop_reaper: detached reaper thread");
        }
    }
    
    void add_ref() { ref_count.fetch_add(1, std::memory_order_relaxed); }
    void release() { ref_count.fetch_sub(1, std::memory_order_relaxed); }
    int get_ref() const { return ref_count.load(std::memory_order_relaxed); }
};

class UringPool {
public:
    static UringPool& instance() {
        static UringPool pool;
        return pool;
    }
    
    std::shared_ptr<UringInstance> acquire(PyObject* loop, 
                                            UringInstance::ReaperFunc reaper_func,
                                            unsigned queue_depth = 256,
                                            unsigned flags = 0,
                                            bool sqpoll = false) {
        std::lock_guard<std::mutex> lk(m_mutex);
        void* key = loop;
        UR_LOG("UringPool::acquire: loop=%p, key=%p", (void*)loop, key);
        
        auto it = m_instances.find(key);
        if (it != m_instances.end()) {
            auto inst = it->second.lock();
            if (inst && inst->running.load(std::memory_order_acquire)) {
                inst->add_ref();
                UR_LOG("UringPool::acquire: found existing instance=%p, ref=%d", (void*)inst.get(), inst->get_ref());
                return inst;
            }
            m_instances.erase(it);
        }
        
        auto inst = std::make_shared<UringInstance>();
        inst->queue_depth = queue_depth;
        inst->flags = flags;
        inst->sqpoll = sqpoll;
        inst->loop = loop;
        inst->reaper_func = reaper_func;
        Py_INCREF(loop);
        
        if (!setup_instance(inst.get())) {
            Py_DECREF(loop);
            return nullptr;
        }
        
        inst->add_ref();
        m_instances[key] = inst;
        UR_LOG("UringPool::acquire: created new instance=%p", (void*)inst.get());
        return inst;
    }
    
    void release(std::shared_ptr<UringInstance>& inst) {
        if (!inst) return;
        inst->release();
        int ref = inst->get_ref();
        UR_LOG("UringPool::release: inst=%p, ref=%d", (void*)inst.get(), ref);
        if (ref == 0) {
            schedule_cleanup(inst);
        }
    }
    
private:
    UringPool() {
        m_cleanup_thread = std::thread(&UringPool::cleanup_loop, this);
    }
    
    ~UringPool() {
        m_stop_cleanup = true;
        m_cv.notify_all();
        if (m_cleanup_thread.joinable()) {
            m_cleanup_thread.join();
        }
    }
    
    bool setup_instance(UringInstance* inst) {
        inst->event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (inst->event_fd == -1) {
            UR_LOG("UringPool::setup_instance: eventfd failed, errno=%d", errno);
            return false;
        }
        
        unsigned actual_flags = inst->flags;
        if (inst->sqpoll) actual_flags |= IORING_SETUP_SQPOLL;
#ifdef IORING_SETUP_SINGLE_ISSUER
        actual_flags |= IORING_SETUP_SINGLE_ISSUER;
#endif
#ifdef IORING_SETUP_DEFER_TASKRUN
        actual_flags |= IORING_SETUP_DEFER_TASKRUN;
#endif
        
        int ret = io_uring_queue_init(inst->queue_depth, &inst->ring, actual_flags);
        if (ret < 0) {
            UR_LOG("UringPool::setup_instance: io_uring_queue_init failed, ret=%d", ret);
            ::close(inst->event_fd);
            return false;
        }
        
        struct io_uring_sqe* sqe = io_uring_get_sqe(&inst->ring);
        if (!sqe) {
            io_uring_queue_exit(&inst->ring);
            ::close(inst->event_fd);
            return false;
        }
        io_uring_prep_poll_add(sqe, inst->event_fd, POLLIN);
        io_uring_sqe_set_data(sqe, nullptr);
        io_uring_submit(&inst->ring);
        
        UR_LOG("UringPool::setup_instance: success, ring_fd=%d, event_fd=%d", inst->ring.ring_fd, inst->event_fd);
        return true;
    }
    
    void schedule_cleanup(std::shared_ptr<UringInstance> inst) {
        std::lock_guard<std::mutex> lk(m_cleanup_mutex);
        m_pending_cleanup.push_back({std::chrono::steady_clock::now() + std::chrono::seconds(5), inst});
        m_cv.notify_one();
    }
    
    void cleanup_loop() {
        while (!m_stop_cleanup) {
            std::unique_lock<std::mutex> lk(m_cleanup_mutex);
            m_cv.wait_for(lk, std::chrono::seconds(1), [this] { return m_stop_cleanup || !m_pending_cleanup.empty(); });
            if (m_stop_cleanup) break;
            
            auto now = std::chrono::steady_clock::now();
            auto it = m_pending_cleanup.begin();
            while (it != m_pending_cleanup.end()) {
                if (it->expiry <= now) {
                    std::lock_guard<std::mutex> lk2(m_mutex);
                    if (auto inst = it->instance.lock()) {
                        void* key = inst->loop;
                        m_instances.erase(key);
                    }
                    it = m_pending_cleanup.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }
    
    struct CleanupEntry {
        std::chrono::steady_clock::time_point expiry;
        std::weak_ptr<UringInstance> instance;
    };
    
    std::mutex m_mutex;
    std::unordered_map<void*, std::weak_ptr<UringInstance>> m_instances;
    std::mutex m_cleanup_mutex;
    std::vector<CleanupEntry> m_pending_cleanup;
    std::condition_variable m_cv;
    std::thread m_cleanup_thread;
    bool m_stop_cleanup = false;
};

#endif