"""在相同端口交替测量两版服务端；每一轮拥有独立进程和原始样本。"""

import argparse
from collections import Counter
import json
import math
from pathlib import Path
import re
import select
import statistics
import subprocess
import sys
from types import SimpleNamespace

import tcp_benchmark as benchmark
from slow_client import run_slow_case


class ServerProcess:
    def __init__(self, executable, port, log_path, quiet=True, backlog=128):
        self.command = [str(executable), "--port", str(port), "--backlog", str(backlog)]
        if quiet:
            self.command.append("--quiet")
        self.log_path = log_path
        self.process = None
        self.log = None
        self.stdout_tail = ""

    def __enter__(self):
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        self.log = self.log_path.open("w")
        try:
            self.process = subprocess.Popen(self.command, stdout=subprocess.PIPE,
                                            stderr=self.log, text=True)
            readable, _, _ = select.select([self.process.stdout], [], [], 5)
            if not readable:
                raise RuntimeError("server startup timed out")
            startup = self.process.stdout.readline().strip()
            match = re.fullmatch(r"Listening on 127\.0\.0\.1:(\d+)", startup)
            if match is None:
                raise RuntimeError(f"server startup failed: {startup}; see {self.log_path}")
            self.port = int(match.group(1))
            return self
        except BaseException:
            self.stop()
            raise

    def stop(self):
        if self.process is not None:
            if self.process.poll() is None:
                self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
            self.stdout_tail = self.process.stdout.read()
            self.process.stdout.close()
        if self.log is not None:
            self.log.close()

    def __exit__(self, exc_type, exc, traceback):
        self.stop()


def read_cache(build_dir):
    cache = {}
    for line in (build_dir / "CMakeCache.txt").read_text().splitlines():
        if line and not line.startswith(("#", "//")) and "=" in line:
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    return cache


def kernel_network_settings():
    settings = {}
    for name in ("core/somaxconn", "ipv4/tcp_max_syn_backlog",
                 "ipv4/tcp_syncookies", "ipv4/tcp_abort_on_overflow"):
        try:
            settings[name] = int(Path("/proc/sys/net", name).read_text())
        except (OSError, ValueError):
            settings[name] = None
    return settings


def listen_counters():
    """读取网络命名空间的全局计数，不能归因于单个服务端 PID。"""
    try:
        lines = Path("/proc/net/netstat").read_text().splitlines()
        for index in range(len(lines) - 1):
            header = lines[index].split()
            if header and header[0] == "TcpExt:":
                values = lines[index + 1].split()
                if values[0] != "TcpExt:" or len(values) != len(header):
                    return None
                counters = dict(zip(header[1:], map(int, values[1:])))
                return {key: counters[key] for key in ("ListenOverflows", "ListenDrops")}
    except (OSError, ValueError, KeyError, IndexError):
        pass
    return None


def counter_delta(before, after):
    if before is None or after is None or any(after[key] < before[key] for key in before):
        return {"available": False}
    return {"available": True, "before": before, "after": after,
            "delta": {key: after[key] - before[key] for key in before},
            "scope": "network_namespace",
            "window": "warmup_setup_measured_requests_and_recovery"}


def comparison_rows(reports):
    rows = []
    summaries = {label: {row["concurrency"]: row for row in report["summary"]}
                 for label, report in reports.items()}
    shared = set(summaries["blocking"]) & set(summaries["epoll"])
    for concurrency in sorted(shared):
        row = {"concurrency": concurrency}
        for label, report in reports.items():
            group = [run for run in report["runs"] if run["concurrency"] == concurrency]
            entry = dict(summaries[label][concurrency])
            resources = [run["server"] for run in group]
            available = all(resource["available"] for resource in resources)
            entry["median_server_cpu_pct_one_core"] = (
                statistics.median(resource["cpu_pct_one_core"] for resource in resources)
                if available else None)
            entry["median_process_peak_rss_kib"] = (
                statistics.median(resource["process_lifetime_peak_rss_kib"] for resource in resources)
                if available else None)
            entry["failure_phases"] = dict(Counter(
                failure["phase"] for run in group for failure in run["failures"]))
            row[label] = entry
        rows.append(row)
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--output-dir", type=Path, default=Path("build/tcp-comparison"))
    parser.add_argument("--port", type=int, default=0,
                        help="First server selects a free port by default; all later runs reuse it")
    parser.add_argument("--requests", type=int, default=1000)
    parser.add_argument("--warmup", type=int, default=10)
    parser.add_argument("--concurrency", type=int, nargs="+", default=[1, 10, 50, 100])
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=1.0)
    parser.add_argument("--backlog", type=int, default=128,
                        help="Requested listen backlog for both servers (default: 128; use 1 for old baseline)")
    parser.add_argument("--slow-clients", type=int, choices=(0, 1), default=0,
                        help="Hold one accepted incomplete request during normal traffic (Linux)")
    args = parser.parse_args()
    if not 0 <= args.port <= 65535:
        parser.error("--port must be between 0 and 65535")
    if not 1 <= args.backlog <= 2147483647:
        parser.error("--backlog must be a positive 32-bit int")
    if args.requests <= 0 or args.repeat <= 0 or args.warmup < 0:
        parser.error("requests/repeat must be positive; warmup must be nonnegative")
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout must be finite and positive")
    if (len(set(args.concurrency)) != len(args.concurrency) or
            any(c <= 0 or c > args.requests for c in args.concurrency)):
        parser.error("concurrency values must be unique and between 1 and requests")
    if args.slow_clients:
        if args.backlog != 128 or args.requests >= 128:
            parser.error("slow-client experiment requires --backlog 128 and --requests < 128")
        if math.ceil(args.requests / min(args.concurrency)) * args.timeout > 30:
            parser.error("slow-client experiment requires requests-per-thread * timeout <= 30s")
    build_dir = args.build_dir.resolve()
    try:
        cache = read_cache(build_dir)
    except OSError as error:
        parser.error(f"configure and build Release first: {error}")
    if cache.get("CMAKE_BUILD_TYPE") != "Release":
        parser.error("comparison requires CMAKE_BUILD_TYPE=Release")
    binaries = {"blocking": build_dir / "scheduler_server",
                "epoll": build_dir / "scheduler_server_epoll"}
    if not all(path.is_file() for path in binaries.values()):
        parser.error("build scheduler_server and scheduler_server_epoll first")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    configuration = {"host": "127.0.0.1", "requests": args.requests, "warmup": args.warmup,
                     "concurrency": args.concurrency, "repeat": args.repeat,
                     "timeout_s": args.timeout, "quiet": True, "backlog": args.backlog,
                     "slow_clients": args.slow_clients,
                     "recovery_probe_timeout_s": 1,
                     "reuse_address": True, "fresh_process_per_run": True,
                     "request": "SCHEDULE_NEXT\n", "expected_response": "SCHEDULE_NONE\n",
                     "request_bytes": 14, "response_bytes": 14,
                     "command": [sys.executable, *sys.argv]}
    notes = (f"Release; quiet; backlog={args.backlog}; SO_REUSEADDR; "
             "fresh server each run; alternating order")
    reports = {label: {"schema_version": 1, "label": label + "-quiet",
                       "environment": benchmark.environment(SimpleNamespace(
                           server_binary=binary, notes=notes)),
                       "configuration": configuration, "runs": [], "aborted": None}
               for label, binary in binaries.items()}
    root = Path(__file__).resolve().parents[1]
    sources = sorted(path for directory in ("apps", "include", "src")
                     for path in (root / directory).rglob("*")
                     if path.suffix in (".h", ".cpp"))
    comparison = {"schema_version": 1, "configuration": configuration,
                  "runner_sha256": benchmark.file_hash(__file__),
                  "slow_client_helper_sha256": benchmark.file_hash(root / "benchmarks/slow_client.py"),
                  "source_sha256": {str(path.relative_to(root)): benchmark.file_hash(path)
                                    for path in sources},
                  "cmake": {key: cache.get(key) for key in (
                      "CMAKE_BUILD_TYPE", "CMAKE_CXX_COMPILER", "CMAKE_CXX_FLAGS",
                      "CMAKE_CXX_FLAGS_RELEASE")},
                  "kernel_network_settings": kernel_network_settings(),
                  "execution_order": [], "aborted": None}
    selected_port = args.port
    interrupted = False
    try:
        for concurrency in args.concurrency:
            for repeat in range(1, args.repeat + 1):
                order = ("blocking", "epoll") if repeat % 2 else ("epoll", "blocking")
                for label in order:
                    log_path = (build_dir / "tcp-comparison-logs" /
                                f"s{args.slow_clients}-b{args.backlog}-c{concurrency}-r{repeat}-{label}.log")
                    with ServerProcess(binaries[label], selected_port, log_path,
                                       backlog=args.backlog) as server:
                        selected_port = server.port
                        position = {"label": label, "concurrency": concurrency,
                                    "repeat": repeat, "port": selected_port, "backlog": args.backlog,
                                    "server_command": server.command, "server_pid": server.process.pid,
                                    "stderr_log": str(log_path)}
                        comparison["execution_order"].append(position)
                        print(f"Running {label}: backlog={args.backlog}, clients={concurrency}, repeat={repeat}, "
                              f"port={selected_port}", flush=True)
                        before = listen_counters()
                        runner = run_slow_case if args.slow_clients else benchmark.run_benchmark
                        run = runner("127.0.0.1", selected_port, args.timeout,
                                     args.requests, args.warmup, concurrency, server.process.pid)
                        run["repeat"] = repeat
                        run["server_process"] = position
                        reports[label]["runs"].append(run)
                        try:
                            latency = benchmark.request_once("127.0.0.1", selected_port, 1)
                            run["recovery_probe"] = {"ok": True, "latency_ns": latency}
                        except Exception as error:
                            run["recovery_probe"] = {"ok": False, "error": str(error),
                                                     "phase": getattr(error, "phase", None)}
                        run["listen_counters"] = counter_delta(before, listen_counters())
                        if not run.get("slow_client", {"valid": True})["valid"]:
                            raise RuntimeError(f"invalid slow-client scenario: {run['slow_client']}")
                        if not run["recovery_probe"]["ok"]:
                            raise RuntimeError(f"recovery probe failed: {run['recovery_probe']}")
                        if server.process.poll() is not None:
                            raise RuntimeError(f"{label} exited during measurement; see {log_path}")
                    run["server_stdout_after_startup"] = server.stdout_tail
                    run["server_stderr_bytes"] = log_path.stat().st_size
                    if server.stdout_tail or run["server_stderr_bytes"]:
                        raise RuntimeError(f"unexpected server output in quiet mode; see {log_path}")
                    print(f"  completed={run['completed']} failed={run['failed']} "
                          f"success_req/s={run['throughput_success_rps']:.2f} "
                          f"failure_phases={dict(Counter(f['phase'] for f in run['failures']))}",
                          flush=True)
    except (Exception, KeyboardInterrupt) as error:
        interrupted = isinstance(error, KeyboardInterrupt)
        comparison["aborted"] = f"{type(error).__name__}: {error}"
        print(comparison["aborted"], file=sys.stderr)
    finally:
        configuration["port"] = selected_port
        for label, report in reports.items():
            report["aborted"] = comparison["aborted"]
            report["summary"] = ([] if comparison["aborted"] else benchmark.summarize(report["runs"]))
            (args.output_dir / f"{label}.json").write_text(
                json.dumps(report, indent=2, allow_nan=False) + "\n")
        comparison["rows"] = comparison_rows(reports)
        (args.output_dir / "comparison.json").write_text(
            json.dumps(comparison, indent=2, allow_nan=False) + "\n")

    print("Medians: clients | blocking req/s | epoll req/s | blocking failures | epoll failures")
    for row in comparison["rows"]:
        b, e = row["blocking"], row["epoll"]
        print(f"{row['concurrency']:7} | {b['median_throughput_success_rps']:14.2f} | "
              f"{e['median_throughput_success_rps']:11.2f} | "
              f"{b['total_failed']:17} | {e['total_failed']:14}")
    print(f"Saved raw reports and comparison to {args.output_dir}")
    if interrupted:
        return 130
    return 1 if comparison["aborted"] or any(run["failed"] for report in reports.values()
                                            for run in report["runs"]) else 0


if __name__ == "__main__":
    sys.exit(main())
