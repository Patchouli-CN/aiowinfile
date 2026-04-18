#pragma once
#include <Python.h>
#include <nanobind/nanobind.h>

#ifdef _WIN32
#include <windows.h>
#endif
#ifndef _WIN32
#include <cstdint>
using DWORD = uint32_t;
using HANDLE = int;
static constexpr HANDLE INVALID_HANDLE_VALUE = (HANDLE)-1;
static inline void CloseHandle(HANDLE) { (void)0; }
#endif
#include <atomic>

namespace py = nanobind;

// ════════════════════════════════════════════════════════════════════════════
// §1  Cached CPython globals
// ════════════════════════════════════════════════════════════════════════════

extern PyObject *g_OSError;
extern PyObject *g_FileNotFoundError;
extern PyObject *g_PermissionError;
extern PyObject *g_ValueError;
extern PyObject *g_KeyboardInterrupt;
extern PyObject *g_FileExistsError;
extern PyObject *g_get_running_loop;

extern PyObject *g_str_set_result;
extern PyObject *g_str_set_exception;
extern PyObject *g_str_create_future;
extern PyObject *g_str_call_soon_ts;

// 全局可配置的 IO worker count（跨平台）。0 = 自动
extern std::atomic<unsigned> g_worker_count;

// 设置全局 worker count（暴露给绑定）
void set_worker_count(unsigned count);

void cache_globals();

#ifdef _WIN32
static inline PyObject *map_win_error(DWORD err) {
    switch (err) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:    return g_FileNotFoundError;
        case ERROR_FILE_EXISTS:       return g_FileExistsError;
        case ERROR_ACCESS_DENIED:
        case ERROR_WRITE_PROTECT:
        case ERROR_SHARING_VIOLATION: return g_PermissionError;
        default:                      return g_OSError;
    }
}

[[noreturn]] void win_throw_os_error(DWORD err, const char *msg,
                                  const char *filename = nullptr);
#endif
