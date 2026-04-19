#pragma once
#include "../globals.hpp"
#include <Python.h>

// ════════════════════════════════════════════════════════════════════════════
// 错误处理工具函数
// ════════════════════════════════════════════════════════════════════════════

// 创建一个已完成的 Future（用于 close() 等操作在事件循环未初始化时）
inline PyObject* create_resolved_future(PyObject* loop, PyObject* value) {
    if (!loop) {
        loop = PyObject_CallNoArgs(g_get_running_loop);
        if (!loop) {
            PyErr_Clear();
            return nullptr;
        }
    } else {
        Py_INCREF(loop);
    }
    
    PyObject* future = PyObject_CallMethod(loop, "create_future", nullptr);
    Py_DECREF(loop);
    
    if (!future) {
        return nullptr;
    }
    
    PyObject* set_result = PyObject_GetAttr(future, g_str_set_result);
    if (set_result) {
        if (!value) {
            value = Py_None;
        }
        Py_INCREF(value);
        PyObject* r = PyObject_CallFunctionObjArgs(set_result, value, nullptr);
        Py_XDECREF(r);
        Py_DECREF(set_result);
    }
    
    return future;
}

// 创建一个带异常的 Future（用于已关闭文件操作等）
inline PyObject* create_rejected_future(PyObject* loop, PyObject* exc_class, 
                                         const char* msg, int err = 0) {
    if (!loop) {
        loop = PyObject_CallNoArgs(g_get_running_loop);
        if (!loop) {
            PyErr_Clear();
            return nullptr;
        }
    } else {
        Py_INCREF(loop);
    }
    
    PyObject* future = PyObject_CallMethod(loop, "create_future", nullptr);
    Py_DECREF(loop);
    
    if (!future) {
        return nullptr;
    }
    
    PyObject* exc;
    if (err != 0) {
        exc = PyObject_CallFunction(exc_class, "is", err, msg);
    } else {
        exc = PyObject_CallFunction(exc_class, "s", msg);
    }
    
    if (!exc) {
        Py_DECREF(future);
        return nullptr;
    }
    
    PyObject* set_exception = PyObject_GetAttr(future, g_str_set_exception);
    if (set_exception) {
        PyObject* r = PyObject_CallFunctionObjArgs(set_exception, exc, nullptr);
        Py_XDECREF(r);
        Py_DECREF(set_exception);
    }
    
    Py_DECREF(exc);
    return future;
}

// 检查文件是否已关闭，如果是则返回带异常的 Future
inline PyObject* check_closed_and_return_future(bool is_running, int fd, 
                                                  PyObject* create_future_fn,
                                                  PyObject* loop = nullptr) {
    if (!is_running || fd == -1) {
        if (create_future_fn) {
            // 已初始化事件循环，使用缓存的 create_future
            PyObject* future = PyObject_CallNoArgs(create_future_fn);
            if (future) {
                resolve_exc_static(future, g_ValueError, 0, "I/O operation on closed file.");
            }
            return future;
        } else {
            // 未初始化事件循环，尝试创建
            return create_rejected_future(loop, g_ValueError, 
                                          "I/O operation on closed file.", 0);
        }
    }
    return nullptr;  // 文件未关闭
}

// 静态版本的 resolve_exc（不依赖 IOBackendBase）
inline void resolve_exc_static(PyObject* future, PyObject* cls, DWORD err, const char* msg) {
    PyObject* exc;
    if (err != 0) {
        exc = PyObject_CallFunction(cls, "is", (int)err, msg);
    } else {
        exc = PyObject_CallFunction(cls, "s", msg);
    }
    PyObject* fn = PyObject_GetAttr(future, g_str_set_exception);
    PyObject* r = PyObject_CallFunctionObjArgs(fn, exc, nullptr);
    Py_XDECREF(r);
    Py_DECREF(fn);
    Py_DECREF(exc);
}

// 静态版本的 resolve_ok
inline void resolve_ok_static(PyObject* future, PyObject* val) {
    PyObject* fn = PyObject_GetAttr(future, g_str_set_result);
    PyObject* r = PyObject_CallFunctionObjArgs(fn, val, nullptr);
    Py_XDECREF(r);
    Py_DECREF(fn);
}