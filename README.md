
# 📄 `aiowinfile` – True Async File I/O on Windows (via IOCP)

> **Real kernel-level async file operations — no thread pools, no faking.  
> Tested: 5000 files opened, written, and closed in ~1.04s on a HDD.**

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

---

## 🌐 中文 | [README_EN.md](English)

### 简介

`aiowinfile` 是一个为 **Windows** 打造的 Python 异步文件 I/O 库。它**不使用线程池**，而是直接调用 Windows 内核的 **I/O 完成端口（IOCP）**，实现**真正的非阻塞异步文件操作**。

与 `aiofiles` 等基于线程池的“伪异步”方案不同，`aiowinfile`：
- ✅ **零线程开销**：不占用 asyncio 线程池
- ✅ **由内核直接驱动**：完成通知无用户态调度延迟
- ✅ **高并发友好**：轻松处理数千文件并发操作

---

### 🔧 实测性能（你的机器）

- **环境**：Windows 10  
- **CPU**：Intel i7-6700K  
- **内存**：32GB DDR4  
- **磁盘**：HDD (WDC WD10EZEX)  
- **结果**：  
  ```text
  # this result is running on test.py
  Opened/Wrote/Closed 5000 files in 1.039s
  Opened/Wrote/Closed 5000 files in 1.044s
  ```

> 💡 即使在**机械硬盘（HDD）** 上，也能高效完成 5000 次完整文件生命周期操作（打开 → 写入 → 关闭），平均 **~0.2ms/文件**。

---

### 安装

```bash
pip install aiowinfile
```

> 需要 Python 3.10+，仅支持 Windows（7 / Server 2008 R2 及以上）

---

### 快速使用

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

API 设计兼容标准 `open()`，迁移成本极低。

---

### 注意事项

- ❌ 不支持 Linux / macOS（本库深度依赖 Windows IOCP）
- ✅ 支持文本/二进制模式、所有常见文件操作（`read`, `write`, `seek` 等）
- ⚠️ 文件路径需为本地磁盘（网络路径未测试）

---

### 开源协议

MIT License – 免费用于任何项目。

---
