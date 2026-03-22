# setup.py
from setuptools import setup, Extension
import pybind11
import sys
from pathlib import Path

# 自动扫描所有 .cpp 文件
src_dir = Path(__file__).parent / 'src'
sources = [str(p.relative_to(Path.cwd())) for p in src_dir.glob('*.cpp')]

setup(
    name='aiowinfile',
    version='0.1.1',
    description='Async Windows IOCP file API for Python',
    author='Patchouli-CN',
    author_email='3072252442@qq.com',
    license='MIT',
    packages=['aiowinfile'],
    ext_modules=[
        Extension(
            'aiowinfile._aiowinfile',
            sources=sources,
            include_dirs=[
                pybind11.get_include(),
                str(src_dir),
            ],
            extra_compile_args=['/std:c++17', '/O2'] if sys.platform == 'win32' else ['-std:c++17', '-O3'],
            libraries=['ws2_32'] if sys.platform == 'win32' else [],
        )
    ],
    python_requires='>=3.10',
)