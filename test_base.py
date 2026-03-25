import ayafileio
import sys
import asyncio
import tempfile
import os


failures = 0

def check(cond: bool, msg: str) -> None:
    global failures
    if cond:
        print("OK:", msg)
    else:
        print("FAIL:", msg)
        failures += 1


def test_worker_validation() -> None:
    # invalid value must raise
    try:
        ayafileio.set_io_worker_count(221)
        check(False, "set_io_worker_count(221) should raise ValueError")
    except ValueError:
        check(True, "set_io_worker_count validation")

    # valid values should not raise
    try:
        ayafileio.set_io_worker_count(0)
        ayafileio.set_io_worker_count(4)
        check(True, "set_io_worker_count accepts 0 and 4")
    except Exception as e:
        check(False, f"set_io_worker_count raised unexpectedly: {e}")


def test_handle_pool_limits() -> None:
    try:
        old = ayafileio.get_handle_pool_limits()
    except Exception as e:
        check(False, f"get_handle_pool_limits raised: {e}")
        return

    try:
        ayafileio.set_handle_pool_limits(2, 16)
        cur = ayafileio.get_handle_pool_limits()
        check(cur == (2, 16), f"set/get_handle_pool_limits roundtrip, got {cur}")
    except Exception as e:
        check(False, f"set_handle_pool_limits raised: {e}")
    finally:
        # restore
        try:
            ayafileio.set_handle_pool_limits(old[0], old[1])
        except Exception:
            pass


async def _io_roundtrip(path: str) -> bool:
    try:
        async with ayafileio.open(path, "w", encoding="utf-8") as f:
            await f.write("hello-async")
            await f.flush()

        async with ayafileio.open(path, "r", encoding="utf-8") as f:
            data = await f.read()
        return data == "hello-async"
    except Exception:
        return False


def test_basic_io() -> None:
    tf = tempfile.NamedTemporaryFile(delete=False)
    tf.close()
    path = tf.name
    try:
        ok = asyncio.run(_io_roundtrip(path))
        check(ok, "basic async write/read roundtrip")
    finally:
        try:
            os.unlink(path)
        except Exception:
            pass


if __name__ == "__main__":
    print("Running basic interface tests for ayafileio...")
    test_worker_validation()
    test_handle_pool_limits()
    test_basic_io()
    if failures:
        print(f"\nCompleted with {failures} failure(s)")
        sys.exit(1)
    else:
        print("\nAll tests passed")