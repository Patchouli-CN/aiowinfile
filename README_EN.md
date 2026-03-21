
## aiowinfile

### Introduction

`aiowinfile` is a **true asynchronous file I/O library for Python on Windows**. It bypasses thread pools entirely and uses Windows’ native **I/O Completion Ports (IOCP)** to deliver **kernel-driven, non-blocking file operations**.

Unlike `aiofiles` (which wraps blocking I/O in threads), `aiowinfile`:
- ✅ **Zero thread overhead** – no `run_in_executor`, no background threads  
- ✅ **Kernel-level completion** – no user-space scheduling delay  
- ✅ **Scales to high concurrency** – handle thousands of files effortlessly  

---

### 🚀 Real-World Performance (Your Machine)

- **OS**: Windows 10  
- **CPU**: Intel i7-6700K  
- **RAM**: 32GB DDR4  
- **Disk**: HDD (WDC WD10EZEX)  
- **Result**:  
  ```text
  Opened/Wrote/Closed 5000 files in 1.039s
  Opened/Wrote/Closed 5000 files in 1.044s
  ```

> 💡 Even on a **mechanical hard drive (HDD)**, it completes 5000 full file lifecycle operations (open → write → close) in just over **1 second** — averaging **~0.2ms per file**.

---

### Installation

```bash
pip install aiowinfile
```

> Requires Python 3.8+, **Windows only** (7 / Server 2008 R2 or later)

---

### Quick Start

```python
import asyncio
import aiowinfile

async def main():
    async with aiowinfile.open("hello.txt", "w") as f:
        await f.write("True async I/O on Windows!\n")

    async with aiowinfile.open("hello.txt", "r") as f:
        print(await f.read())

asyncio.run(main())
```

API closely matches Python’s built-in `open()` — easy to adopt.

---

### Caveats

- ❌ **Not cross-platform** – Windows-only (relies on IOCP)
- ✅ Supports text/binary modes and common ops (`read`, `write`, `seek`, etc.)
- ⚠️ Local disk paths only (network/UNC paths untested)

---

### License

MIT License – free for any use.
