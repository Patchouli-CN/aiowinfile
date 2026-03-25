
# ayafileio

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Python Version](https://img.shields.io/badge/python-3.10+-blue.svg)](https://www.python.org/)
[![Platform](https://img.shields.io/badge/platform-Cross--platform-blue.svg)](https://en.wikipedia.org/wiki/Cross-platform)

> **"The fastest file I/O in Gensokyo, swift as the Wind God Maiden."**
> *— Aya Shameimaru, always flying at full speed*

**Cross-platform asynchronous file I/O library using native async I/O where available.**
On Windows it leverages IOCP (I/O Completion Ports); on Linux it uses a thread-based backend to simulate asynchronous behavior, delivering non-blocking file operations.

## 📸 Key Features

| Feature | Description |
|--------:|:------------|
| 🍃 **Zero thread overhead** | No need for `run_in_executor`; no background threads on Windows when using IOCP |
| 📰 **Kernel-level completion** | Eliminates user-space scheduling delays by relying on kernel completion events |
| ⚡ **High concurrency** | Handles thousands of concurrent file operations with low overhead |
| 🎴 **Familiar API** | Standard file-like interface with `async/await` support |
| 📖 **Text & binary support** | Automatic encoding/decoding in text modes |
| 🔧 **Configurable handle pool** | Tune handle reuse to optimize performance on Windows |
| 🌍 **Cross-platform** | Works on both Windows and Linux |

## 🛠️ Installation

```bash
pip install ayafileio
```

**System requirements:**
- Python 3.10+
- Windows 7 / Server 2008 R2 or newer, or Linux
- No external dependencies

## 🚀 Quick Start

```python
import asyncio
import ayafileio

async def main():
    # Write to a file — fast as the wind
    async with ayafileio.open("example.txt", "w") as f:
        await f.write("Hello, async world!\n")
        await f.flush()

    # Read with automatic decoding
    async with ayafileio.open("example.txt", "r", encoding="utf-8") as f:
        content = await f.read()
        print(content)  # "Hello, async world!\n"

    # Binary operations
    async with ayafileio.open("data.bin", "rb") as f:
        data = await f.read(1024)
        await f.seek(0, 0)  # Seek to start

asyncio.run(main())
```

## ⚙️ Advanced Configuration

### Handle pool tuning (Windows)

For high-concurrency workloads you can tune the handle pool to reuse file handles across open/close cycles:

```python
import ayafileio

# Inspect current limits
max_per_key, max_total = ayafileio.get_handle_pool_limits()
print(f"Current: {max_per_key} per key, {max_total} total")

# Increase limits to improve performance for many files
ayafileio.set_handle_pool_limits(128, 4096)
```

This reduces expensive `CreateFile` calls by reusing handles.

### I/O worker count (cross-platform)

```python
# Set number of I/O workers (on Windows this controls IOCP worker threads)
ayafileio.set_io_worker_count(8)  # 0 = auto, 1-128 = fixed
# Calling this on Linux will also work — it gracefully falls back to the platform backend
```

## 📚 API Reference

### `AsyncFile` class

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

### Supported modes

| Mode | Description |
|------|-------------|
| `"r"`, `"rb"` | Read (text/binary) |
| `"w"`, `"wb"` | Write (text/binary) |
| `"a"`, `"ab"` | Append (text/binary) |
| `"x"`, `"xb"` | Exclusive create (text/binary) |
| `+` added to a mode | Read/write combinations |

### Public functions

```python
def set_handle_pool_limits(max_per_key: int, max_total: int) -> None: ...
def get_handle_pool_limits() -> tuple[int, int]: ...
def set_io_worker_count(count: int = 0) -> None: ...  # cross-platform
```

## 🧪 Performance Comparison

Under comparable conditions (10s runs, mixed random reads/writes), `ayafileio` demonstrates high throughput:

| Concurrency | ayafileio | aiofiles | Advantage |
|------------:|----------:|---------:|:---------:|
| 50  | 2,043 ops/s | 1,657 ops/s | **+23%** |
| 100 | 770 ops/s   | 405 ops/s   | **+90%** |
| 500 | 1,130 ops/s | 1,032 ops/s | **+9.5%** |

P99 latency (lower is better):
- 100 concurrency: ayafileio **33ms** vs aiofiles 562ms
- 200 concurrency: ayafileio **35ms** vs aiofiles 155ms

> Measurements performed on Windows 10 with mechanical disk — ayafileio remains fast even on degraded hardware.

Run benchmarks:

```bash
git clone https://github.com/your-repo/ayafileio.git
cd ayafileio
python run_benchmark.py
```

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
- Thanks to Aya Shameimaru for the "fastest" spirit
- Thanks to everyone who ran tests on old hardware

---

**"Slow is a crime, right?"**
*— Aya Shameimaru, editor-in-chief of Bunbunmaru News*
