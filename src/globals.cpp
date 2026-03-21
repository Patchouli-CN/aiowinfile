#include "globals.hpp"

PyObject *g_OSError           = nullptr;
PyObject *g_FileNotFoundError = nullptr;
PyObject *g_PermissionError   = nullptr;
PyObject *g_ValueError        = nullptr;
PyObject *g_KeyboardInterrupt = nullptr;
PyObject *g_get_running_loop  = nullptr;

PyObject *g_str_set_result    = nullptr;
PyObject *g_str_set_exception = nullptr;
PyObject *g_str_create_future = nullptr;
PyObject *g_str_call_soon_ts  = nullptr;

void cache_globals() {
    auto *builtins = PyImport_ImportModule("builtins");
    g_OSError           = PyObject_GetAttrString(builtins, "OSError");
    g_FileNotFoundError = PyObject_GetAttrString(builtins, "FileNotFoundError");
    g_PermissionError   = PyObject_GetAttrString(builtins, "PermissionError");
    g_ValueError        = PyObject_GetAttrString(builtins, "ValueError");
    g_KeyboardInterrupt = PyObject_GetAttrString(builtins, "KeyboardInterrupt");
    Py_DECREF(builtins);

    auto *asyncio = PyImport_ImportModule("asyncio");
    g_get_running_loop = PyObject_GetAttrString(asyncio, "get_running_loop");
    Py_DECREF(asyncio);

    g_str_set_result    = PyUnicode_InternFromString("set_result");
    g_str_set_exception = PyUnicode_InternFromString("set_exception");
    g_str_create_future = PyUnicode_InternFromString("create_future");
    g_str_call_soon_ts  = PyUnicode_InternFromString("call_soon_threadsafe");
}

[[noreturn]] void throw_os_error(DWORD err, const char *msg, const char *filename) {
    PyObject *cls = map_win_error(err);
    PyObject *exc = filename
        ? PyObject_CallFunction(cls, "iss", (int)err, msg, filename)
        : PyObject_CallFunction(cls, "is",  (int)err, msg);
    PyErr_SetObject((PyObject *)Py_TYPE(exc), exc);
    Py_DECREF(exc);
    throw py::error_already_set();
}
