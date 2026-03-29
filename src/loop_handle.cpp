#include "loop_handle.hpp"

LoopHandle::LoopHandle(PyObject *loop) {
    call_soon_ts = PyObject_GetAttr(loop, g_str_call_soon_ts);
    batch.reserve(64);

    py::object fn = py::cpp_function([this]() {
        std::vector<BatchEntry> local;
        {
            std::lock_guard<std::mutex> lk(batch_mtx);
            local.swap(batch);
            dispatch_pending.store(false, std::memory_order_release);
        }
        for (auto &e : local) {
            PyObject *r = PyObject_CallFunctionObjArgs(e.set_fn, e.val, nullptr);
            if (!r) PyErr_Print(); else Py_DECREF(r);
            Py_DECREF(e.set_fn);
            Py_DECREF(e.val);
        }
    });
    drain_cb = fn.release().ptr();
}

LoopHandle::~LoopHandle() {
    Py_XDECREF(call_soon_ts);
    Py_XDECREF(drain_cb);
    for (auto &e : batch) { Py_DECREF(e.set_fn); Py_DECREF(e.val); }
}

void LoopHandle::push(PyObject *set_fn, PyObject *val) {
    bool need_schedule = false;
    {
        std::lock_guard<std::mutex> lk(batch_mtx);
        batch.push_back({set_fn, val});
        if (!dispatch_pending.load(std::memory_order_relaxed)) {
            dispatch_pending.store(true, std::memory_order_relaxed);
            need_schedule = true;
        }
    }
    if (need_schedule) {
        PyObject *r = PyObject_CallFunctionObjArgs(call_soon_ts, drain_cb, nullptr);
        if (!r) {
            // 调度失败：打印错误并重置 dispatch_pending，避免批处理永远不被调度
            PyErr_Print();
            std::lock_guard<std::mutex> lk(batch_mtx);
            dispatch_pending.store(false, std::memory_order_relaxed);
        } else {
            Py_DECREF(r);
        }
    }
}

static std::mutex                                      g_loopsMtx;
static std::vector<std::pair<PyObject*, LoopHandle*>>  g_loops;

LoopHandle *get_or_create_loop_handle(PyObject *loop) {
    std::lock_guard<std::mutex> lk(g_loopsMtx);
    for (auto &kv : g_loops) if (kv.first == loop) return kv.second;
    auto *h = new LoopHandle(loop);
    g_loops.emplace_back(loop, h);
    return h;
}
