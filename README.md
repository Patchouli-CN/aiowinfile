
# ayafileio
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Python Version](https://img.shields.io/badge/python-3.10+-blue.svg)](https://www.python.org/)
[![Platform](https://img.shields.io/badge/platform-Cross--platform-blue.svg)](https://en.wikipedia.org/wiki/Cross-platform)
[![Speed](https://img.shields.io/badge/speed-%E6%9C%80%E9%80%9F-red.svg)](#)

> **「幻想郷最速のファイルI/O、風神少女の如く」**  
> *—— 射命丸文，今日も全力で翔ける*

**跨平台异步文件 I/O 库，使用原生异步 I/O 机制。**  
Windows 上借 **IOCP**（I/O 完成端口）之力，Linux 上以线程池模拟异步，实现真正无阻塞的文件操作。

## 📸 核心特性

| 特性 | 说明 |
|------|------|
| 🍃 **零线程开销** | 无需 `run_in_executor`，无后台线程 (Windows) |
| 📰 **内核级完成** | 无用户态调度延迟，直达内核 |
| ⚡ **高并发友好** | 数千并发文件操作，文文团扇一挥间 |
| 🎴 **标准 API** | 熟悉的文件接口，支持 `async/await` |
| 📖 **文本二进制支持** | 文本模式自动编解码 |
| 🔧 **可配置句柄池** | 根据工作负载调整性能 (Windows) |
| 🌍 **跨平台** | Windows / Linux 皆可翱翔 |

## 🛠️ 安装

```bash
pip install ayafileio
```

**系统要求：**
- Python 3.10+（幻想郷的科技也要与时俱进）
- Windows 7 / Server 2008 R2 或更高版本，或 Linux
- 无其他依赖（自带团扇，无需外援）

## 🚀 快速开始

```python
import asyncio
import ayafileio

async def main():
    # 写入文件——像风一样快
    async with ayafileio.open("example.txt", "w") as f:
        await f.write("Hello, async world!\n")
        await f.flush()

    # 读取并自动解码——文文新闻，一触即达
    async with ayafileio.open("example.txt", "r", encoding="utf-8") as f:
        content = await f.read()
        print(content)  # "Hello, async world!\n"

    # 二进制操作——数据如风，来去无痕
    async with ayafileio.open("data.bin", "rb") as f:
        data = await f.read(1024)
        await f.seek(0, 0)  # 风神少女，回首即原点

asyncio.run(main())
```

## ⚙️ 高级配置

### 句柄池调优（Windows）

高并发工作负载时，调整句柄池大小，让文件句柄如天狗之羽，收放自如：

```python
import ayafileio

# 查看当前限制
max_per_key, max_total = ayafileio.get_handle_pool_limits()
print(f"当前: 每键 {max_per_key}，总计 {max_total}")

# 增加以提升多文件性能——风势加强！
ayafileio.set_handle_pool_limits(128, 4096)
```

这会在打开/关闭周期中重用文件句柄，减少昂贵的 `CreateFile` 调用——毕竟，天狗从不做无谓的起落。

### I/O 工作线程数（跨平台）

```python
# 设置 I/O worker 数量（Windows 上为 IOCP 线程数）
ayafileio.set_io_worker_count(8)  # 0 = 自动，1-128 = 手动
# Linux 上调用此方法也不会报错——优雅降级，是风神的风格
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
| 加上 `"+"` | 读写组合，风神双持 |

### 函数

```python
def set_handle_pool_limits(max_per_key: int, max_total: int) -> None: ...
def get_handle_pool_limits() -> tuple[int, int]: ...
def set_io_worker_count(count: int = 0) -> None: ...  # 跨平台通用
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

> *数据基于 Windows 10 + 机械硬盘（C5=12, C6=45）实测——即使是即将报废的硬件，文文依然能跑出速度。*

运行基准测试：
```bash
git clone https://github.com/your-repo/ayafileio.git
cd ayafileio
python run_benchmark.py
```

## 🤝 贡献

欢迎投稿「文文。新闻」！请：

1. Fork 本仓库
2. 创建功能分支（`git checkout -b feature/amazing-feature`）
3. 添加测试（天狗也要认真对待）
4. 确保基准测试仍通过
5. 提交拉取请求

## 📄 许可证

MIT 许可证 —— **最速最自由**，详见 [LICENSE](LICENSE)。

## 🙏 致谢

- 感谢 Windows IOCP 提供的真异步之力
- 感谢射命丸文带来的「最速」精神
- 感谢所有在坏盘上测试的勇者们

---

**「遅いのは罪だぜ？」**  
*—— 射命丸文，『文文。新闻』主编*
