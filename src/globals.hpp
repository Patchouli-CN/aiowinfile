#pragma once
#define PY_SSIZE_T_CLEAN
#include <pybind11/pybind11.h>
#ifdef _WIN32
#include <windows.h>
#endif

namespace py = pybind11;

// ════════════════════════════════════════════════════════════════════════════
// §1  Cached CPython globals
// ════════════════════════════════════════════════════════════════════════════

extern PyObject *g_OSError;
extern PyObject *g_FileNotFoundError;
extern PyObject *g_PermissionError;
extern PyObject *g_ValueError;
extern PyObject *g_KeyboardInterrupt;
extern PyObject *g_get_running_loop;

extern PyObject *g_str_set_result;
extern PyObject *g_str_set_exception;
extern PyObject *g_str_create_future;
extern PyObject *g_str_call_soon_ts;

void cache_globals();

#ifdef _WIN32
static inline PyObject *map_win_error(DWORD err) {
    switch (err) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:    return g_FileNotFoundError;
        case ERROR_ACCESS_DENIED:
        case ERROR_WRITE_PROTECT:
        case ERROR_SHARING_VIOLATION: return g_PermissionError;
        default:                      return g_OSError;
    }
}

[[noreturn]] void throw_os_error(DWORD err, const char *msg,
                                  const char *filename = nullptr);
#endif
