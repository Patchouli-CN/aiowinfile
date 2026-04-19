#pragma once
#ifdef HAVE_IO_URING

#include <liburing.h>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <thread>
#include <vector>
#include <Python.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <poll.h>
#include "debug_log.hpp"

// ════════════════════════════════════════════════════════════════════════════
// io_uring 全局管理器 - 类似 Windows 的 IOCP 全局管理
// ════════════════════════════════════════════════════════════════════════════

class IOUringBackend;

#define THREAD_ID_HASH() std::hash<std::thread::id>{}(std::this_thread::get_id())

// 全局状态声明（定义在 uring_globals.cpp 中）
extern std::atomic<bool> g_uring_running;
extern std::mutex g_uring_instances_mtx;
extern std::unordered_map<void*, std::weak_ptr<struct UringInstance>> g_uring_instances;

struct UringInstance {
    struct io_uring ring;
    std::thread reaper_thread;
    std::atomic<bool> reaper_stop{false};
    PyObject* loop = nullptr;        // 关联的事件循环
    int event_fd = -1;
    
    unsigned queue_depth = 256;
    unsigned flags = 0;
    bool sqpoll = false;
    
    using ReaperFunc = void (*)(UringInstance*);
    ReaperFunc reaper_func = nullptr;
    
    // 简单的构造函数
    UringInstance() = default;
    
    // 禁止拷贝
    UringInstance(const UringInstance&) = delete;
    UringInstance& operator=(const UringInstance&) = delete;
    
    // 析构函数：只清理系统资源，Python 对象由 cleanup 统一处理
    ~UringInstance() {
        UR_LOG("UringInstance destructor: this=%p", (void*)this);
        
        reaper_stop.store(true, std::memory_order_release);
        
        // 唤醒 reaper
        if (event_fd != -1) {
            uint64_t val = 1;
            write(event_fd, &val, sizeof(val));
        }
        
        // 等待 reaper 退出
        if (reaper_thread.joinable()) {
            reaper_thread.join();
        }
        
        // 清理 io_uring
        io_uring_queue_exit(&ring);
        
        // 关闭 eventfd
        if (event_fd != -1) {
            ::close(event_fd);
        }
        
        UR_LOG("UringInstance destructor: done");
    }
    
    void stop_reaper() {
        reaper_stop.store(true, std::memory_order_release);
        if (event_fd != -1) {
            uint64_t val = 1;
            write(event_fd, &val, sizeof(val));
        }
        if (reaper_thread.joinable()) {
            reaper_thread.join();
        }
    }
};

// ════════════════════════════════════════════════════════════════════════════
// 全局 io_uring 管理器
// ════════════════════════════════════════════════════════════════════════════

class UringManager {
public:
    static UringManager& instance() {
        static UringManager mgr;
        return mgr;
    }
    
    // 获取或创建与事件循环关联的 io_uring 实例
    std::shared_ptr<UringInstance> acquire(PyObject* loop,
                                            UringInstance::ReaperFunc reaper_func,
                                            unsigned queue_depth = 256,
                                            unsigned flags = 0,
                                            bool sqpoll = false) {
        std::lock_guard<std::mutex> lk(m_mutex);
        
        void* key = loop;
        UR_LOG("UringManager::acquire: loop=%p", (void*)loop);
        
        auto it = m_instances.find(key);
        if (it != m_instances.end()) {
            auto inst = it->second.lock();
            if (inst) {
                UR_LOG("UringManager::acquire: found existing instance=%p", (void*)inst.get());
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
        
        if (!setup_instance(inst.get())) {
            UR_LOG("UringManager::acquire: setup_instance failed");
            return nullptr;
        }
        
        m_instances[key] = inst;
        
        // 同时加入全局追踪（用于 cleanup）
        {
            std::lock_guard<std::mutex> lk2(g_uring_instances_mtx);
            g_uring_instances[key] = inst;
        }
        
        UR_LOG("UringManager::acquire: created new instance=%p", (void*)inst.get());
        return inst;
    }
    
    // 启动 reaper 线程（由 IOUringBackend 调用）
    void start_reaper(std::shared_ptr<UringInstance> inst) {
        if (!inst->reaper_thread.joinable()) {
            inst->reaper_thread = std::thread(inst->reaper_func, inst.get());
            UR_LOG("UringManager::start_reaper: started reaper for inst=%p", (void*)inst.get());
        }
    }
    
    // 清理所有实例（模块退出时调用）
    void cleanup_all() {
        std::lock_guard<std::mutex> lk(m_mutex);
        UR_LOG("UringManager::cleanup_all: cleaning %zu instances", m_instances.size());
        
        for (auto& pair : m_instances) {
            if (auto inst = pair.second.lock()) {
                UR_LOG("UringManager::cleanup_all: stopping instance=%p", (void*)inst.get());
                inst->stop_reaper();
                // 释放 Python 对象引用
                if (inst->loop) {
                    PyGILState_STATE gstate = PyGILState_Ensure();
                    Py_DECREF(inst->loop);
                    inst->loop = nullptr;
                    PyGILState_Release(gstate);
                }
            }
        }
        m_instances.clear();
        
        // 同时清理全局追踪
        {
            std::lock_guard<std::mutex> lk2(g_uring_instances_mtx);
            g_uring_instances.clear();
        }
        
        UR_LOG("UringManager::cleanup_all: done");
    }
    
    size_t instance_count() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_instances.size();
    }
    
private:
    UringManager() {
        g_uring_running.store(true, std::memory_order_release);
    }
    
    ~UringManager() {
        cleanup_all();
    }
    
    bool setup_instance(UringInstance* inst) {
        inst->event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (inst->event_fd == -1) {
            UR_LOG("UringManager::setup_instance: eventfd failed, errno=%d", errno);
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
            UR_LOG("UringManager::setup_instance: io_uring_queue_init failed, ret=%d", ret);
            ::close(inst->event_fd);
            inst->event_fd = -1;
            return false;
        }
        
        // 提交 eventfd poll 请求
        struct io_uring_sqe* sqe = io_uring_get_sqe(&inst->ring);
        if (sqe) {
            io_uring_prep_poll_add(sqe, inst->event_fd, POLLIN);
            io_uring_sqe_set_data(sqe, nullptr);
            io_uring_submit(&inst->ring);
        }
        
        UR_LOG("UringManager::setup_instance: success, ring_fd=%d, event_fd=%d",
               inst->ring.ring_fd, inst->event_fd);
        return true;
    }
    
    mutable std::mutex m_mutex;
    std::unordered_map<void*, std::weak_ptr<UringInstance>> m_instances;
};

extern std::atomic<bool> g_uring_running;
extern std::mutex g_uring_instances_mtx;
extern std::unordered_map<void*, std::weak_ptr<UringInstance>> g_uring_instances;

// 便捷函数
inline UringManager& uring_manager() {
    return UringManager::instance();
}

// 模块级 cleanup 函数
inline void uring_cleanup_all() {
    UringManager::instance().cleanup_all();
}

#endif // HAVE_IO_URING