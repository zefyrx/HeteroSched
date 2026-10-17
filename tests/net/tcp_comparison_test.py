"""验证对比工具的条件一致性、进程生命周期和结果保存。"""

import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "benchmarks"))
from compare_tcp_servers import ServerProcess


def request(port, payload):
    with socket.create_connection(("127.0.0.1", port), timeout=3) as connection:
        connection.sendall(payload)
        response = b""
        while b"\n" not in response:
            chunk = connection.recv(4096)
            if not chunk:
                raise RuntimeError("response closed before newline")
            response += chunk
        return response


class ComparisonTests(unittest.TestCase):
    def binary(self, label):
        return BUILD_DIR / ("scheduler_server" if label == "blocking" else "scheduler_server_epoll")

    def test_quiet_mode_protocol_and_error_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            for label in ("blocking", "epoll"):
                log = Path(directory) / f"{label}.log"
                with ServerProcess(self.binary(label), 0, log) as server:
                    self.assertEqual(request(server.port, b"REGISTER_WORKER 1 1 1\n"), b"REGISTER_OK\n")
                    self.assertEqual(request(server.port, b"SUBMIT_JOB 1 1 1\n"), b"SUBMIT_OK\n")
                    self.assertEqual(request(server.port, b"FETCH_JOB 1\n"), b"RUN_JOB 1 1 1\n")
                    self.assertEqual(request(server.port, b"COMPLETE_JOB 1\n"), b"COMPLETE_OK\n")
                    with socket.create_connection(("127.0.0.1", server.port), timeout=3) as partial:
                        partial.sendall(b"SCHEDULE_")
                        partial.shutdown(socket.SHUT_WR)
                        self.assertEqual(partial.recv(1), b"")
                self.assertEqual(server.stdout_tail, "")
                self.assertEqual(log.read_text(), "")

    def test_verbose_mode_keeps_request_logs(self):
        with tempfile.TemporaryDirectory() as directory:
            for label in ("blocking", "epoll"):
                with ServerProcess(self.binary(label), 0, Path(directory) / label, quiet=False) as server:
                    self.assertEqual(request(server.port, b"SCHEDULE_NEXT\n"), b"SCHEDULE_NONE\n")
                self.assertIn("Received: SCHEDULE_NEXT", server.stdout_tail)
                self.assertIn("Response: SCHEDULE_NONE", server.stdout_tail)
                self.assertIn("State: workers=0, waiting=0, running=0", server.stdout_tail)

    def test_invalid_options(self):
        for label in ("blocking", "epoll"):
            for arguments in (("--port", "-1"), ("--port", "65536"), ("--port", "abc"),
                              ("--port",), ("--bad",), ("--port", "0", "--port", "0")):
                with self.subTest(label=label, arguments=arguments):
                    result = subprocess.run([str(self.binary(label)), *arguments],
                                            capture_output=True, text=True, timeout=3)
                    self.assertEqual(result.returncode, 1)
                    self.assertNotIn("Listening", result.stdout)

    def compare(self, output, *arguments):
        return subprocess.run([
            sys.executable, str(ROOT / "benchmarks/compare_tcp_servers.py"),
            "--build-dir", str(BUILD_DIR), "--output-dir", str(output),
            "--requests", "5", "--warmup", "1", "--concurrency", "1", "2",
            "--repeat", "2", "--timeout", "2", *arguments
        ], capture_output=True, text=True, timeout=20)

    def test_alternating_fresh_processes_reuse_one_port(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            result = self.compare(output)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            comparison = json.loads((output / "comparison.json").read_text())
            self.assertIsNone(comparison["aborted"])
            order = comparison["execution_order"]
            self.assertEqual(len({item["port"] for item in order}), 1)
            self.assertEqual(len({item["server_pid"] for item in order}), 8)
            self.assertEqual([item["label"] for item in order[:4]],
                             ["blocking", "epoll", "epoll", "blocking"])
            for label in ("blocking", "epoll"):
                raw = json.loads((output / f"{label}.json").read_text())
                self.assertEqual(len(raw["runs"]), 4)
                for run in raw["runs"]:
                    self.assertEqual(run["completed"], 5)
                    self.assertEqual(run["failed"], 0)
                    self.assertEqual(run["server_stdout_after_startup"], "")
                    self.assertEqual(run["server_stderr_bytes"], 0)

    def test_occupied_port_aborts_and_preserves_existing_server(self):
        with tempfile.TemporaryDirectory() as directory:
            with ServerProcess(self.binary("blocking"), 0, Path(directory) / "existing.log") as server:
                output = Path(directory) / "results"
                result = self.compare(output, "--port", str(server.port))
                self.assertEqual(result.returncode, 1)
                comparison = json.loads((output / "comparison.json").read_text())
                self.assertIsNotNone(comparison["aborted"])
                self.assertEqual(comparison["execution_order"], [])
                self.assertIsNone(server.process.poll())
                self.assertEqual(request(server.port, b"SCHEDULE_NEXT\n"), b"SCHEDULE_NONE\n")


if __name__ == "__main__":
    BUILD_DIR = Path(sys.argv.pop(1)).resolve()
    unittest.main()
