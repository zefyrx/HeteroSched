import argparse
from collections import Counter
import math
import socket
import sys
import time


def request_once(host, port, timeout):
    """返回从开始连接到收到完整响应的耗时，单位为纳秒；失败时抛出异常。"""
    expected = b"SCHEDULE_NONE\n"

    # 1. 用 time.perf_counter_ns() 记录开始时间
    start = time.perf_counter_ns()

    # 2. 建立 TCP 连接并设置超时
    with socket.create_connection((host, port), timeout=timeout) as sock:
        # 3. 使用 sendall() 发送 SCHEDULE_NEXT\n
        sock.sendall(b"SCHEDULE_NEXT\n")

        # 4. TCP 可能分多次返回响应，因此持续读取到换行。
        response = b""
        while b"\n" not in response:
            chunk = sock.recv(4096)
            if not chunk:
                raise ConnectionError("connection closed before newline")
            response += chunk
            # 本场景响应固定，及时拒绝超长响应，避免持续累积数据。
            if len(response) > len(expected):
                raise ValueError(f"Unexpected response: {response!r}")

        elapsed_ns = time.perf_counter_ns() - start

        # 5. 校验空任务队列场景下的预期响应。
        if response != expected:
            raise ValueError(f"Unexpected response: {response!r}")

    # 6. with 在正常返回和异常时都会关闭 socket。
    return elapsed_ns


def run_benchmark(host, port, timeout, requests, warmup):
    """先预热，再返回成功请求耗时、失败原因计数及正式测量总耗时。"""
    for _ in range(warmup):
        try:
            request_once(host, port, timeout)
        except (OSError, ValueError) as error:
            raise RuntimeError(f"Warm-up failed: {error}") from error

    latencies_ns = []
    errors = Counter()
    start = time.perf_counter_ns()
    for _ in range(requests):
        try:
            latencies_ns.append(request_once(host, port, timeout))
        except (OSError, ValueError) as error:
            # 在计时循环外统一输出，避免逐条打印干扰测量。
            errors[f"{type(error).__name__}: {error}"] += 1
    elapsed_ns = time.perf_counter_ns() - start
    return latencies_ns, errors, elapsed_ns


def percentile_ms(sorted_latencies_ns, percentile):
    """使用 nearest-rank 定义；调用方传入非空、升序排列的耗时。"""
    index = math.ceil(percentile / 100 * len(sorted_latencies_ns)) - 1
    return sorted_latencies_ns[index] / 1_000_000


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Measure sequential TCP requests against an empty Scheduler."
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--timeout", type=float, default=2.0,
                        help="Socket operation timeout in seconds (default: 2)")
    parser.add_argument("--requests", type=int, default=1000,
                        help="Number of measured requests (default: 1000)")
    parser.add_argument("--warmup", type=int, default=10,
                        help="Unmeasured warm-up requests (default: 10)")
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout must be finite and positive")
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    if args.requests <= 0:
        parser.error("--requests must be positive")
    if args.warmup < 0:
        parser.error("--warmup must not be negative")

    try:
        latencies_ns, errors, elapsed_ns = run_benchmark(
            args.host, args.port, args.timeout, args.requests, args.warmup
        )
    except RuntimeError as error:
        print(f"{error}; no measured run performed", file=sys.stderr)
        sys.exit(1)

    completed = len(latencies_ns)
    failed = sum(errors.values())
    elapsed_seconds = elapsed_ns / 1_000_000_000
    print(f"Target: {args.host}:{args.port}; concurrency=1; "
          f"requests={args.requests}; warmup={args.warmup}; timeout_s={args.timeout:g}")
    print(f"Completed: {completed}; failed: {failed}; "
          f"error_rate_pct={failed / args.requests * 100:.2f}")
    print(f"Measured duration_s={elapsed_seconds:.6f}; "
          f"throughput_success_rps={completed / elapsed_seconds:.2f}")
    if latencies_ns:
        latencies_ns.sort()
        print(f"Successful request latency: "
              f"p50_ms={percentile_ms(latencies_ns, 50):.3f}; "
              f"p95_ms={percentile_ms(latencies_ns, 95):.3f}")
    else:
        print("Successful request latency: p50_ms=N/A; p95_ms=N/A")
    for reason, count in errors.items():
        print(f"Failed {count} request(s): {reason}", file=sys.stderr)
    sys.exit(1 if failed else 0)
