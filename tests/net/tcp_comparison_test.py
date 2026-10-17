"""验证对比工具的条件一致性、进程生命周期和结果保存。"""

import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "benchmarks"))
from compare_tcp_servers import ServerProcess, counter_delta, listen_counters


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
                              ("--port",), ("--bad",), ("--port", "0", "--port", "0"),
                              ("--backlog", "0"), ("--backlog", "-1"),
                              ("--backlog", "2147483648"), ("--backlog", "abc"),
                              ("--backlog",), ("--backlog", "1", "--backlog", "128")):
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
            self.assertEqual(comparison["configuration"]["backlog"], 1)
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

    def test_custom_backlog_is_used_and_recorded(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            result = self.compare(output, "--backlog", "128")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            comparison = json.loads((output / "comparison.json").read_text())
            self.assertEqual(comparison["configuration"]["backlog"], 128)
            self.assertIn("core/somaxconn", comparison["kernel_network_settings"])
            for item in comparison["execution_order"]:
                self.assertEqual(item["backlog"], 128)
                command = item["server_command"]
                self.assertEqual(command[command.index("--backlog") + 1], "128")
            for label in ("blocking", "epoll"):
                raw = json.loads((output / f"{label}.json").read_text())
                for run in raw["runs"]:
                    counters = run["listen_counters"]
                    if counters["available"]:
                        self.assertEqual(counters["scope"], "network_namespace")
                        self.assertEqual(counters["window"], "warmup_and_measured_requests")
                        self.assertTrue(all(value >= 0 for value in counters["delta"].values()))

    def test_blocking_backlog_can_hold_a_burst_while_one_client_is_incomplete(self):
        # 阻塞版接受第一个空闲连接后停在 recv，后续连接只能在内核排队。
        # 只验证突发连接可排队；不把它当作慢客户端隔离能力。
        with tempfile.TemporaryDirectory() as directory:
            with ServerProcess(self.binary("blocking"), 0, Path(directory) / "queue.log",
                               backlog=128) as server:
                clients = []
                try:
                    for _ in range(8):
                        clients.append(socket.create_connection(("127.0.0.1", server.port), timeout=2))
                    self.assertEqual(len(clients), 8)
                finally:
                    for client in clients:
                        client.close()

    def test_counter_parsing_and_unavailable_data(self):
        sample = ("IpExt: A\nIpExt: 0\n"
                  "TcpExt: Other ListenDrops ListenOverflows\nTcpExt: 99 12 8\n")
        with patch("pathlib.Path.read_text", return_value=sample):
            self.assertEqual(listen_counters(), {"ListenOverflows": 8, "ListenDrops": 12})
        with patch("pathlib.Path.read_text", side_effect=OSError("unavailable")):
            self.assertIsNone(listen_counters())
        self.assertFalse(counter_delta(None, {"ListenOverflows": 8})["available"])
        self.assertFalse(counter_delta({"ListenOverflows": 8}, {"ListenOverflows": 0})["available"])

    def test_invalid_comparison_backlog_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ("0", "-1", "2147483648"):
                result = self.compare(Path(directory), "--backlog", value)
                self.assertEqual(result.returncode, 2)

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
