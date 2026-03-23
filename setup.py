# setup.py
from setuptools import setup, Extension
import pybind11
import sys
from pathlib import Path

# Auto-scan all .cpp files recursively under src/
src_dir = Path('src')
sources = [str(p) for p in src_dir.rglob('*.cpp') 
       if not ((sys.platform != 'win32' and 'windows' in p.name) or 
           (sys.platform == 'win32' and 'linux' in p.name))]

setup(
    name='ayafileio',
    version='0.1.1',
    description='Cross-platform async file API for Python',
    author='Patchouli-CN',
    author_email='3072252442@qq.com',
    license='MIT',
    packages=['ayafileio'],
    ext_modules=[
        Extension(
            'ayafileio._ayafileio',
            sources=sources,
            include_dirs=[
                pybind11.get_include(),
                str(src_dir),
            ],
            extra_compile_args=['/std:c++17', '/O2'] if sys.platform == 'win32' else ['-std:c++17', '-O3'],
            libraries=['ws2_32'] if sys.platform == 'win32' else [],
        )
    ],
    # 重要：包含包数据
    include_package_data=True,
    package_data={
        'ayafileio': ['*.pyd', '*.pyi'],  # 包含编译好的扩展和类型提示
    },
    # 排除不需要的文件
    exclude_package_data={
        '': ['*.cpp', '*.hpp', '*.pyc', '__pycache__'],  # 打包时排除源码
    },
    python_requires='>=3.10',
    # 确保 wheel 是平台特定的
    zip_safe=False,
)