#include "iocp.hpp"
#include "file_handle.hpp"
#include "io_request.hpp"
#include <algorithm>

HANDLE                           g_iocp           = NULL;
std::atomic<bool>                g_iocpRunning{false};
std::atomic<bool>                g_ctrlcTriggered{false};
std::vector<std::thread>         g_iocpWorkers;
std::mutex                       g_openFilesMtx;
std::unordered_set<FileHandle*>  g_openFiles;

// ════════════════════════════════════════════════════════════════════════════
// §4  IOCP init / shutdown
// ════════════════════════════════════════════════════════════════════════════

void init_iocp() {
    SYSTEM_INFO si{}; GetSystemInfo(&si);
    // 优化：允许更多工作线程以处理高并发
    // 原来：min(CPU核心数, 4)
    // 现在：min(CPU核心数 * 2, 16)，但至少1个
    unsigned n = std::max(1u, std::min((unsigned)si.dwNumberOfProcessors * 2, 16u));
    g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (!g_iocp) throw std::runtime_error("Failed to create IOCP");
    g_iocpRunning.store(true, std::memory_order_release);
    g_iocpWorkers.reserve(n);
    for (unsigned i = 0; i < n; ++i) g_iocpWorkers.emplace_back(iocp_thread_proc);
}

void shutdown_iocp() {
    if (!g_iocp) return;
    g_iocpRunning.store(false, std::memory_order_release);
    for (size_t i = 0; i < g_iocpWorkers.size(); ++i)
        PostQueuedCompletionStatus(g_iocp, 0, 0, NULL);
    for (auto &t : g_iocpWorkers) if (t.joinable()) t.join();
    g_iocpWorkers.clear();
    CloseHandle(g_iocp); g_iocp = NULL;
}

BOOL WINAPI ctrl_handler(DWORD t) {
    if (t==CTRL_C_EVENT||t==CTRL_BREAK_EVENT||t==CTRL_CLOSE_EVENT||
        t==CTRL_LOGOFF_EVENT||t==CTRL_SHUTDOWN_EVENT) {
        g_ctrlcTriggered.store(true, std::memory_order_relaxed);
        close_all_files(); return TRUE;
    }
    return FALSE;
}

// ════════════════════════════════════════════════════════════════════════════
// §7  IOCP worker + close_all
// ════════════════════════════════════════════════════════════════════════════

void iocp_thread_proc() {
    while (true) {
        DWORD bytes=0; ULONG_PTR key=0; LPOVERLAPPED ov=nullptr;
        BOOL ok = GetQueuedCompletionStatus(g_iocp, &bytes, &key, &ov, INFINITE);
        if (!g_iocpRunning.load(std::memory_order_acquire) && !ov) break;
        if (!ov) continue;
        auto *req  = reinterpret_cast<IORequest*>(ov);
        auto *file = req->file;
        if (!file) { delete req; continue; }
        if (!ok) file->complete_error(req, GetLastError());
        else     file->complete_ok(req, bytes);
    }
}

void close_all_files() {
    std::vector<FileHandle*> snap;
    { std::lock_guard<std::mutex> lk(g_openFilesMtx); snap.assign(g_openFiles.begin(), g_openFiles.end()); }
    for (auto *f : snap) try { f->close_impl(); } catch (...) {}
}
