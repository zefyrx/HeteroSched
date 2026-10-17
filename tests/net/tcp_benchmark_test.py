"""验证压测工具的并发、样本记账和持久化；不把测试耗时当作性能结果。"""

from concurrent.futures import ThreadPoolExecutor
import importlib.util
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "benchmarks/tcp_benchmark.py"
SPEC = importlib.util.spec_from_file_location("tcp_benchmark", SCRIPT)
benchmark = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(benchmark)
OK = b"SCHEDULE_NONE\n"


class TcpFixture:
    """只接受约定数量的连接，用同步信号验证并发，不依赖 sleep。"""

    def __init__(self, count, handler=None):
        self.count = count
        self.handler = handler or (lambda connection: connection.sendall(OK))
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(128)
        self.listener.settimeout(3)
        self.port = self.listener.getsockname()[1]
        self.errors = []
        self.release = threading.Event()

    def handle(self, connection):
        with connection:
            connection.settimeout(3)
            request = b""
            while b"\n" not in request:
                chunk = connection.recv(128)
                if not chunk:
                    raise RuntimeError("incomplete request")
                request += chunk
            if request != b"SCHEDULE_NEXT\n":
                raise RuntimeError(f"unexpected request: {request!r}")
            self.handler(connection)

    def serve(self):
        try:
            with self.listener, ThreadPoolExecutor(max_workers=32) as pool:
                futures = []
                for _ in range(self.count):
                    connection, _ = self.listener.accept()
                    futures.append(pool.submit(self.handle, connection))
                for future in futures:
                    future.result()
        except Exception as error:
            self.errors.append(error)

    def __enter__(self):
        self.thread = threading.Thread(target=self.serve, daemon=True)
        self.thread.start()
        return self

    def __exit__(self, exc_type, exc, traceback):
        self.release.set()
        self.thread.join(5)
        if self.thread.is_alive():
            raise RuntimeError("test server did not stop")
        if self.errors and exc_type is None:
            raise self.errors[0]


class BenchmarkTests(unittest.TestCase):
    def run_workload(self, server, requests, warmup=0, concurrency=1, timeout=1):
        return benchmark.run_benchmark("127.0.0.1", server.port, timeout,
                                       requests, warmup, concurrency)

    def test_clients_really_overlap(self):
        all_connected = threading.Barrier(3)

        def respond(connection):
            all_connected.wait(timeout=2)
            connection.sendall(OK)

        with TcpFixture(3, respond) as server:
            run = self.run_workload(server, requests=3, concurrency=3, timeout=3)
        self.assertEqual((run["completed"], run["failed"]), (3, 0))

    def test_partition_and_warmup_accounting(self):
        with TcpFixture(13) as server:
            run = self.run_workload(server, requests=11, warmup=2, concurrency=3)
        self.assertEqual(len(run["latencies_ns"]), 11)
        self.assertEqual(run["completed"] + run["failed"], 11)
        self.assertEqual(run["failed"], 0)
        self.assertGreater(run["elapsed_s"], 0)

    def test_split_response_and_network_failures(self):
        def split(connection):
            connection.sendall(b"SCHEDULE_")
            connection.sendall(b"NONE\n")

        with TcpFixture(1, split) as server:
            self.assertEqual(self.run_workload(server, 1)["completed"], 1)
        for payload, phase in ((b"BAD\n", "validate"),
                               (b"SCHEDULE_", "receive"), (b"x" * 100, "validate")):
            with self.subTest(payload=payload):
                with TcpFixture(1, lambda connection: connection.sendall(payload)) as server:
                    run = self.run_workload(server, 1)
                self.assertEqual(run["failed"], 1)
                self.assertEqual(len(run["failures"]), 1)
                self.assertIsNone(run["p95_ms"])
                self.assertGreater(run["failures"][0]["elapsed_ns"], 0)
                self.assertEqual(run["failures"][0]["phase"], phase)

    def test_timeout_is_counted(self):
        with TcpFixture(1) as server:
            server.handler = lambda connection: server.release.wait(timeout=3)
            run = self.run_workload(server, 1, timeout=0.05)
        self.assertEqual(run["failed"], 1)
        self.assertIn("TimeoutError", run["failures"][0]["error"])
        self.assertEqual(run["failures"][0]["phase"], "receive")

    def test_refused_connection_is_classified(self):
        with socket.socket() as reserved:
            reserved.bind(("127.0.0.1", 0))
            run = benchmark.run_benchmark("127.0.0.1", reserved.getsockname()[1], 1, 1, 0)
        self.assertEqual(run["failed"], 1)
        self.assertEqual(run["failures"][0]["phase"], "connect")

    def test_mixed_success_and_failure_are_all_counted(self):
        sequence = iter([OK, b"BAD\n", OK, b"SCHEDULE_"])
        lock = threading.Lock()

        def respond(connection):
            with lock:
                payload = next(sequence)
            connection.sendall(payload)

        with TcpFixture(4, respond) as server:
            run = self.run_workload(server, 4, concurrency=2)
        self.assertEqual((run["completed"], run["failed"]), (2, 2))
        self.assertEqual(run["error_rate_pct"], 50)
        self.assertEqual(sum(run["errors"].values()), 2)

    def test_failed_warmup_aborts(self):
        with TcpFixture(1, lambda connection: connection.sendall(b"BAD\n")) as server:
            with self.assertRaisesRegex(RuntimeError, "Warm-up failed"):
                self.run_workload(server, requests=10, warmup=1)

    def test_percentiles_and_summary_keep_failed_runs(self):
        samples = [i * 1_000_000 for i in range(1, 21)]
        self.assertEqual(benchmark.percentile_ms(samples, 50), 10)
        self.assertEqual(benchmark.percentile_ms(samples, 95), 19)
        self.assertEqual(benchmark.percentile_ms([2_000_000], 95), 2)
        with TcpFixture(2) as server:
            first = self.run_workload(server, 2)
        second = dict(first, p50_ms=None, p95_ms=None, completed=0, failed=2,
                      throughput_success_rps=0, error_rate_pct=100)
        summary = benchmark.summarize([first, second])[0]
        self.assertIsNone(summary["median_p95_ms"])
        self.assertEqual(summary["total_failed"], 2)

    def test_cli_repeats_and_saves_raw_samples(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "result.json"
            with TcpFixture(28) as server:
                result = subprocess.run([
                    sys.executable, str(SCRIPT), "--port", str(server.port),
                    "--requests", "5", "--warmup", "2", "--concurrency", "1", "3",
                    "--repeat", "2", "--server-pid", str(os.getpid()),
                    "--output", str(output)
                ], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(output.read_text())
            self.assertEqual(len(report["runs"]), 4)
            self.assertEqual(len(report["summary"]), 2)
            for run in report["runs"]:
                self.assertEqual(len(run["latencies_ns"]), 5)
                self.assertEqual(run["failed"], 0)
                if sys.platform.startswith("linux"):
                    self.assertTrue(run["server"]["available"])

    def test_cli_failure_is_saved_and_returns_nonzero(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "failed.json"
            with TcpFixture(2, lambda connection: connection.sendall(b"BAD\n")) as server:
                result = subprocess.run([
                    sys.executable, str(SCRIPT), "--port", str(server.port),
                    "--requests", "2", "--warmup", "0", "--repeat", "1",
                    "--output", str(output)
                ], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 1)
            report = json.loads(output.read_text())
            self.assertEqual(report["runs"][0]["failed"], 2)
            self.assertIsNone(report["summary"][0]["median_p95_ms"])

    def test_cli_warmup_abort_is_saved(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "aborted.json"
            with TcpFixture(1, lambda connection: connection.sendall(b"BAD\n")) as server:
                result = subprocess.run([
                    sys.executable, str(SCRIPT), "--port", str(server.port),
                    "--requests", "2", "--warmup", "1", "--repeat", "1",
                    "--output", str(output)
                ], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 1)
            report = json.loads(output.read_text())
            self.assertEqual(report["runs"], [])
            self.assertIn("Warm-up failed", report["aborted"]["error"])


if __name__ == "__main__":
    unittest.main()
