import setuptools
from pybind11.setup_helpers import Pybind11Extension, build_ext

ext_modules = [
    Pybind11Extension(
        "aiowinfile._aiowinfile",
        [
            "src/globals.cpp",
            "src/pool.cpp",
            "src/loop_handle.cpp",
            "src/iocp.cpp",
            "src/file_handle.cpp",
            "src/handle_pool.cpp",
            "src/bindings.cpp",
        ],
        define_macros=[
            ("WIN32_LEAN_AND_MEAN", None),
            ("NOMINMAX", None),
            ("_WIN32_WINNT", "0x0601"),  # Windows 7+
        ],
        extra_compile_args=["/utf-8", "/O2", "/GL", "/W3", "/arch:AVX2"],
        extra_link_args=["/LTCG"],
        cxx_std=17,
    )
]

setuptools.setup(
    name="aiowinfile",
    version="0.2.0",
    author="aiowinfile",
    description="Async Windows IOCP file API for Python",
    packages=["aiowinfile"],
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
    zip_safe=False,
)
