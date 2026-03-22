#pragma once
#include <windows.h>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <vector>
#include <thread>

// ════════════════════════════════════════════════════════════════════════════
// §4  IOCP globals
// ════════════════════════════════════════════════════════════════════════════

class FileHandle;

extern HANDLE                           g_iocp;
extern std::atomic<bool>                g_iocpRunning;
extern std::atomic<bool>                g_ctrlcTriggered;
extern std::vector<std::thread>         g_iocpWorkers;
extern std::mutex                       g_openFilesMtx;
extern std::unordered_set<FileHandle*>  g_openFiles;
extern std::atomic<unsigned>            g_iocp_worker_count;

void init_iocp();
void shutdown_iocp();
void set_iocp_worker_count(unsigned count);
void iocp_thread_proc();
void close_all_files();

BOOL WINAPI ctrl_handler(DWORD t);
