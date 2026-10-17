import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import socket
import statistics
import subprocess
import sys
import threading
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


def server_snapshot(pid):
    """Linux 只读采样。VmHWM 是进程生命周期峰值，不是本轮独立峰值。"""
    if pid is None:
        return None
    try:
        stat = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
        status = Path(f"/proc/{pid}/status").read_text().splitlines()
        peak = next(int(line.split()[1]) for line in status
                    if line.startswith("VmHWM:"))
        return {"cpu_ticks": int(stat[11]) + int(stat[12]),
                "start_ticks": int(stat[19]), "peak_rss_kib": peak,
                "sample_ns": time.perf_counter_ns()}
    except (OSError, ValueError, IndexError, StopIteration):
        return None


def server_metrics(before, after):
    if before is None or after is None or before["start_ticks"] != after["start_ticks"]:
        return {"available": False}
    cpu_seconds = (after["cpu_ticks"] - before["cpu_ticks"]) / os.sysconf("SC_CLK_TCK")
    interval = (after["sample_ns"] - before["sample_ns"]) / 1_000_000_000
    return {"available": True, "cpu_seconds": cpu_seconds,
            "cpu_pct_one_core": cpu_seconds / interval * 100,
            "sample_interval_s": interval,
            "process_lifetime_peak_rss_kib": after["peak_rss_kib"]}


def run_benchmark(host, port, timeout, requests, warmup, concurrency=1, server_pid=None):
    """串行预热后，以固定并发运行一次；每连接一请求，不自动重试。"""
    for index in range(warmup):
        try:
            request_once(host, port, timeout)
        except (OSError, ValueError) as error:
            raise RuntimeError(f"Warm-up failed at request {index + 1}/{warmup}: {error}") from error

    timing = {}

    def begin():
        timing["server_before"] = server_snapshot(server_pid)
        timing["start_ns"] = time.perf_counter_ns()

    # 所有线程就绪后一起开始；线程创建不计入正式测量。
    ready = threading.Barrier(concurrency + 1, action=begin)

    def client(count):
        latencies = []
        failures = []
        ready.wait()
        for _ in range(count):
            attempt_start = time.perf_counter_ns()
            try:
                latencies.append(request_once(host, port, timeout))
            except (OSError, ValueError) as error:
                failures.append({"elapsed_ns": time.perf_counter_ns() - attempt_start,
                                 "error": f"{type(error).__name__}: {error}"})
        return latencies, failures

    with ThreadPoolExecutor(max_workers=concurrency) as pool:
        per_client, extra = divmod(requests, concurrency)
        futures = [pool.submit(client, per_client + (index < extra))
                   for index in range(concurrency)]
        ready.wait()
        results = [future.result() for future in futures]
        elapsed_ns = time.perf_counter_ns() - timing["start_ns"]
        after = server_snapshot(server_pid)

    latencies_ns = [latency for latencies, _ in results for latency in latencies]
    failures = [failure for _, failures in results for failure in failures]
    ordered = sorted(latencies_ns)
    completed = len(latencies_ns)
    return {
        "concurrency": concurrency, "requests": requests, "warmup": warmup,
        "completed": completed, "failed": len(failures),
        "error_rate_pct": len(failures) / requests * 100,
        "elapsed_s": elapsed_ns / 1_000_000_000,
        "throughput_success_rps": completed / (elapsed_ns / 1_000_000_000),
        "p50_ms": percentile_ms(ordered, 50) if ordered else None,
        "p95_ms": percentile_ms(ordered, 95) if ordered else None,
        "errors": dict(Counter(failure["error"] for failure in failures)),
        "latencies_ns": latencies_ns, "failures": failures,
        "server": server_metrics(timing["server_before"], after),
    }


def percentile_ms(sorted_latencies_ns, percentile):
    """使用 nearest-rank 定义；调用方传入非空、升序排列的耗时。"""
    index = math.ceil(percentile / 100 * len(sorted_latencies_ns)) - 1
    return sorted_latencies_ns[index] / 1_000_000


def summarize(runs):
    """每个并发数逐项取各轮中位数，不混合不同轮次的延迟样本。"""
    summaries = []
    for concurrency in sorted({run["concurrency"] for run in runs}):
        group = [run for run in runs if run["concurrency"] == concurrency]
        summary = {"concurrency": concurrency, "runs": len(group),
                   "total_completed": sum(run["completed"] for run in group),
                   "total_failed": sum(run["failed"] for run in group)}
        for metric in ("throughput_success_rps", "error_rate_pct", "p50_ms", "p95_ms"):
            values = [run[metric] for run in group]
            # 任一轮无成功样本时，不隐藏该轮而只对其余轮次取中位数。
            summary[f"median_{metric}"] = (statistics.median(values)
                                           if all(v is not None for v in values) else None)
        summaries.append(summary)
    return summaries


def command_output(command):
    try:
        return subprocess.check_output(command, text=True, stderr=subprocess.DEVNULL,
                                       timeout=3).strip()
    except (OSError, subprocess.SubprocessError):
        return None


def file_hash(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest() if path else None


def environment(args):
    cpu_model = None
    try:
        cpu_model = next(line.split(":", 1)[1].strip()
                         for line in Path("/proc/cpuinfo").read_text().splitlines()
                         if line.startswith("model name"))
    except (OSError, StopIteration):
        pass
    return {
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "os": platform.platform(), "cpu_model": cpu_model,
        "logical_cpus": os.cpu_count(), "python": platform.python_version(),
        "git_commit": command_output(["git", "rev-parse", "HEAD"]),
        "git_status": command_output(["git", "status", "--short"]),
        "script_sha256": file_hash(__file__),
        "server_binary_sha256": file_hash(args.server_binary),
        "compiler": command_output(["c++", "--version"]),
        "build_notes": args.notes,
    }


def main():
    parser = argparse.ArgumentParser(
        description="Benchmark short TCP requests against a fresh, empty Scheduler."
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--timeout", type=float, default=2.0,
                        help="Socket operation timeout in seconds (default: 2)")
    parser.add_argument("--requests", type=int, default=1000,
                        help="Number of measured requests (default: 1000)")
    parser.add_argument("--warmup", type=int, default=10,
                        help="Sequential warm-up requests before each run (default: 10)")
    parser.add_argument("--concurrency", type=int, nargs="+", default=[1],
                        help="Client counts, e.g. 1 10 50 100 (default: 1)")
    parser.add_argument("--repeat", type=int, default=3,
                        help="Measured runs per client count (default: 3)")
    parser.add_argument("--output", type=Path, help="Save raw samples and metadata as JSON")
    parser.add_argument("--label", default="manual", help="Server implementation label")
    parser.add_argument("--server-pid", type=int, help="Linux PID for optional CPU/RSS sampling")
    parser.add_argument("--server-binary", type=Path, help="Hash this server executable")
    parser.add_argument("--notes", default="", help="Build flags, logging, and server revision")
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout must be finite and positive")
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    if args.requests <= 0:
        parser.error("--requests must be positive")
    if args.warmup < 0:
        parser.error("--warmup must not be negative")
    if args.repeat <= 0:
        parser.error("--repeat must be positive")
    if any(c <= 0 or c > args.requests for c in args.concurrency):
        parser.error("each --concurrency must be between 1 and --requests")
    if len(set(args.concurrency)) != len(args.concurrency):
        parser.error("--concurrency must not contain duplicates")
    if args.server_pid is not None and args.server_pid <= 0:
        parser.error("--server-pid must be positive")
    if args.server_binary is not None and not args.server_binary.is_file():
        parser.error("--server-binary must name an existing file")

    report = {"schema_version": 1, "label": args.label,
              "environment": environment(args),
              "configuration": {"host": args.host, "port": args.port,
                                "timeout_s": args.timeout, "requests": args.requests,
                                "warmup": args.warmup, "concurrency": args.concurrency,
                                "repeat": args.repeat, "server_pid": args.server_pid,
                                "command": [sys.executable, *sys.argv],
                                "request": "SCHEDULE_NEXT\n", "request_bytes": 14,
                                "expected_response": "SCHEDULE_NONE\n", "response_bytes": 14},
              "runs": [], "aborted": None}
    print("Concurrency Run Completed Failed Success_req/s P50_ms P95_ms", flush=True)
    for concurrency in args.concurrency:
        for repeat in range(1, args.repeat + 1):
            try:
                run = run_benchmark(args.host, args.port, args.timeout, args.requests,
                                    args.warmup, concurrency, args.server_pid)
            except RuntimeError as error:
                report["aborted"] = {"concurrency": concurrency, "repeat": repeat,
                                     "error": str(error)}
                print(str(error), file=sys.stderr)
                break
            run["repeat"] = repeat
            report["runs"].append(run)
            p50 = f"{run['p50_ms']:.3f}" if run["p50_ms"] is not None else "N/A"
            p95 = f"{run['p95_ms']:.3f}" if run["p95_ms"] is not None else "N/A"
            print(f"{concurrency:11} {repeat:3} {run['completed']:9} {run['failed']:6} "
                  f"{run['throughput_success_rps']:13.2f} {p50:>6} {p95:>6}", flush=True)
            for reason, count in run["errors"].items():
                print(f"  Failed {count} request(s): {reason}", file=sys.stderr)
        if report["aborted"]:
            break
    report["summary"] = summarize(report["runs"])
    print("Medians across runs (successful-request latency):")
    for row in report["summary"]:
        print(json.dumps(row, sort_keys=True))
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
        print(f"Saved {args.output}")
    return 1 if report["aborted"] or any(run["failed"] for run in report["runs"]) else 0


if __name__ == "__main__":
    sys.exit(main())
