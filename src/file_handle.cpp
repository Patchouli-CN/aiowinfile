#include "file_handle.hpp"
#ifdef _WIN32
#include "backends/windows_io_backend.hpp"
#else
#include "backends/thread_io_backend.hpp"
#endif

FileHandle::FileHandle(const std::string &path, const std::string &mode) {
#ifdef _WIN32
    m_backend = new WindowsIOBackend(path, mode);
#else
    m_backend = new ThreadIOBackend(path, mode);
#endif
}

FileHandle::~FileHandle() {
    delete m_backend;
}