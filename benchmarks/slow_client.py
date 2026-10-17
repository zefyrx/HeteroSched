"""Linux 慢连接实验：确认 accept 后保持半条请求，测量结束再补齐。"""

from pathlib import Path
import select
import socket
import threading
import time

import tcp_benchmark as benchmark


def endpoint_hex(endpoint):
    address, port = endpoint
    return f"{socket.inet_aton(address)[::-1].hex().upper()}:{port:04X}"


def wait_until_accepted(pid, connection, timeout=2):
    """ESTABLISHED 不等于 accept；还要求对端 inode 出现在目标进程 fd 中。"""
    local = endpoint_hex(connection.getpeername())
    remote = endpoint_hex(connection.getsockname())
    deadline = time.monotonic() + timeout
    while True:
        rows = Path(f"/proc/{pid}/net/tcp").read_text().splitlines()[1:]
        inodes = {row.split()[9] for row in rows
                  if row.split()[1:4] == [local, remote, "01"]}
        for fd in Path(f"/proc/{pid}/fd").iterdir():
            try:
                target = str(fd.readlink())
            except FileNotFoundError:  # fd 可能在读取目录后关闭。
                continue
            for inode in inodes:
                if inode != "0" and target == f"socket:[{inode}]":
                    return {"accepted_observed": True, "socket_inode": inode,
                            "local": local, "remote": remote}
        if time.monotonic() >= deadline:
            raise TimeoutError("slow socket was not accepted by target process")
        # 等待一个可验证条件，截止时间保证失败时不会无限轮询。
        time.sleep(0.005)


class SlowClient:
    prefix = b"SCHEDULE_NEXT "
    interval_s = 0.5
    max_body_bytes = 128

    def __init__(self, host, port, pid):
        self.host, self.port, self.pid = host, port, pid
        self.connection = None
        self.thread = None
        self.stop_event = threading.Event()
        self.first_drip = threading.Event()
        self.error = None
        self.body_bytes = len(self.prefix)
        self.metadata = {"count": 1, "prefix": self.prefix.decode(),
                         "drip_interval_s": self.interval_s,
                         "max_body_bytes": self.max_body_bytes}

    def __enter__(self):
        try:
            self.connection = socket.create_connection((self.host, self.port), timeout=1)
            self.connection.sendall(self.prefix)
            self.metadata.update(wait_until_accepted(self.pid, self.connection))
            self.thread = threading.Thread(target=self.drip, daemon=True)
            self.thread.start()
            return self
        except BaseException:
            self.close()
            raise

    def drip(self):
        try:
            while not self.stop_event.wait(self.interval_s):
                if self.body_bytes >= self.max_body_bytes:
                    raise RuntimeError("slow request reached protocol size limit")
                self.connection.sendall(b" ")
                self.body_bytes += 1
                self.first_drip.set()
        except Exception as error:
            self.error = f"{type(error).__name__}: {error}"

    def stop_dripping(self):
        self.stop_event.set()
        if self.thread is not None:
            self.thread.join(timeout=2)
            if self.thread.is_alive():
                raise RuntimeError("slow sender did not stop")

    def finish(self):
        self.stop_dripping()
        self.metadata["body_bytes_before_newline"] = self.body_bytes
        if self.error:
            raise RuntimeError(self.error)
        readable, _, _ = select.select([self.connection], [], [], 0)
        if readable:
            raise RuntimeError("slow connection responded or closed before newline")
        self.metadata["no_response_before_newline"] = True
        self.connection.sendall(b"\n")
        self.connection.shutdown(socket.SHUT_WR)
        response = b""
        while b"\n" not in response:
            chunk = self.connection.recv(4096)
            if not chunk:
                raise ConnectionError("slow connection closed before complete response")
            response += chunk
            if len(response) > len(b"SCHEDULE_NONE\n"):
                raise ValueError("unexpected slow-client response length")
        if response != b"SCHEDULE_NONE\n":
            raise ValueError(f"unexpected slow-client response: {response!r}")
        self.metadata["release_response"] = response.decode()

    def close(self):
        try:
            self.stop_dripping()
        finally:
            if self.connection is not None:
                self.connection.close()

    def __exit__(self, exc_type, exc, traceback):
        self.close()


def run_slow_case(host, port, timeout, requests, warmup, concurrency, pid):
    # 预热先完成，慢连接确认 accepted 后才进入正常请求的 barrier。
    for _ in range(warmup):
        benchmark.request_once(host, port, timeout)
    with SlowClient(host, port, pid) as slow:
        run = benchmark.run_benchmark(host, port, timeout, requests, 0, concurrency, pid)
        run["warmup"] = warmup
        run["warmup_before_slow_setup"] = True
        run["slow_client"] = slow.metadata
        try:
            slow.finish()
            slow.metadata["valid"] = True
        except Exception as error:
            # 保留已经测得的请求结果，由调用者保存后标记实验无效。
            slow.metadata.update(valid=False, error=f"{type(error).__name__}: {error}")
    return run
