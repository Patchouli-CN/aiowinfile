
# ayafileio

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Python Version](https://img.shields.io/badge/python-3.10+-blue.svg)](https://www.python.org/)
[![Platform](https://img.shields.io/badge/platform-Cross--platform-blue.svg)](https://en.wikipedia.org/wiki/Cross-platform)
[![Speed](https://img.shields.io/badge/speed-%E6%9C%80%E9%80%9F-red.svg)](#)

> **「幻想郷最速のファイルI/O、風神少女の如く」**  
> *—— 射命丸文，今日も全力で翔ける*

**跨平台异步文件 I/O 库，使用原生异步 I/O 机制。**  
Windows 上借 **IOCP**（I/O 完成端口）之力，Linux 上支持 **io_uring**（内核 5.1+）或线程池降级，实现真正无阻塞的文件操作。

## 📸 核心特性

| 特性 | 说明 |
|------|------|
| 🍃 **零线程开销** | 无需 `run_in_executor`，真异步平台无后台线程 |
| 📰 **内核级完成** | IOCP / io_uring 直达内核，无用户态调度延迟 |
| ⚡ **高并发友好** | 数千并发文件操作，文文团扇一挥间 |
| 🎴 **标准 API** | 熟悉的文件接口，支持 `async/await` |
| 📖 **文本二进制支持** | 文本模式自动编解码 |
| 🔧 **统一配置系统** | 运行时动态调整所有参数 |
| 🌍 **跨平台** | Windows / Linux / macOS 皆可翱翔 |

## 🛠️ 安装

```bash
pip install ayafileio
```

**系统要求：**
- Python 3.10+
- Windows 7 / Server 2008 R2 或更高版本，或 Linux (内核 5.1+ 可启用 io_uring)
- 无其他依赖

## 🚀 快速开始

```python
import asyncio
import ayafileio

async def main():
    # 写入文件——像风一样快
    async with ayafileio.open("example.txt", "w") as f:
        await f.write("Hello, async world!\n")

    # 读取并自动解码——文文新闻，一触即达
    async with ayafileio.open("example.txt", "r", encoding="utf-8") as f:
        content = await f.read()
        print(content)

    # 二进制操作——数据如风，来去无痕
    async with ayafileio.open("data.bin", "rb") as f:
        data = await f.read(1024)
        await f.seek(0, 0)

asyncio.run(main())
```

## ⚙️ 统一配置

`ayafileio` 提供统一的配置系统，所有参数可在运行时动态调整：

```python
import ayafileio

# 查看当前配置
config = ayafileio.get_config()
print(config)
# {
#     "handle_pool_max_per_key": 64,
#     "handle_pool_max_total": 2048,
#     "io_worker_count": 0,
#     "buffer_pool_max": 512,
#     "buffer_size": 65536,
#     "close_timeout_ms": 4000,
#     "io_uring_queue_depth": 256,
#     "io_uring_sqpoll": False,
#     "enable_debug_log": False
# }

# 修改配置——风势加强！
ayafileio.configure({
    "io_worker_count": 8,
    "buffer_size": 131072,      # 128KB 缓冲区
    "close_timeout_ms": 2000,
})

# 重置为默认值
ayafileio.reset_config()
```

### 配置项说明

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| `handle_pool_max_per_key` | 64 | 每个文件最大缓存句柄数 (Windows) |
| `handle_pool_max_total` | 2048 | 全局最大缓存句柄数 (Windows) |
| `io_worker_count` | 0 | I/O 工作线程数，0=自动 |
| `buffer_pool_max` | 512 | 最大缓存缓冲区数 |
| `buffer_size` | 65536 | 单个缓冲区大小 (字节) |
| `close_timeout_ms` | 4000 | 关闭时等待 pending I/O 的超时 (ms) |
| `io_uring_queue_depth` | 256 | io_uring 队列深度 (Linux) |
| `io_uring_sqpoll` | False | 是否启用 SQPOLL 模式 (Linux) |
| `enable_debug_log` | False | 是否启用调试日志 |

## 🔍 后端信息

查看当前使用的后端：

```python
info = ayafileio.get_backend_info()
print(info)
# Windows: {'platform': 'windows', 'backend': 'iocp', 'is_truly_async': True, ...}
# Linux (io_uring): {'platform': 'linux', 'backend': 'io_uring', 'is_truly_async': True, ...}
# Linux (fallback): {'platform': 'linux', 'backend': 'thread_pool', 'is_truly_async': False, ...}
# macOS: {'platform': 'macos', 'backend': 'thread_pool', 'is_truly_async': False, ...}
```

## 📚 API 参考

### AsyncFile 类

```python
class AsyncFile:
    def __init__(self, path: str | Path, mode: str = "rb", encoding: str | None = None): ...
    async def read(self, size: int = -1) -> str | bytes: ...
    async def write(self, data: str | bytes) -> int: ...
    async def seek(self, offset: int, whence: int = 0) -> int: ...
    async def flush(self) -> None: ...
    async def close(self) -> None: ...
    async def readline(self) -> str | bytes: ...
    def __aiter__(self) -> AsyncFile: ...
    async def __anext__(self) -> str | bytes: ...
```

### 支持的模式

| 模式 | 说明 |
|------|------|
| `"r"`, `"rb"` | 读取（文本/二进制） |
| `"w"`, `"wb"` | 写入（文本/二进制） |
| `"a"`, `"ab"` | 追加（文本/二进制） |
| `"x"`, `"xb"` | 独占创建（文本/二进制） |
| 加上 `"+"` | 读写组合 |

### 配置函数

```python
def configure(options: dict) -> None: ...      # 统一配置
def get_config() -> dict: ...                   # 获取当前配置
def reset_config() -> None: ...                 # 重置为默认值
def get_backend_info() -> dict: ...             # 获取后端信息

# 向后兼容（推荐使用上面的统一配置）
def set_handle_pool_limits(max_per_key: int, max_total: int) -> None: ...
def get_handle_pool_limits() -> tuple[int, int]: ...
def set_io_worker_count(count: int = 0) -> None: ...
```

## 🧪 性能对比

在同等环境下（测试时长 10 秒，随机读写混合），`ayafileio` 展现出了天狗般的速度：

| 并发数 | ayafileio | aiofiles | 优势 |
|--------|-----------|----------|------|
| 50 | 2,043 ops/s | 1,657 ops/s | **+23%** |
| 100 | 770 ops/s | 405 ops/s | **+90%** |
| 500 | 1,130 ops/s | 1,032 ops/s | **+9.5%** |

**P99 延迟对比（越低越好）：**
- 100 并发：ayafileio **33ms** vs aiofiles 562ms
- 200 并发：ayafileio **35ms** vs aiofiles 155ms

> *Windows 10 + 机械硬盘实测——即使是即将报废的硬件，文文依然能跑出速度。*

## 🤝 贡献

欢迎投稿「文文。新闻」！请：

1. Fork 本仓库
2. 创建功能分支（`git checkout -b feature/amazing-feature`）
3. 添加测试
4. 确保基准测试仍通过
5. 提交拉取请求

## 📄 许可证

MIT 许可证 —— **最速最自由**，详见 [LICENSE](LICENSE)。

---

**「遅いのは罪だぜ？」**  
*—— 射命丸文，『文文。新闻』主编*
