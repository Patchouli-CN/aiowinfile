/*
 * bindings.cpp  –  pybind11 module entry point
 */
#include "globals.hpp"
#include "iocp.hpp"
#include "file_handle.hpp"
#include "handle_pool.hpp"

// ════════════════════════════════════════════════════════════════════════════
// §8  pybind11 bindings
// ════════════════════════════════════════════════════════════════════════════

struct PyAsyncIOCPFile {
    FileHandle *fh;
    explicit PyAsyncIOCPFile(const std::string &path, const std::string &mode)
        : fh(new FileHandle(path, mode)) {}
    ~PyAsyncIOCPFile() { delete fh; }

    py::object read(int64_t size = -1) {
        PyObject *r = fh->read(size);
        if (!r) throw py::error_already_set();
        return py::reinterpret_steal<py::object>(r);
    }
    py::object write(py::object data) {
        Py_buffer view{};
        if (PyObject_GetBuffer(data.ptr(), &view, PyBUF_SIMPLE) < 0)
            throw py::error_already_set();
        PyObject *r = fh->write(&view);
        PyBuffer_Release(&view);
        if (!r) throw py::error_already_set();
        return py::reinterpret_steal<py::object>(r);
    }
    py::object seek(int64_t offset, int whence = 0) {
        PyObject *r = fh->seek(offset, whence);
        if (!r) throw py::error_already_set();
        return py::reinterpret_steal<py::object>(r);
    }
    py::object flush() {
        PyObject *r = fh->flush();
        if (!r) throw py::error_already_set();
        return py::reinterpret_steal<py::object>(r);
    }
    py::object close() {
        PyObject *r = fh->close();
        if (!r) throw py::error_already_set();
        return py::reinterpret_steal<py::object>(r);
    }
    void close_impl() { fh->close_impl(); }
};

PYBIND11_MODULE(_aiowinfile, m) {
    m.doc() = "Async IOCP file operations for Windows";

    cache_globals();
    init_iocp();

    auto atexit = py::module_::import("atexit");
    atexit.attr("register")(py::cpp_function([]() {
        close_all_files();
        handle_pool_drain();
        shutdown_iocp();
    }));

    py::class_<PyAsyncIOCPFile>(m, "AsyncFile")
        .def(py::init<const std::string &, const std::string &>(),
             py::arg("path"), py::arg("mode") = "rb")
        .def("read",  &PyAsyncIOCPFile::read,  py::arg("size") = -1)
        .def("write", &PyAsyncIOCPFile::write)
        .def("seek",  &PyAsyncIOCPFile::seek,  py::arg("offset"), py::arg("whence") = 0)
        .def("flush", &PyAsyncIOCPFile::flush)
        .def("close", &PyAsyncIOCPFile::close)
        .def("close_impl", &PyAsyncIOCPFile::close_impl);

    m.def("set_handle_pool_limits", &set_handle_pool_limits,
        "Set handle pool max_per_key and max_total",
        py::arg("max_per_key"), py::arg("max_total"));

    m.def("get_handle_pool_limits", &get_handle_pool_limits,
        "Get current handle pool limits as (max_per_key, max_total)");
}

