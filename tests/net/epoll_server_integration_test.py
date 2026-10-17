"""启动真实 C++ epoll 服务端验证协议和连接生命周期。"""

from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import re
import select
import socket
import subprocess
import sys
import threading
import unittest


class Peer:
    def __init__(self, port):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=3)
        self.buffer = b""

    def send(self, data):
        self.socket.sendall(data)

    def line(self):
        while b"\n" not in self.buffer:
            chunk = self.socket.recv(4096)
            if not chunk:
                raise ConnectionError("closed before response newline")
            self.buffer += chunk
        line, self.buffer = self.buffer.split(b"\n", 1)
        return line + b"\n"

    def eof(self):
        if self.buffer:
            raise AssertionError(f"unexpected buffered response: {self.buffer!r}")
        if self.socket.recv(1) != b"":
            raise AssertionError("expected EOF")

    def close(self):
        self.socket.close()


class EpollIntegrationTests(unittest.TestCase):
    def setUp(self):
        self.addCleanup(self.stop_server)
        self.start_server()

    def start_server(self, *options):
        self.server = subprocess.Popen([SERVER_BINARY, "--port", "0", "--quiet", *options],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       text=True)
        readable, _, _ = select.select([self.server.stdout], [], [], 3)
        self.assertTrue(readable, "server startup timed out")
        startup = self.server.stdout.readline().strip()
        match = re.fullmatch(r"Listening on 127\.0\.0\.1:(\d+)", startup)
        self.assertIsNotNone(match, f"unexpected startup: {startup}")
        self.port = int(match.group(1))

    def stop_server(self):
        self.server.terminate()
        try:
            self.server.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.server.kill()
            self.server.wait(timeout=3)
        stdout_tail = self.server.stdout.read()
        stderr = self.server.stderr.read()
        self.server.stdout.close()
        self.server.stderr.close()
        self.assertEqual(stdout_tail, "", "unexpected output in quiet mode")
        self.assertEqual(stderr, "", "server errors or sanitizer diagnostics")

    def peer(self):
        peer = Peer(self.port)
        self.addCleanup(peer.close)
        return peer

    def request(self, line):
        peer = self.peer()
        peer.send(line + b"\n")
        response = peer.line()
        peer.close()
        return response

    def test_short_connection_registration_and_persistent_requests(self):
        self.assertEqual(self.request(b"REGISTER_WORKER 1 2 4096"), b"REGISTER_OK\n")
        peer = self.peer()
        peer.send(b"SUBMIT_JOB 10 2 4096\nFETCH_JOB 1\nCOMPLETE_JOB 10\n")
        self.assertEqual(peer.line(), b"SUBMIT_OK\n")
        self.assertEqual(peer.line(), b"RUN_JOB 10 2 4096\n")
        self.assertEqual(peer.line(), b"COMPLETE_OK\n")
        # 保持连接，确认发完输出后还能处理新的请求。
        peer.send(b"FETCH_JOB 1\n")
        self.assertEqual(peer.line(), b"NO_JOB\n")

    def test_fragmented_request_does_not_block_another_client(self):
        slow = self.peer()
        slow.send(b"SUBMIT_JOB 11 ")
        self.assertEqual(self.request(b"SCHEDULE_NEXT"), b"SCHEDULE_NONE\n")
        slow.send(b"1 1\n")
        self.assertEqual(slow.line(), b"SUBMIT_OK\n")
        self.assertEqual(self.request(b"SUBMIT_JOB 11 1 1"), b"SUBMIT_ERROR JOB_REJECTED\n")

    def test_complete_lines_and_partial_tail(self):
        peer = self.peer()
        peer.send(b"SUBMIT_JOB 21 1 1\nSUBMIT_JOB 22 ")
        self.assertEqual(peer.line(), b"SUBMIT_OK\n")
        peer.send(b"1 1\nSCHEDULE_NEXT\n")
        self.assertEqual(peer.line(), b"SUBMIT_OK\n")
        self.assertEqual(peer.line(), b"SCHEDULE_NONE\n")

    def test_half_close_flushes_complete_lines_and_discards_partial_tail(self):
        peer = self.peer()
        peer.send(b"SUBMIT_JOB 31 1 1\nSUBMIT_JOB 32")
        peer.socket.shutdown(socket.SHUT_WR)
        self.assertEqual(peer.line(), b"SUBMIT_OK\n")
        peer.eof()
        self.assertEqual(self.request(b"SUBMIT_JOB 32 1 1"), b"SUBMIT_OK\n")

    def test_invalid_fields_and_commands(self):
        for request, expected in (
            (b"", b"REQUEST_ERROR UNKNOWN_COMMAND\n"),
            (b"BAD", b"REQUEST_ERROR UNKNOWN_COMMAND\n"),
            (b"REGISTER_WORKER -1 1 1", b"REGISTER_ERROR INVALID_MESSAGE\n"),
            (b"SUBMIT_JOB 1 0 4096", b"SUBMIT_ERROR INVALID_MESSAGE\n"),
            (b"SUBMIT_JOB 1 abc 4096", b"SUBMIT_ERROR INVALID_MESSAGE\n"),
            (b"SUBMIT_JOB 2147483648 1 1", b"SUBMIT_ERROR INVALID_MESSAGE\n"),
            (b"SCHEDULE_NEXT extra", b"SCHEDULE_ERROR INVALID_MESSAGE\n"),
            (b"FETCH_JOB 99", b"FETCH_ERROR UNKNOWN_WORKER\n"),
        ):
            with self.subTest(request=request):
                self.assertEqual(self.request(request), expected)

    def test_length_limit_applies_to_each_line(self):
        peer = self.peer()
        peer.send(b"SCHEDULE_NEXT" + b" " * 115 + b"\nSCHEDULE_NEXT\n")
        self.assertEqual(peer.line(), b"SCHEDULE_NONE\n")
        self.assertEqual(peer.line(), b"SCHEDULE_NONE\n")
        for payload in (b"x" * 129, b"x" * 129 + b"\n"):
            oversized = self.peer()
            oversized.send(payload)
            oversized.eof()
        self.assertEqual(self.request(b"SCHEDULE_NEXT"), b"SCHEDULE_NONE\n")

    def test_valid_response_before_oversized_line_is_preserved(self):
        peer = self.peer()
        peer.send(b"SUBMIT_JOB 40 1 1\n" + b"x" * 129 + b"\n")
        self.assertEqual(peer.line(), b"SUBMIT_OK\n")
        peer.eof()

    def test_disconnect_before_newline_does_not_submit(self):
        disconnected = self.peer()
        disconnected.send(b"SUBMIT_JOB 51 ")
        disconnected.close()
        self.assertEqual(self.request(b"SUBMIT_JOB 51 1 1"), b"SUBMIT_OK\n")

    def test_concurrent_fetch_delivers_once_and_completion_releases_once(self):
        self.assertEqual(self.request(b"REGISTER_WORKER 1 2 4096"), b"REGISTER_OK\n")
        self.assertEqual(self.request(b"SUBMIT_JOB 61 2 4096"), b"SUBMIT_OK\n")
        peers = [self.peer() for _ in range(12)]
        ready = threading.Barrier(len(peers))

        def fetch(peer):
            ready.wait(timeout=3)
            peer.send(b"FETCH_JOB 1\n")
            return peer.line()

        with ThreadPoolExecutor(max_workers=len(peers)) as pool:
            responses = list(pool.map(fetch, peers))
        self.assertEqual(responses.count(b"RUN_JOB 61 2 4096\n"), 1)
        self.assertEqual(responses.count(b"NO_JOB\n"), 11)
        self.assertEqual(self.request(b"COMPLETE_JOB 61"), b"COMPLETE_OK\n")
        self.assertEqual(self.request(b"COMPLETE_JOB 61"), b"COMPLETE_ERROR COMPLETION_REJECTED\n")
        for job_id in (62, 63):
            self.assertEqual(self.request(f"SUBMIT_JOB {job_id} 2 4096".encode()), b"SUBMIT_OK\n")
        self.assertEqual(self.request(b"FETCH_JOB 1"), b"RUN_JOB 62 2 4096\n")
        self.assertEqual(self.request(b"FETCH_JOB 1"), b"NO_JOB\n")
        self.assertEqual(self.request(b"COMPLETE_JOB 62"), b"COMPLETE_OK\n")
        self.assertEqual(self.request(b"FETCH_JOB 1"), b"RUN_JOB 63 2 4096\n")
        self.assertEqual(self.request(b"FETCH_JOB 1"), b"NO_JOB\n")

    def test_repeated_disconnects_do_not_break_new_connections(self):
        for _ in range(100):
            peer = self.peer()
            peer.send(b"SCHEDULE_")
            peer.close()
            self.assertEqual(self.request(b"SCHEDULE_NEXT"), b"SCHEDULE_NONE\n")

    def test_idle_timer_wakes_event_loop_and_discards_incomplete_request(self):
        self.stop_server()
        self.start_server("--idle-timeout-ms", "150")
        silent = self.peer()
        silent.eof()  # 无后续网络事件，epoll_wait 也必须按截止时间醒来。
        persistent = self.peer()
        persistent.send(b"SCHEDULE_NEXT\n")
        self.assertEqual(persistent.line(), b"SCHEDULE_NONE\n")
        persistent.eof()
        partial = self.peer()
        partial.send(b"SUBMIT_JOB 71 ")
        partial.eof()
        self.assertEqual(self.request(b"SUBMIT_JOB 71 1 1"), b"SUBMIT_OK\n")
        self.assertIsNone(self.server.poll())

    def test_connection_timeout_preserves_job_resource_accounting(self):
        self.stop_server()
        self.start_server("--idle-timeout-ms", "150")
        self.assertEqual(self.request(b"REGISTER_WORKER 1 1 1"), b"REGISTER_OK\n")
        self.assertEqual(self.request(b"SUBMIT_JOB 81 1 1"), b"SUBMIT_OK\n")
        worker = self.peer()
        worker.send(b"FETCH_JOB 1\n")
        self.assertEqual(worker.line(), b"RUN_JOB 81 1 1\n")
        worker.eof()
        self.assertEqual(self.request(b"SUBMIT_JOB 82 1 1"), b"SUBMIT_OK\n")
        self.assertEqual(self.request(b"FETCH_JOB 1"), b"NO_JOB\n")
        self.assertEqual(self.request(b"COMPLETE_JOB 81"), b"COMPLETE_OK\n")
        self.assertEqual(self.request(b"COMPLETE_JOB 81"), b"COMPLETE_ERROR COMPLETION_REJECTED\n")
        self.assertEqual(self.request(b"FETCH_JOB 1"), b"RUN_JOB 82 1 1\n")

    def test_idle_expiry_reclaims_connection_fds(self):
        self.stop_server()
        self.start_server("--backlog", "128", "--idle-timeout-ms", "1000")
        descriptors = Path(f"/proc/{self.server.pid}/fd")
        initial = len(list(descriptors.iterdir()))
        peers = [self.peer() for _ in range(32)]
        for peer in peers:
            peer.send(b"SCHEDULE_NEXT\n")
            self.assertEqual(peer.line(), b"SCHEDULE_NONE\n")  # 响应确认已被接受和处理。
        self.assertEqual(len(list(descriptors.iterdir())), initial + 32)
        for peer in peers:
            peer.eof()
        self.assertEqual(len(list(descriptors.iterdir())), initial)
        self.assertEqual(self.request(b"SCHEDULE_NEXT"), b"SCHEDULE_NONE\n")

    def test_idle_timeout_arguments(self):
        for options in (("--idle-timeout-ms", "-1"), ("--idle-timeout-ms", "2147483648"),
                        ("--idle-timeout-ms", "abc"), ("--idle-timeout-ms", "1x"),
                        ("--idle-timeout-ms",),
                        ("--idle-timeout-ms", "1", "--idle-timeout-ms", "2")):
            result = subprocess.run([SERVER_BINARY, *options], capture_output=True, text=True, timeout=3)
            self.assertEqual(result.returncode, 1)
            self.assertNotIn("Listening", result.stdout)
        self.stop_server()
        self.start_server("--idle-timeout-ms", "0")
        self.assertEqual(self.request(b"SCHEDULE_NEXT"), b"SCHEDULE_NONE\n")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("usage: epoll_server_integration_test.py <server-binary>")
    SERVER_BINARY = sys.argv.pop(1)
    unittest.main()
