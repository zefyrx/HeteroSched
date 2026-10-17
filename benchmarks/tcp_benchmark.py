import argparse
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


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Measure one TCP request against an empty Scheduler."
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--timeout", type=float, default=2.0,
                        help="Socket operation timeout in seconds (default: 2)")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")

    try:
        elapsed_ns = request_once(args.host, args.port, args.timeout)
    except (OSError, ValueError) as error:
        print(f"Request failed: {error}", file=sys.stderr)
        sys.exit(1)

    print(f"Request succeeded: latency_ms={elapsed_ns / 1_000_000:.3f}")
