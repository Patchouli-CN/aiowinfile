"""性能指标定义"""
from dataclasses import dataclass, field
from typing import List


@dataclass
class PerformanceMetrics:
    """性能指标"""
    name: str = ""
    concurrent_clients: int = 0
    total_operations: int = 0
    total_bytes: int = 0
    total_time: float = 0.0
    min_latency: float = float('inf')
    max_latency: float = 0.0
    avg_latency: float = 0.0
    p50_latency: float = 0.0
    p95_latency: float = 0.0
    p99_latency: float = 0.0
    error_count: int = 0
    timeout_count: int = 0
    cpu_usage: float = 0.0
    memory_usage: float = 0.0
    thread_count: int = 0
    handle_count: int = 0
    completed: bool = False
    raw_latencies: List[float] = field(default_factory=list)
    
    @property
    def error_rate(self) -> float:
        if self.total_operations == 0:
            return 0.0
        return self.error_count / self.total_operations * 100
    
    @property
    def ops_per_second(self) -> float:
        if self.total_time == 0:
            return 0.0
        return self.total_operations / self.total_time
    
    @property
    def mb_per_second(self) -> float:
        if self.total_time == 0:
            return 0.0
        return self.total_bytes / self.total_time / 1024 / 1024
    
    def to_dict(self):
        return {
            'name': self.name,
            'concurrent_clients': self.concurrent_clients,
            'total_operations': self.total_operations,
            'total_bytes_mb': self.total_bytes / 1024 / 1024,
            'total_time_seconds': self.total_time,
            'ops_per_second': self.ops_per_second,
            'mb_per_second': self.mb_per_second,
            'avg_latency_ms': self.avg_latency * 1000,
            'p50_latency_ms': self.p50_latency * 1000,
            'p95_latency_ms': self.p95_latency * 1000,
            'p99_latency_ms': self.p99_latency * 1000,
            'error_rate': self.error_rate,
            'cpu_usage': self.cpu_usage,
            'memory_usage_mb': self.memory_usage,
            'thread_count': self.thread_count,
            'handle_count': self.handle_count,
            'completed': self.completed
        }
    
    def calculate_percentiles(self):
        """计算百分位数"""
        if not self.raw_latencies:
            return
        
        sorted_lat = sorted(self.raw_latencies)
        self.min_latency = min(self.raw_latencies)
        self.max_latency = max(self.raw_latencies)
        self.avg_latency = sum(self.raw_latencies) / len(self.raw_latencies)
        
        n = len(sorted_lat)
        self.p50_latency = sorted_lat[int(n * 0.5)]
        self.p95_latency = sorted_lat[int(n * 0.95)]
        self.p99_latency = sorted_lat[int(n * 0.99)]