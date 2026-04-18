
# ayafileio

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Python Version](https://img.shields.io/badge/python-3.10+-blue.svg)](https://www.python.org/)
[![Platform](https://img.shields.io/badge/platform-Cross--platform-blue.svg)](https://en.wikipedia.org/wiki/Cross-platform)

> **"The fastest file I/O in Gensokyo, swift as the Wind God Maiden."**
> *— Aya Shameimaru, always flying at full speed*

**Cross-platform asynchronous file I/O library using native async I/O where available.**  
On Windows it leverages **IOCP** (I/O Completion Ports); on Linux it supports **io_uring** (kernel 5.1+) with automatic fallback to thread pool; on macOS it uses thread pool.

## 📸 Key Features

| Feature | Description |
|--------:|:------------|
| 🍃 **Zero thread overhead** | No `run_in_executor` needed; no background threads on true async platforms |
| 📰 **Kernel-level completion** | IOCP / io_uring provide true async I/O without user-space scheduling |
| ⚡ **High concurrency** | Handles thousands of concurrent file operations with low overhead |
| 🎴 **Familiar API** | Standard file-like interface with `async/await` support |
| 📖 **Text & binary support** | Automatic encoding/decoding in text modes |
| 🔧 **Unified configuration** | Runtime tunable parameters for all backends |
| 🌍 **Cross-platform** | Works on Windows, Linux, and macOS |

## 🛠️ Installation

```bash
pip install ayafileio
```

**System requirements:**
- Python 3.10+
- Windows 7 / Server 2008 R2 or newer, or Linux (kernel 5.1+ for io_uring), or macOS
- No external dependencies

## 🚀 Quick Start

```python
import asyncio
import ayafileio

async def main():
    # Write to a file — fast as the wind
    async with ayafileio.open("example.txt", "w") as f:
        await f.write("Hello, async world!\n")

    # Read with automatic decoding
    async with ayafileio.open("example.txt", "r", encoding="utf-8") as f:
        content = await f.read()
        print(content)

    # Binary operations
    async with ayafileio.open("data.bin", "rb") as f:
        data = await f.read(1024)
        await f.seek(0, 0)

asyncio.run(main())
```

## ⚙️ Unified Configuration

`ayafileio` provides a unified configuration system that allows runtime tuning of all parameters:

```python
import ayafileio

# View current configuration
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

# Update configuration
ayafileio.configure({
    "io_worker_count": 8,
    "buffer_size": 131072,      # 128KB buffer
    "close_timeout_ms": 2000,
})

# Reset to defaults
ayafileio.reset_config()
```

### Configuration Options

| Option | Default | Description |
|--------|---------|-------------|
| `handle_pool_max_per_key` | 64 | Max cached handles per file (Windows) |
| `handle_pool_max_total` | 2048 | Max total cached handles (Windows) |
| `io_worker_count` | 0 | IO worker threads, 0=auto |
| `buffer_pool_max` | 512 | Max cached buffers |
| `buffer_size` | 65536 | Buffer size in bytes |
| `close_timeout_ms` | 4000 | Close timeout for pending I/O (ms) |
| `io_uring_queue_depth` | 256 | io_uring queue depth (Linux) |
| `io_uring_sqpoll` | False | Enable SQPOLL mode (Linux) |
| `enable_debug_log` | False | Enable debug logging |

## 🔍 Backend Information

Check which backend is currently in use:

```python
info = ayafileio.get_backend_info()
print(info)
# Windows: {'platform': 'windows', 'backend': 'iocp', 'is_truly_async': True, ...}
# Linux (io_uring): {'platform': 'linux', 'backend': 'io_uring', 'is_truly_async': True, ...}
# Linux (fallback): {'platform': 'linux', 'backend': 'thread_pool', 'is_truly_async': False, ...}
# macOS: {'platform': 'macos', 'backend': 'thread_pool', 'is_truly_async': False, ...}
```

## 📚 API Reference

### AsyncFile class

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

### Supported Modes

| Mode | Description |
|------|-------------|
| `"r"`, `"rb"` | Read (text/binary) |
| `"w"`, `"wb"` | Write (text/binary) |
| `"a"`, `"ab"` | Append (text/binary) |
| `"x"`, `"xb"` | Exclusive create (text/binary) |
| `+` added | Read/write combinations |

### Configuration Functions

```python
def configure(options: dict) -> None: ...      # Unified configuration
def get_config() -> dict: ...                   # Get current configuration
def reset_config() -> None: ...                 # Reset to defaults
def get_backend_info() -> dict: ...             # Get backend information

# Backward compatible (prefer unified configuration above)
def set_handle_pool_limits(max_per_key: int, max_total: int) -> None: ...
def get_handle_pool_limits() -> tuple[int, int]: ...
def set_io_worker_count(count: int = 0) -> None: ...
```

## 🧪 Performance Comparison

Under comparable conditions (10s runs, mixed random reads/writes), `ayafileio` demonstrates high throughput:

| Concurrency | ayafileio | aiofiles | Advantage |
|------------:|----------:|---------:|:---------:|
| 50  | 2,043 ops/s | 1,657 ops/s | **+23%** |
| 100 | 770 ops/s   | 405 ops/s   | **+90%** |
| 500 | 1,130 ops/s | 1,032 ops/s | **+9.5%** |

**P99 latency (lower is better):**
- 100 concurrency: ayafileio **33ms** vs aiofiles 562ms
- 200 concurrency: ayafileio **35ms** vs aiofiles 155ms

> Measurements performed on Windows 10 with mechanical disk — ayafileio remains fast even on degraded hardware.

## 🤝 Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Add tests
4. Ensure benchmarks pass
5. Open a pull request

## 📄 License

MIT License — see [LICENSE](LICENSE) for details.

## 🙏 Acknowledgments

- Thanks to Windows IOCP for providing true asynchronous I/O
- Thanks to Linux io_uring for next-generation async I/O
- Thanks to Aya Shameimaru for the "fastest" spirit

---

**"Slow is a crime, right?"**
*— Aya Shameimaru, editor-in-chief of Bunbunmaru News*
