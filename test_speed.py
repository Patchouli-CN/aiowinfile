#!/usr/bin/env python3
"""
ayafileio vs aiofiles 专业基准测试
包含延迟分布、抖动分析、配置调优对比
"""

import asyncio
import tempfile
import time
import json
import os
import io
import sys
import statistics
import platform
from pathlib import Path
from dataclasses import dataclass, field

# Rich 美化输出
try:
    from rich.console import Console
    from rich.table import Table
    from rich.panel import Panel
    from rich.progress import Progress, SpinnerColumn, TextColumn, BarColumn, TaskProgressColumn
    from rich.layout import Layout
    from rich.live import Live
    from rich import box
    from rich.text import Text
    from rich.columns import Columns
    RICH_AVAILABLE = True
except ImportError:
    RICH_AVAILABLE = False
    print("提示: 安装 rich 可获得更好的输出效果: pip install rich")

if sys.platform == "win32":
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace')

# 检查依赖
try:
    import aiofiles
except ImportError:
    print("错误：请先安装 aiofiles: pip install aiofiles")
    sys.exit(1)

try:
    import ayafileio
except ImportError:
    print("错误：请先安装 ayafileio")
    sys.exit(1)

# 初始化 Rich Console
console = Console() if RICH_AVAILABLE else None


# ════════════════════════════════════════════════════════════════════════════
# 配置
# ════════════════════════════════════════════════════════════════════════════

@dataclass
class Config:
    """测试配置"""
    # KeyValueStore 场景：模拟截图/PDF 存储
    screenshot_size: int = 512 * 1024      # 512KB
    screenshot_count: int = 100
    
    # Dataset 场景：模拟爬取结果追加写入
    item_size: int = 1024                  # 1KB
    item_count: int = 5000
    
    # 并发配置
    concurrent_limit: int = 50
    
    # 测试轮数
    warmup_rounds: int = 2
    test_rounds: int = 5
    
    # 是否启用 ayafileio 性能调优
    enable_tuning: bool = True
    
    # 调优模式: "auto", "aggressive", "conservative", "none"
    tuning_mode: str = "auto"


# ════════════════════════════════════════════════════════════════════════════
# 平台自适应调优
# ════════════════════════════════════════════════════════════════════════════

def get_platform_info() -> dict:
    """获取详细平台信息"""
    info = {
        "system": sys.platform,
        "is_linux": sys.platform == "linux",
        "is_windows": sys.platform == "win32",
        "is_macos": sys.platform == "darwin",
    }
    
    try:
        info["cpu_count"] = os.cpu_count() or 4
    except:
        info["cpu_count"] = 4
    
    try:
        if sys.platform == "linux":
            with open("/proc/meminfo", "r") as f:
                for line in f:
                    if line.startswith("MemTotal"):
                        info["mem_kb"] = int(line.split()[1])
                        break
        elif sys.platform == "win32":
            import ctypes
            kernel32 = ctypes.windll.kernel32
            class MEMORYSTATUSEX(ctypes.Structure):
                _fields_ = [
                    ("dwLength", ctypes.c_ulong),
                    ("dwMemoryLoad", ctypes.c_ulong),
                    ("ullTotalPhys", ctypes.c_ulonglong),
                    ("ullAvailPhys", ctypes.c_ulonglong),
                    ("ullTotalPageFile", ctypes.c_ulonglong),
                    ("ullAvailPageFile", ctypes.c_ulonglong),
                    ("ullTotalVirtual", ctypes.c_ulonglong),
                    ("ullAvailVirtual", ctypes.c_ulonglong),
                    ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
                ]
            memoryStatus = MEMORYSTATUSEX()
            memoryStatus.dwLength = ctypes.sizeof(MEMORYSTATUSEX)
            if kernel32.GlobalMemoryStatusEx(ctypes.byref(memoryStatus)):
                info["mem_bytes"] = memoryStatus.ullTotalPhys
        elif sys.platform == "darwin":
            import subprocess
            result = subprocess.run(["sysctl", "-n", "hw.memsize"], capture_output=True, text=True)
            if result.returncode == 0:
                info["mem_bytes"] = int(result.stdout.strip())
    except:
        pass
    
    return info


def apply_platform_tuning(config: Config):
    """根据平台和调优模式应用最优配置"""
    platform_info = get_platform_info()
    backend_info = ayafileio.get_backend_info()
    
    if console:
        console.print(Panel(
            f"[bold cyan]平台信息[/bold cyan]\n"
            f"  系统: {platform_info['system']}\n"
            f"  后端: {backend_info['backend']}\n"
            f"  CPU: {platform_info.get('cpu_count', '?')} 核心\n"
            f"  调优模式: {config.tuning_mode}",
            title="🔧 平台自适应调优",
            border_style="cyan"
        ))
    else:
        print(f"\n🔧 平台自适应调优:")
        print(f"   - 系统: {platform_info['system']}")
        print(f"   - 后端: {backend_info['backend']}")
        print(f"   - CPU: {platform_info.get('cpu_count', '?')} 核心")
    
    tuning_config = {}
    
    if config.tuning_mode == "none":
        if console:
            console.print("[dim]调优模式: 无 (使用默认配置)[/dim]")
        else:
            print("   - 调优模式: 无 (使用默认配置)")
        return
    
    if config.tuning_mode == "aggressive":
        if console:
            console.print("[bold yellow]调优模式: 激进 (追求极致性能)[/bold yellow]")
        else:
            print("   - 调优模式: 激进 (追求极致性能)")
        tuning_config["buffer_size"] = 1024 * 1024
        tuning_config["buffer_pool_max"] = 2048
        tuning_config["close_timeout_ms"] = 5000
        
        if platform_info["is_linux"]:
            tuning_config["io_uring_queue_depth"] = 1024
            tuning_config["io_uring_sqpoll"] = True
        
    elif config.tuning_mode == "conservative":
        if console:
            console.print("[bold green]调优模式: 保守 (稳定性优先)[/bold green]")
        else:
            print("   - 调优模式: 保守 (稳定性优先)")
        tuning_config["buffer_size"] = 64 * 1024
        tuning_config["buffer_pool_max"] = 256
        tuning_config["close_timeout_ms"] = 4000
        
    else:  # "auto"
        if console:
            console.print("[bold green]调优模式: 自动[/bold green]")
        else:
            print("   - 调优模式: 自动")
        
        if backend_info["backend"] == "iocp":
            tuning_config["buffer_size"] = 512 * 1024
            tuning_config["buffer_pool_max"] = 1024
            tuning_config["close_timeout_ms"] = 3000
            if console:
                console.print("[dim]     - Windows IOCP: 大缓冲区模式 (512KB)[/dim]")
            
        elif backend_info["backend"] == "io_uring":
            tuning_config["buffer_size"] = 256 * 1024
            tuning_config["buffer_pool_max"] = 1024
            tuning_config["io_uring_queue_depth"] = 512
            tuning_config["io_uring_sqpoll"] = False
            tuning_config["close_timeout_ms"] = 4000
            if console:
                console.print("[dim]     - Linux io_uring: 批量提交模式 (队列深度=512)[/dim]")
            
        elif backend_info["backend"] == "dispatch_io":
            tuning_config["buffer_size"] = 256 * 1024
            tuning_config["buffer_pool_max"] = 512
            tuning_config["close_timeout_ms"] = 4000
            if console:
                console.print("[dim]     - macOS Dispatch I/O: 标准模式[/dim]")
            
        else:
            cpu_count = platform_info.get("cpu_count", 4)
            tuning_config["io_worker_count"] = min(cpu_count * 2, 16)
            tuning_config["buffer_size"] = 128 * 1024
            tuning_config["buffer_pool_max"] = 512
            if console:
                console.print(f"[dim]     - 线程池模式: worker数={tuning_config['io_worker_count']}[/dim]")
    
    if tuning_config:
        try:
            ayafileio.configure(tuning_config)
            if console:
                config_text = ", ".join(f"{k}={v}" for k, v in tuning_config.items())
                console.print(f"[green]✅ 已应用配置: {config_text}[/green]")
            else:
                print(f"   ✅ 已应用配置: {tuning_config}")
        except Exception as e:
            if console:
                console.print(f"[red]⚠️ 配置应用失败: {e}[/red]")
            else:
                print(f"   ⚠️ 配置应用失败: {e}")


def apply_ayafileio_tuning(config: Config):
    """应用 ayafileio 性能调优配置（兼容旧接口）"""
    if not config.enable_tuning:
        if console:
            console.print("[dim]  ⚙️  ayafileio 使用默认配置[/dim]")
        else:
            print("  ⚙️  ayafileio 使用默认配置")
        return
    
    apply_platform_tuning(config)


# ════════════════════════════════════════════════════════════════════════════
# 统计数据类
# ════════════════════════════════════════════════════════════════════════════

@dataclass
class BenchmarkStats:
    """基准测试统计数据"""
    name: str
    values: list[float] = field(default_factory=list)
    
    @property
    def count(self) -> int:
        return len(self.values)
    
    @property
    def mean(self) -> float:
        return statistics.mean(self.values) if self.values else 0
    
    @property
    def median(self) -> float:
        return statistics.median(self.values) if self.values else 0
    
    @property
    def stdev(self) -> float:
        return statistics.stdev(self.values) if len(self.values) > 1 else 0
    
    @property
    def p95(self) -> float:
        if not self.values:
            return 0
        sorted_vals = sorted(self.values)
        idx = int(len(sorted_vals) * 0.95)
        return sorted_vals[min(idx, len(sorted_vals) - 1)]
    
    @property
    def p99(self) -> float:
        if not self.values:
            return 0
        sorted_vals = sorted(self.values)
        idx = int(len(sorted_vals) * 0.99)
        return sorted_vals[min(idx, len(sorted_vals) - 1)]
    
    @property
    def min_val(self) -> float:
        return min(self.values) if self.values else 0
    
    @property
    def max_val(self) -> float:
        return max(self.values) if self.values else 0
    
    @property
    def jitter(self) -> float:
        return (self.stdev / self.mean * 100) if self.mean > 0 else 0
    
    @property
    def range_ratio(self) -> float:
        return (self.max_val / self.min_val) if self.min_val > 0 else 0
    
    def to_dict(self) -> dict:
        return {
            "count": self.count,
            "mean": self.mean,
            "median": self.median,
            "stdev": self.stdev,
            "p95": self.p95,
            "p99": self.p99,
            "min": self.min_val,
            "max": self.max_val,
            "jitter_percent": self.jitter,
            "range_ratio": self.range_ratio,
            "raw_values": self.values,
        }
    
    def get_color(self) -> str:
        """根据性能返回颜色"""
        if self.mean < 0.05:
            return "green"
        elif self.mean < 0.1:
            return "yellow"
        return "red"


@dataclass
class ComparisonResult:
    """对比结果"""
    library: str
    stats: BenchmarkStats
    throughput: float = 0
    
    def speedup_vs(self, other: "ComparisonResult") -> float:
        return other.stats.mean / self.stats.mean if self.stats.mean > 0 else 0


# ════════════════════════════════════════════════════════════════════════════
# 辅助函数
# ════════════════════════════════════════════════════════════════════════════

def generate_screenshot_data(size: int) -> bytes:
    return os.urandom(size)


def generate_json_item(size: int) -> dict:
    item = {
        "url": f"https://example.com/page/{os.urandom(4).hex()}",
        "title": f"Page Title {os.urandom(8).hex()}",
        "timestamp": time.time(),
        "data": os.urandom(size - 200).hex()[:size - 200],
    }
    return item


def format_duration(seconds: float) -> str:
    """格式化时间显示"""
    if seconds < 0.001:
        return f"{seconds * 1000:.2f}ms"
    elif seconds < 1:
        return f"{seconds * 1000:.1f}ms"
    return f"{seconds:.3f}s"


# ════════════════════════════════════════════════════════════════════════════
# 场景 1：KeyValueStore 写入
# ════════════════════════════════════════════════════════════════════════════

async def benchmark_kvs_write(
    library: str,
    temp_dir: Path,
    data_list: list,
    config: Config,
) -> float:
    semaphore = asyncio.Semaphore(config.concurrent_limit)
    
    if library == "ayafileio":
        async def write_one(i: int, data: bytes):
            async with semaphore:
                path = temp_dir / f"file_{i}.bin"
                async with ayafileio.open(path, "wb") as f:
                    await f.write(data)
    else:
        async def write_one(i: int, data: bytes):
            async with semaphore:
                path = temp_dir / f"file_{i}.bin"
                async with aiofiles.open(path, "wb") as f:
                    await f.write(data)
    
    start = time.perf_counter()
    tasks = [write_one(i, data) for i, data in enumerate(data_list)]
    await asyncio.gather(*tasks)
    elapsed = time.perf_counter() - start
    
    await asyncio.sleep(0.05)
    return elapsed


# ════════════════════════════════════════════════════════════════════════════
# 场景 2：KeyValueStore 读取
# ════════════════════════════════════════════════════════════════════════════

async def benchmark_kvs_read(
    library: str,
    temp_dir: Path,
    count: int,
    config: Config,
) -> float:
    semaphore = asyncio.Semaphore(config.concurrent_limit)
    
    if library == "ayafileio":
        async def read_one(i: int):
            async with semaphore:
                path = temp_dir / f"file_{i}.bin"
                async with ayafileio.open(path, "rb") as f:
                    return await f.read()
    else:
        async def read_one(i: int):
            async with semaphore:
                path = temp_dir / f"file_{i}.bin"
                async with aiofiles.open(path, "rb") as f:
                    return await f.read()
    
    start = time.perf_counter()
    tasks = [read_one(i) for i in range(count)]
    await asyncio.gather(*tasks)
    elapsed = time.perf_counter() - start
    
    await asyncio.sleep(0.05)
    return elapsed


# ════════════════════════════════════════════════════════════════════════════
# 场景 3：Dataset 追加写入
# ════════════════════════════════════════════════════════════════════════════

async def benchmark_dataset_write_detailed(
    library: str,
    path: Path,
    items: list,
    config: Config,
) -> tuple[float, list[float]]:
    semaphore = asyncio.Semaphore(config.concurrent_limit)
    write_latencies: list[float] = []
    
    if library == "ayafileio":
        async def write_batch(batch: list):
            async with semaphore:
                async with ayafileio.open(path, "a", encoding="utf-8") as f:
                    for item in batch:
                        line = json.dumps(item, ensure_ascii=False) + "\n"
                        w_start = time.perf_counter()
                        await f.write(line)
                        w_end = time.perf_counter()
                        write_latencies.append((w_end - w_start) * 1000)
    else:
        async def write_batch(batch: list):
            async with semaphore:
                async with aiofiles.open(path, "a", encoding="utf-8") as f:
                    for item in batch:
                        line = json.dumps(item, ensure_ascii=False) + "\n"
                        w_start = time.perf_counter()
                        await f.write(line)
                        w_end = time.perf_counter()
                        write_latencies.append((w_end - w_start) * 1000)
    
    batch_size = len(items) // 10
    batches = [items[i:i+batch_size] for i in range(0, len(items), batch_size)]
    
    start = time.perf_counter()
    await asyncio.gather(*[write_batch(batch) for batch in batches])
    elapsed = time.perf_counter() - start
    
    await asyncio.sleep(0.05)
    return elapsed, write_latencies


# ════════════════════════════════════════════════════════════════════════════
# 场景 4：混合读写
# ════════════════════════════════════════════════════════════════════════════

async def benchmark_mixed_workload(
    library: str,
    temp_dir: Path,
    read_count: int,
    write_data: list,
    config: Config,
) -> float:
    semaphore = asyncio.Semaphore(config.concurrent_limit)
    
    if library == "ayafileio":
        async def read_one(i: int):
            async with semaphore:
                path = temp_dir / f"existing_{i}.bin"
                async with ayafileio.open(path, "rb") as f:
                    return await f.read()
        
        async def write_one(i: int, data: bytes):
            async with semaphore:
                path = temp_dir / f"mixed_{i}.bin"
                async with ayafileio.open(path, "wb") as f:
                    await f.write(data)
    else:
        async def read_one(i: int):
            async with semaphore:
                path = temp_dir / f"existing_{i}.bin"
                async with aiofiles.open(path, "rb") as f:
                    return await f.read()
        
        async def write_one(i: int, data: bytes):
            async with semaphore:
                path = temp_dir / f"mixed_{i}.bin"
                async with aiofiles.open(path, "wb") as f:
                    await f.write(data)
    
    read_tasks = [read_one(i) for i in range(read_count)]
    write_tasks = [write_one(i, data) for i, data in enumerate(write_data)]
    all_tasks = read_tasks + write_tasks
    
    import random
    random.shuffle(all_tasks)
    
    start = time.perf_counter()
    await asyncio.gather(*all_tasks)
    elapsed = time.perf_counter() - start
    
    await asyncio.sleep(0.05)
    return elapsed


# ════════════════════════════════════════════════════════════════════════════
# Rich 表格渲染
# ════════════════════════════════════════════════════════════════════════════

def create_result_table(
    info: dict,
    aya_write: ComparisonResult,
    aio_write: ComparisonResult,
    aya_read: ComparisonResult,
    aio_read: ComparisonResult,
    aya_dataset: ComparisonResult,
    aio_dataset: ComparisonResult,
    aya_mixed: ComparisonResult,
    aio_mixed: ComparisonResult,
    write_latencies: dict,
    speedup_write: float,
    speedup_read: float,
    speedup_dataset: float,
    speedup_mixed: float,
) -> Table:
    """创建 Rich 结果表格"""
    table = Table(title="📊 测试结果对比", box=box.ROUNDED, header_style="bold cyan")
    
    table.add_column("场景", style="cyan", no_wrap=True)
    table.add_column("指标", style="dim")
    table.add_column("ayafileio", justify="right")
    table.add_column("aiofiles", justify="right")
    table.add_column("对比", justify="center")
    
    # 写入测试
    table.add_row(
        "KeyValueStore\n写入",
        "中位数",
        f"[{aya_write.stats.get_color()}]{format_duration(aya_write.stats.median)}[/]",
        format_duration(aio_write.stats.median),
        f"[{'green' if speedup_write > 1 else 'red'}]{speedup_write:.2f}x[/]"
    )
    table.add_row(
        "",
        "抖动",
        f"[{'green' if aya_write.stats.jitter < aio_write.stats.jitter else 'red'}]{aya_write.stats.jitter:.1f}%[/]",
        f"{aio_write.stats.jitter:.1f}%",
        "✅ 更稳" if aya_write.stats.jitter < aio_write.stats.jitter else "❌"
    )
    table.add_row(
        "",
        "P99",
        format_duration(aya_write.stats.p99),
        format_duration(aio_write.stats.p99),
        ""
    )
    
    table.add_row("", "", "", "", "")
    
    # 读取测试
    table.add_row(
        "KeyValueStore\n读取",
        "中位数",
        f"[{aya_read.stats.get_color()}]{format_duration(aya_read.stats.median)}[/]",
        format_duration(aio_read.stats.median),
        f"[{'green' if speedup_read > 1 else 'red'}]{speedup_read:.2f}x[/]"
    )
    table.add_row(
        "",
        "抖动",
        f"[{'green' if aya_read.stats.jitter < aio_read.stats.jitter else 'red'}]{aya_read.stats.jitter:.1f}%[/]",
        f"{aio_read.stats.jitter:.1f}%",
        "✅ 更稳" if aya_read.stats.jitter < aio_read.stats.jitter else "❌"
    )
    
    table.add_row("", "", "", "", "")
    
    # Dataset 追加写入
    table.add_row(
        "Dataset\n追加写入",
        "中位数",
        f"[{aya_dataset.stats.get_color()}]{format_duration(aya_dataset.stats.median)}[/]",
        format_duration(aio_dataset.stats.median),
        f"[{'green' if speedup_dataset > 1 else 'red'}]{speedup_dataset:.2f}x[/]"
    )
    table.add_row(
        "",
        "吞吐量",
        f"[green]{aya_dataset.throughput:.0f}[/] 条/秒",
        f"{aio_dataset.throughput:.0f} 条/秒",
        f"[green]{speedup_dataset:.2f}x[/]"
    )
    table.add_row(
        "",
        "单次write\nP99延迟",
        f"[green]{write_latencies['ayafileio'].p99:.3f}ms[/]",
        f"{write_latencies['aiofiles'].p99:.3f}ms",
        ""
    )
    
    table.add_row("", "", "", "", "")
    
    # 混合读写
    table.add_row(
        "混合读写",
        "中位数",
        f"[{aya_mixed.stats.get_color()}]{format_duration(aya_mixed.stats.median)}[/]",
        format_duration(aio_mixed.stats.median),
        f"[{'green' if speedup_mixed > 1 else 'red'}]{speedup_mixed:.2f}x[/]"
    )
    table.add_row(
        "",
        "抖动",
        f"[{'green' if aya_mixed.stats.jitter < aio_mixed.stats.jitter else 'red'}]{aya_mixed.stats.jitter:.1f}%[/]",
        f"{aio_mixed.stats.jitter:.1f}%",
        "✅ 更稳" if aya_mixed.stats.jitter < aio_mixed.stats.jitter else "❌"
    )
    
    return table


def create_header_panel(info: dict, config: Config, total_size_mb: float) -> Panel:
    """创建头部信息面板"""
    content = (
        f"[bold cyan]平台:[/bold cyan] {info['platform']}\n"
        f"[bold cyan]后端:[/bold cyan] {info['backend']} [dim](真异步: {info['is_truly_async']})[/dim]\n"
        f"[bold cyan]调优模式:[/bold cyan] {config.tuning_mode}\n"
        f"[bold cyan]测试数据:[/bold cyan] {config.screenshot_count} 个截图 ({total_size_mb:.1f} MB) + {config.item_count} 条记录\n"
        f"[bold cyan]并发数:[/bold cyan] {config.concurrent_limit} | [bold cyan]测试轮数:[/bold cyan] {config.test_rounds}"
    )
    return Panel(content, title="🎯 ayafileio vs aiofiles 基准测试", border_style="cyan")


# ════════════════════════════════════════════════════════════════════════════
# 运行完整基准测试
# ════════════════════════════════════════════════════════════════════════════

async def run_benchmark(config: Config) -> dict:
    """运行完整基准测试"""
    if console:
        console.clear()
        console.print(create_header_panel({}, config, 0))
    else:
        print("=" * 80)
        print("ayafileio vs aiofiles 专业基准测试")
        print("=" * 80)
    
    # 显示后端信息
    info = ayafileio.get_backend_info()
    
    # 应用性能调优
    apply_ayafileio_tuning(config)
    
    if console:
        console.print(f"\n[bold]⚙️  测试配置:[/bold]")
        console.print(f"   - 截图文件大小: {config.screenshot_size // 1024} KB")
        console.print(f"   - 截图文件数量: {config.screenshot_count}")
        console.print(f"   - Dataset 每条大小: ~{config.item_size} bytes")
        console.print(f"   - Dataset 条数: {config.item_count}")
        console.print(f"   - 最大并发: {config.concurrent_limit}")
    else:
        print(f"\n⚙️  测试配置:")
        print(f"   - 截图文件大小: {config.screenshot_size // 1024} KB")
        print(f"   - 截图文件数量: {config.screenshot_count}")
        print(f"   - Dataset 每条大小: ~{config.item_size} bytes")
        print(f"   - Dataset 条数: {config.item_count}")
    
    # 准备测试数据
    if console:
        console.print("\n[bold cyan]📦 准备测试数据...[/bold cyan]")
    else:
        print("\n📦 准备测试数据...")
    
    screenshot_data = [generate_screenshot_data(config.screenshot_size) 
                       for _ in range(config.screenshot_count)]
    total_size_mb = (config.screenshot_size * config.screenshot_count) / (1024 * 1024)
    
    if console:
        console.print(f"[green]✅ 生成了 {config.screenshot_count} 个模拟截图文件 (总计 {total_size_mb:.1f} MB)[/green]")
    else:
        print(f"   生成了 {config.screenshot_count} 个模拟截图文件 (总计 {total_size_mb:.1f} MB)")
    
    dataset_items = [generate_json_item(config.item_size) 
                     for _ in range(config.item_count)]
    
    existing_count = 50
    existing_data = [generate_screenshot_data(config.screenshot_size) 
                     for _ in range(existing_count)]
    
    results = {
        "platform": info["platform"],
        "backend": info["backend"],
        "is_truly_async": info["is_truly_async"],
        "tuning_enabled": config.enable_tuning,
        "tuning_mode": config.tuning_mode,
        "config": {
            "screenshot_size_kb": config.screenshot_size // 1024,
            "screenshot_count": config.screenshot_count,
            "item_size_bytes": config.item_size,
            "item_count": config.item_count,
            "concurrent_limit": config.concurrent_limit,
            "warmup_rounds": config.warmup_rounds,
            "test_rounds": config.test_rounds,
        },
        "benchmarks": {}
    }
    
    # ════════════════════════════════════════════════════════════════════════
    # 场景 1：KeyValueStore 写入
    # ════════════════════════════════════════════════════════════════════════
    if console:
        console.print("\n[bold cyan]📊 场景 1：KeyValueStore 写入[/bold cyan]")
        console.print("[dim]模拟截图/PDF 存储[/dim]")
    else:
        print("\n" + "─" * 80)
        print("📊 场景 1：KeyValueStore 写入 (模拟截图/PDF 存储)")
        print("─" * 80)
    
    kvs_write_results = {}
    
    for lib in ["ayafileio", "aiofiles"]:
        times = []
        for round_num in range(config.test_rounds + config.warmup_rounds):
            with tempfile.TemporaryDirectory() as tmpdir:
                tmp_path = Path(tmpdir)
                elapsed = await benchmark_kvs_write(lib, tmp_path, screenshot_data, config)
                if round_num >= config.warmup_rounds:
                    times.append(elapsed)
                    if console:
                        console.print(f"  {lib:10} 第{round_num - config.warmup_rounds + 1}轮: {format_duration(elapsed)}")
                    else:
                        print(f"  {lib:10} 第{round_num - config.warmup_rounds + 1}轮: {elapsed:.4f}s")
        
        stats = BenchmarkStats(name=f"{lib}_kvs_write", values=times)
        throughput = total_size_mb / stats.mean if stats.mean > 0 else 0
        kvs_write_results[lib] = ComparisonResult(library=lib, stats=stats, throughput=throughput)
    
    aya_write = kvs_write_results["ayafileio"]
    aio_write = kvs_write_results["aiofiles"]
    speedup_write = aya_write.speedup_vs(aio_write)
    
    if console:
        console.print(f"\n  📈 [cyan]ayafileio[/cyan]: 中位数 {format_duration(aya_write.stats.median)}, 抖动 {aya_write.stats.jitter:.1f}%, P99 {format_duration(aya_write.stats.p99)}")
        console.print(f"  📈 [dim]aiofiles[/dim]:   中位数 {format_duration(aio_write.stats.median)}, 抖动 {aio_write.stats.jitter:.1f}%, P99 {format_duration(aio_write.stats.p99)}")
        console.print(f"  🚀 [{'green' if speedup_write > 1 else 'red'}]{'提速' if speedup_write > 1 else '减速'}: {speedup_write:.2f}x[/]")
    else:
        print(f"\n  📈 ayafileio: 中位数 {aya_write.stats.median:.4f}s, 抖动 {aya_write.stats.jitter:.1f}%, P99 {aya_write.stats.p99:.4f}s")
        print(f"  📈 aiofiles:  中位数 {aio_write.stats.median:.4f}s, 抖动 {aio_write.stats.jitter:.1f}%, P99 {aio_write.stats.p99:.4f}s")
        print(f"  🚀 提速: {speedup_write:.2f}x")
    
    results["benchmarks"]["kvs_write"] = {
        "ayafileio": aya_write.stats.to_dict(),
        "aiofiles": aio_write.stats.to_dict(),
        "speedup": speedup_write,
        "throughput_aya_mbps": aya_write.throughput,
        "throughput_aio_mbps": aio_write.throughput,
    }
    
    # ════════════════════════════════════════════════════════════════════════
    # 场景 2：KeyValueStore 读取
    # ════════════════════════════════════════════════════════════════════════
    if console:
        console.print("\n[bold cyan]📊 场景 2：KeyValueStore 读取[/bold cyan]")
    else:
        print("\n" + "─" * 80)
        print("📊 场景 2：KeyValueStore 读取")
        print("─" * 80)
    
    kvs_read_results = {}
    
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp_path = Path(tmpdir)
        for i, data in enumerate(screenshot_data):
            (tmp_path / f"file_{i}.bin").write_bytes(data)
        
        for lib in ["ayafileio", "aiofiles"]:
            times = []
            for round_num in range(config.test_rounds + config.warmup_rounds):
                elapsed = await benchmark_kvs_read(lib, tmp_path, config.screenshot_count, config)
                if round_num >= config.warmup_rounds:
                    times.append(elapsed)
                    if console:
                        console.print(f"  {lib:10} 第{round_num - config.warmup_rounds + 1}轮: {format_duration(elapsed)}")
                    else:
                        print(f"  {lib:10} 第{round_num - config.warmup_rounds + 1}轮: {elapsed:.4f}s")
            
            stats = BenchmarkStats(name=f"{lib}_kvs_read", values=times)
            throughput = total_size_mb / stats.mean if stats.mean > 0 else 0
            kvs_read_results[lib] = ComparisonResult(library=lib, stats=stats, throughput=throughput)
    
    aya_read = kvs_read_results["ayafileio"]
    aio_read = kvs_read_results["aiofiles"]
    speedup_read = aya_read.speedup_vs(aio_read)
    
    if console:
        console.print(f"\n  📈 [cyan]ayafileio[/cyan]: 中位数 {format_duration(aya_read.stats.median)}, 抖动 {aya_read.stats.jitter:.1f}%, P99 {format_duration(aya_read.stats.p99)}")
        console.print(f"  📈 [dim]aiofiles[/dim]:   中位数 {format_duration(aio_read.stats.median)}, 抖动 {aio_read.stats.jitter:.1f}%, P99 {format_duration(aio_read.stats.p99)}")
        console.print(f"  🚀 [{'green' if speedup_read > 1 else 'red'}]{'提速' if speedup_read > 1 else '减速'}: {speedup_read:.2f}x[/]")
    else:
        print(f"\n  📈 ayafileio: 中位数 {aya_read.stats.median:.4f}s, 抖动 {aya_read.stats.jitter:.1f}%, P99 {aya_read.stats.p99:.4f}s")
        print(f"  📈 aiofiles:  中位数 {aio_read.stats.median:.4f}s, 抖动 {aio_read.stats.jitter:.1f}%, P99 {aio_read.stats.p99:.4f}s")
        print(f"  🚀 提速: {speedup_read:.2f}x")
    
    results["benchmarks"]["kvs_read"] = {
        "ayafileio": aya_read.stats.to_dict(),
        "aiofiles": aio_read.stats.to_dict(),
        "speedup": speedup_read,
        "throughput_aya_mbps": aya_read.throughput,
        "throughput_aio_mbps": aio_read.throughput,
    }
    
    # ════════════════════════════════════════════════════════════════════════
    # 场景 3：Dataset 追加写入
    # ════════════════════════════════════════════════════════════════════════
    if console:
        console.print("\n[bold cyan]📊 场景 3：Dataset 追加写入[/bold cyan]")
        console.print("[dim]详细延迟分析[/dim]")
    else:
        print("\n" + "─" * 80)
        print("📊 场景 3：Dataset 追加写入 (详细延迟分析)")
        print("─" * 80)
    
    dataset_results = {}
    write_latencies = {}
    
    for lib in ["ayafileio", "aiofiles"]:
        times = []
        all_latencies = []
        
        for round_num in range(config.test_rounds + config.warmup_rounds):
            with tempfile.TemporaryDirectory() as tmpdir:
                tmp_path = Path(tmpdir) / "dataset.jsonl"
                elapsed, latencies = await benchmark_dataset_write_detailed(lib, tmp_path, dataset_items, config)
                if round_num >= config.warmup_rounds:
                    times.append(elapsed)
                    all_latencies.extend(latencies)
                    if console:
                        console.print(f"  {lib:10} 第{round_num - config.warmup_rounds + 1}轮: {format_duration(elapsed)}")
                    else:
                        print(f"  {lib:10} 第{round_num - config.warmup_rounds + 1}轮: {elapsed:.4f}s")
        
        stats = BenchmarkStats(name=f"{lib}_dataset", values=times)
        latency_stats = BenchmarkStats(name=f"{lib}_write_latency_ms", values=all_latencies)
        throughput = config.item_count / stats.mean if stats.mean > 0 else 0
        
        dataset_results[lib] = ComparisonResult(library=lib, stats=stats, throughput=throughput)
        write_latencies[lib] = latency_stats
        
        if console:
            console.print(f"\n  📝 [cyan]{lib}[/cyan] 单次 write 延迟:")
            console.print(f"      中位数: [green]{latency_stats.median:.3f}ms[/green], P95: {latency_stats.p95:.3f}ms, P99: {latency_stats.p99:.3f}ms")
            console.print(f"      抖动: {latency_stats.jitter:.1f}%, 极差比: {latency_stats.range_ratio:.1f}x")
        else:
            print(f"\n  📝 {lib} 单次 write 延迟 (毫秒):")
            print(f"      中位数: {latency_stats.median:.3f}ms, P95: {latency_stats.p95:.3f}ms, P99: {latency_stats.p99:.3f}ms")
            print(f"      抖动: {latency_stats.jitter:.1f}%, 极差比: {latency_stats.range_ratio:.1f}x")
    
    aya_dataset = dataset_results["ayafileio"]
    aio_dataset = dataset_results["aiofiles"]
    speedup_dataset = aya_dataset.speedup_vs(aio_dataset)
    
    if console:
        console.print(f"\n  📈 [cyan]总体:[/cyan]")
        console.print(f"  🚀 [{'green' if speedup_dataset > 1 else 'red'}]{'提速' if speedup_dataset > 1 else '减速'}: {speedup_dataset:.2f}x[/]")
        console.print(f"  📝 吞吐量: [green]{aya_dataset.throughput:.0f}[/green] 条/秒 vs {aio_dataset.throughput:.0f} 条/秒")
    else:
        print(f"\n  📈 总体:")
        print(f"  🚀 提速: {speedup_dataset:.2f}x")
        print(f"  📝 吞吐量: ayafileio {aya_dataset.throughput:.0f} 条/秒, aiofiles {aio_dataset.throughput:.0f} 条/秒")
    
    results["benchmarks"]["dataset_write"] = {
        "ayafileio": aya_dataset.stats.to_dict(),
        "aiofiles": aio_dataset.stats.to_dict(),
        "speedup": speedup_dataset,
        "items_per_sec_aya": aya_dataset.throughput,
        "items_per_sec_aio": aio_dataset.throughput,
        "write_latency_ms": {
            "ayafileio": write_latencies["ayafileio"].to_dict(),
            "aiofiles": write_latencies["aiofiles"].to_dict(),
        }
    }
    
    # ════════════════════════════════════════════════════════════════════════
    # 场景 4：混合读写
    # ════════════════════════════════════════════════════════════════════════
    if console:
        console.print("\n[bold cyan]📊 场景 4：混合读写[/bold cyan]")
    else:
        print("\n" + "─" * 80)
        print("📊 场景 4：混合读写")
        print("─" * 80)
    
    mixed_results = {}
    
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp_path = Path(tmpdir)
        for i, data in enumerate(existing_data):
            (tmp_path / f"existing_{i}.bin").write_bytes(data)
        
        for lib in ["ayafileio", "aiofiles"]:
            times = []
            for round_num in range(config.test_rounds + config.warmup_rounds):
                elapsed = await benchmark_mixed_workload(
                    lib, tmp_path, existing_count, screenshot_data[:30], config
                )
                if round_num >= config.warmup_rounds:
                    times.append(elapsed)
                    if console:
                        console.print(f"  {lib:10} 第{round_num - config.warmup_rounds + 1}轮: {format_duration(elapsed)}")
                    else:
                        print(f"  {lib:10} 第{round_num - config.warmup_rounds + 1}轮: {elapsed:.4f}s")
            
            stats = BenchmarkStats(name=f"{lib}_mixed", values=times)
            mixed_results[lib] = ComparisonResult(library=lib, stats=stats, throughput=0)
    
    aya_mixed = mixed_results["ayafileio"]
    aio_mixed = mixed_results["aiofiles"]
    speedup_mixed = aya_mixed.speedup_vs(aio_mixed)
    
    if console:
        console.print(f"\n  📈 [cyan]ayafileio[/cyan]: 中位数 {format_duration(aya_mixed.stats.median)}, 抖动 {aya_mixed.stats.jitter:.1f}%")
        console.print(f"  📈 [dim]aiofiles[/dim]:   中位数 {format_duration(aio_mixed.stats.median)}, 抖动 {aio_mixed.stats.jitter:.1f}%")
        console.print(f"  🚀 [{'green' if speedup_mixed > 1 else 'red'}]{'提速' if speedup_mixed > 1 else '减速'}: {speedup_mixed:.2f}x[/]")
    else:
        print(f"\n  📈 ayafileio: 中位数 {aya_mixed.stats.median:.4f}s, 抖动 {aya_mixed.stats.jitter:.1f}%")
        print(f"  📈 aiofiles:  中位数 {aio_mixed.stats.median:.4f}s, 抖动 {aio_mixed.stats.jitter:.1f}%")
        print(f"  🚀 提速: {speedup_mixed:.2f}x")
    
    results["benchmarks"]["mixed_workload"] = {
        "ayafileio": aya_mixed.stats.to_dict(),
        "aiofiles": aio_mixed.stats.to_dict(),
        "speedup": speedup_mixed,
    }
    
    # ════════════════════════════════════════════════════════════════════════
    # 汇总输出
    # ════════════════════════════════════════════════════════════════════════
    if console:
        console.print("\n")
        table = create_result_table(
            info, aya_write, aio_write,
            aya_read, aio_read,
            aya_dataset, aio_dataset,
            aya_mixed, aio_mixed,
            write_latencies,
            speedup_write, speedup_read, speedup_dataset, speedup_mixed
        )
        console.print(table)
        
        # 更新头部面板
        console.print(create_header_panel(info, config, total_size_mb))
    else:
        print("\n" + "=" * 90)
        print("📊 测试结果汇总")
        print("=" * 90)
        print(f"""
┌───────────────────────────────────────────────────────────────────────────────────────┐
│                                    测试结果对比                                         │
├───────────────────────────────────────────────────────────────────────────────────────┤
│ 平台: {info['platform']:<10}  ayafileio 后端: {info['backend']:<12}  真异步: {info['is_truly_async']} │
│ 调优: {config.tuning_mode} 模式                                                         │
├───────────────────────────────────────────────────────────────────────────────────────┤
│ 场景                     │ 指标              │ ayafileio  │ aiofiles   │ 对比         │
├───────────────────────────────────────────────────────────────────────────────────────┤
│ KeyValueStore 写入       │ 中位数 (s)        │ {aya_write.stats.median:8.4f} │ {aio_write.stats.median:8.4f} │ {speedup_write:5.2f}x     │
│                          │ 抖动 (%)          │ {aya_write.stats.jitter:8.1f} │ {aio_write.stats.jitter:8.1f} │ {'✅ 更稳' if aya_write.stats.jitter < aio_write.stats.jitter else '❌'}      │
│                          │ P99 (s)           │ {aya_write.stats.p99:8.4f} │ {aio_write.stats.p99:8.4f} │            │
├───────────────────────────────────────────────────────────────────────────────────────┤
│ KeyValueStore 读取       │ 中位数 (s)        │ {aya_read.stats.median:8.4f} │ {aio_read.stats.median:8.4f} │ {speedup_read:5.2f}x     │
│                          │ 抖动 (%)          │ {aya_read.stats.jitter:8.1f} │ {aio_read.stats.jitter:8.1f} │ {'✅ 更稳' if aya_read.stats.jitter < aio_read.stats.jitter else '❌'}      │
├───────────────────────────────────────────────────────────────────────────────────────┤
│ Dataset 追加写入         │ 中位数 (s)        │ {aya_dataset.stats.median:8.4f} │ {aio_dataset.stats.median:8.4f} │ {speedup_dataset:5.2f}x     │
│                          │ 吞吐量 (条/秒)    │ {aya_dataset.throughput:8.0f} │ {aio_dataset.throughput:8.0f} │ {speedup_dataset:5.2f}x     │
│                          │ 单次write P99(ms) │ {write_latencies['ayafileio'].p99:8.3f} │ {write_latencies['aiofiles'].p99:8.3f} │            │
├───────────────────────────────────────────────────────────────────────────────────────┤
│ 混合读写                 │ 中位数 (s)        │ {aya_mixed.stats.median:8.4f} │ {aio_mixed.stats.median:8.4f} │ {speedup_mixed:5.2f}x     │
└───────────────────────────────────────────────────────────────────────────────────────┘
""")
    
    # 保存结果
    output_file = Path("benchmark_results_detailed.json")
    with open(output_file, "w", encoding="utf-8") as f:
        json.dump(results, f, indent=2, ensure_ascii=False)
    
    if console:
        console.print(f"\n[dim]📁 详细结果已保存到: {output_file}[/dim]")
    else:
        print(f"\n📁 详细结果已保存到: {output_file}")
    
    return results


def main():
    """主函数"""
    import argparse
    
    parser = argparse.ArgumentParser(description="ayafileio 性能基准测试")
    parser.add_argument("--tuning", choices=["auto", "aggressive", "conservative", "none"],
                        default="aggressive", help="调优模式 (默认: aggressive)")
    parser.add_argument("--rounds", type=int, default=5, help="测试轮数")
    parser.add_argument("--items", type=int, default=5000, help="Dataset 条目数")
    parser.add_argument("--no-rich", action="store_true", help="禁用 Rich 美化输出")
    
    args = parser.parse_args()
    
    # 如果禁用 rich 或 rich 不可用，使用普通输出
    global console
    if args.no_rich or not RICH_AVAILABLE:
        console = None
    
    if sys.platform == "win32":
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
    else:
        try:
            loop = asyncio.new_event_loop()
            asyncio.set_event_loop(loop)
        except Exception:
            pass
    
    config = Config(
        test_rounds=args.rounds,
        item_count=args.items,
        tuning_mode=args.tuning,
        enable_tuning=(args.tuning != "none")
    )
    
    try:
        results = asyncio.run(run_benchmark(config))
        return results
    except KeyboardInterrupt:
        if console:
            console.print("\n\n[yellow]⚠️ 测试被用户中断[/yellow]")
        else:
            print("\n\n⚠️  测试被用户中断")
        return None


if __name__ == "__main__":
    main()