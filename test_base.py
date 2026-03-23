import ayafileio

try:
    ayafileio.set_iocp_worker_count(221) # must raise exception
except ValueError as e:
    print("passed")
    
max_per_key, max_total = ayafileio.get_handle_pool_limits()
print(f"句柄池配置：{max_per_key}, {max_total}")