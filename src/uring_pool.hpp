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

// ════════════════════════════════════════════════════════════════════════════
// io_uring 实例池 - 复用 io_uring，避免频繁创建/销毁
// ════════════════════════════════════════════════════════════════════════════

// 前置声明
class IOUringBackend;

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
        // 保存 event_fd 的副本，避免在析构过程中被其他线程修改
        int fd = event_fd;
        
        // 设置停止标志，使用 memory_order_release 确保可见性
        running.store(false, std::memory_order_release);
        reaper_stop.store(true, std::memory_order_release);
        
        // 唤醒 reaper 线程（如果还在运行）
        if (fd != -1) {
            uint64_t val = 1;
            ssize_t ret = write(fd, &val, sizeof(val));
            (void)ret;  // 忽略返回值，即使失败也继续
        }
        
        // 给 reaper 线程极短时间退出
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // 如果 reaper 线程还存在，detach 它（让它自己结束，不阻塞）
        if (reaper_thread.joinable()) {
            reaper_thread.detach();
        }
        
        // 清理 io_uring 资源
        io_uring_queue_exit(&ring);
        
        // 关闭 eventfd
        if (fd != -1) {
            ::close(fd);
        }
        
        // 不在这里释放 loop，避免 GIL 问题
        // Python 的 GC 会在合适的时候处理
    }
    
    void stop_reaper() {
        if (!running.exchange(false, std::memory_order_acq_rel)) {
            return;
        }
        UR_LOG("UringInstance::stop_reaper: stopping reaper, inst=%p, event_fd=%d", (void*)this, event_fd);
        
        reaper_stop.store(true, std::memory_order_release);
        
        // 向 eventfd 写入以唤醒 reaper
        int fd = event_fd;
        if (fd != -1) {
            uint64_t val = 1;
            ssize_t ret = write(fd, &val, sizeof(val));
            UR_LOG("UringInstance::stop_reaper: wrote to eventfd, ret=%zd", ret);
        }
        
        // 等待极短时间让 reaper 有机会退出
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // detach 而不是 join，避免死锁
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
        // 第一步：设置停止标志
        m_stop_cleanup.store(true, std::memory_order_release);
        
        // 第二步：唤醒清理线程
        m_cv.notify_all();
        
        // 第三步：等待清理线程退出
        if (m_cleanup_thread.joinable()) {
            m_cleanup_thread.join();
        }
        
        // 第四步：清理资源（此时没有其他线程在运行）
        // shared_ptr 会自动析构，不需要额外操作
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
            inst->event_fd = -1;
            return false;
        }
        
        struct io_uring_sqe* sqe = io_uring_get_sqe(&inst->ring);
        if (!sqe) {
            UR_LOG("UringPool::setup_instance: failed to get sqe for eventfd poll");
            io_uring_queue_exit(&inst->ring);
            ::close(inst->event_fd);
            inst->event_fd = -1;
            return false;
        }
        io_uring_prep_poll_add(sqe, inst->event_fd, POLLIN);
        io_uring_sqe_set_data(sqe, nullptr);
        ret = io_uring_submit(&inst->ring);
        if (ret < 0) {
            UR_LOG("UringPool::setup_instance: submit poll failed, ret=%d", ret);
            io_uring_queue_exit(&inst->ring);
            ::close(inst->event_fd);
            inst->event_fd = -1;
            return false;
        }
        
        UR_LOG("UringPool::setup_instance: success, ring_fd=%d, event_fd=%d", inst->ring.ring_fd, inst->event_fd);
        return true;
    }
    
    void schedule_cleanup(std::shared_ptr<UringInstance> inst) {
        std::lock_guard<std::mutex> lk(m_cleanup_mutex);
        m_pending_cleanup.push_back({std::chrono::steady_clock::now() + std::chrono::seconds(5), inst});
        m_cv.notify_one();
    }
    
    void cleanup_loop() {
        UR_LOG("UringPool::cleanup_loop: started");
        
        while (true) {
            std::unique_lock<std::mutex> lk(m_cleanup_mutex);
            
            // 等待条件：有任务要清理，或者需要停止
            m_cv.wait_for(lk, std::chrono::seconds(1), [this] { 
                return m_stop_cleanup.load(std::memory_order_acquire) || !m_pending_cleanup.empty(); 
            });
            
            // 关键：先检查停止标志，如果是 true 就立即退出
            // 这样可以避免访问可能已被析构的成员变量
            if (m_stop_cleanup.load(std::memory_order_acquire)) {
                UR_LOG("UringPool::cleanup_loop: stop flag set, exiting immediately");
                break;
            }
            
            // 安全地处理清理队列
            auto now = std::chrono::steady_clock::now();
            auto it = m_pending_cleanup.begin();
            while (it != m_pending_cleanup.end()) {
                if (it->expiry <= now) {
                    UR_LOG("UringPool::cleanup_loop: cleaning up expired instance");
                    
                    // 获取 weak_ptr 的 shared_ptr
                    auto inst = it->instance.lock();
                    if (inst) {
                        // 从主 map 中移除
                        std::lock_guard<std::mutex> lk2(m_mutex);
                        void* key = inst->loop;
                        auto map_it = m_instances.find(key);
                        if (map_it != m_instances.end()) {
                            auto existing = map_it->second.lock();
                            if (!existing || existing.get() == inst.get()) {
                                m_instances.erase(map_it);
                                UR_LOG("UringPool::cleanup_loop: removed instance from map");
                            }
                        }
                    }
                    it = m_pending_cleanup.erase(it);
                } else {
                    ++it;
                }
            }
        }
        
        UR_LOG("UringPool::cleanup_loop: exiting");
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
    std::atomic<bool> m_stop_cleanup{false};  // 改为 atomic，确保跨线程可见性
};

#endif // HAVE_IO_URING