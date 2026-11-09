#!/usr/bin/env python3
"""Black-box tests: run the server binary and talk raw HTTP to it over TCP.

Standard library only. Run directly or through CTest:

    python3 tests/integration/test_server.py --binary build/CppHTTPServer --mode event

Each test class starts its own server on a free port with a generated document
root. After the class finishes, the server's stderr is checked for sanitizer
reports, so a memory error or data race fails the run even if every response
looked correct.
"""

import argparse
import os
import re
import resource
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ARGS = None  # Set in __main__: binary path and mode

# Far larger than loopback socket buffers, so sending it takes many writes.
BIG_FILE_SIZE = 16_000_000

# Short server timeouts for the classes that test them (production: 15 s / 10 s).
FAST_TIMEOUTS = ("--idle-timeout", "2", "--request-timeout", "1")
SANITIZER_REPORT = re.compile(rb"ERROR: AddressSanitizer|WARNING: ThreadSanitizer|runtime error:")


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

def make_doc_root(base):
    """<base>/www is served; <base>/www-secret must never be reachable."""
    root = os.path.join(base, "www")
    secret = os.path.join(base, "www-secret")
    os.makedirs(os.path.join(root, "sub"))
    os.makedirs(secret)
    files = {
        "index.html": b"<html>index</html>",
        "style.css": b"body { color: red; }",
        "script.js": b"console.log(1);",
        "empty.txt": b"",
        "a..b.txt": b"dots",
    }
    for name, content in files.items():
        with open(os.path.join(root, name), "wb") as f:
            f.write(content)
    with open(os.path.join(root, "big.bin"), "wb") as f:
        f.truncate(BIG_FILE_SIZE)  # Sparse file of zeros
    with open(os.path.join(secret, "key.txt"), "wb") as f:
        f.write(b"TOPSECRET")
    os.symlink(os.path.join(secret, "key.txt"), os.path.join(root, "link.txt"))
    return root


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Server:
    def __init__(self, binary, mode, root, max_fds=None, extra_args=()):
        self.port = free_port()
        self.log = tempfile.TemporaryFile()
        preexec = None
        if max_fds is not None:
            preexec = lambda: resource.setrlimit(resource.RLIMIT_NOFILE, (max_fds, max_fds))
        self.proc = subprocess.Popen(
            [binary, "--mode", mode, "--port", str(self.port), "--root", root, *extra_args],
            stdout=subprocess.DEVNULL, stderr=self.log, preexec_fn=preexec)
        self._wait_ready()

    def _wait_ready(self):
        deadline = time.time() + 30
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError(f"server exited during startup:\n{self.output()}")
            try:
                socket.create_connection(("127.0.0.1", self.port), timeout=0.2).close()
                return
            except OSError:
                time.sleep(0.05)
        raise RuntimeError("server did not start listening within 30 s")

    def alive(self):
        return self.proc.poll() is None

    def output(self):
        self.log.seek(0)
        return self.log.read().decode(errors="replace")

    def stop(self):
        if self.alive():
            self.proc.send_signal(signal.SIGTERM)
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        self.log.seek(0)
        return self.log.read()


# ---------------------------------------------------------------------------
# HTTP helpers (deliberately raw: the point is to control exact bytes)
# ---------------------------------------------------------------------------

def get(path, headers=""):
    return f"GET {path} HTTP/1.1\r\nHost: test\r\n{headers}\r\n".encode()


def read_until_close(sock, timeout=3.0):
    """Returns (bytes, how) where how is 'EOF', 'RST', or 'OPEN' (timed out)."""
    sock.settimeout(timeout)
    out = b""
    try:
        while True:
            chunk = sock.recv(1 << 20)
            if not chunk:
                return out, "EOF"
            out += chunk
    except socket.timeout:
        return out, "OPEN"
    except ConnectionResetError:
        return out, "RST"


class Response:
    def __init__(self, status, headers, body):
        self.status, self.headers, self.body = status, headers, body

    def __repr__(self):
        return f"<{self.status} {len(self.body)}B>"


def parse_responses(data):
    """Split a byte stream into responses using Content-Length only, as a
    keep-alive client must. A response without Content-Length is an error."""
    responses = []
    while data:
        head, sep, data = data.partition(b"\r\n\r\n")
        if not sep:
            raise AssertionError(f"truncated response head: {head[:200]!r}")
        lines = head.decode("latin-1").split("\r\n")
        headers = {}
        for line in lines[1:]:
            name, _, value = line.partition(":")
            headers[name.strip().lower()] = value.strip()
        if "content-length" not in headers:
            raise AssertionError(f"response without Content-Length: {lines[0]}")
        n = int(headers["content-length"])
        if len(data) < n:
            raise AssertionError(f"body truncated: got {len(data)} of {n} bytes")
        responses.append(Response(int(lines[0].split()[1]), headers, data[:n]))
        data = data[n:]
    return responses


class KeepAliveClient:
    """One connection, many sequential requests, framed by Content-Length."""

    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.buf = b""

    def request(self, path, headers=""):
        self.sock.sendall(get(path, headers))
        while b"\r\n\r\n" not in self.buf:
            self._recv()
        head = self.buf.partition(b"\r\n\r\n")[0]
        match = re.search(rb"(?i)\r\ncontent-length:\s*(\d+)", head)
        assert match, f"no Content-Length in {head!r}"
        total = len(head) + 4 + int(match.group(1))
        while len(self.buf) < total:
            self._recv()
        response, self.buf = self.buf[:total], self.buf[total:]
        return parse_responses(response)[0]

    def _recv(self):
        chunk = self.sock.recv(1 << 20)
        if not chunk:
            raise ConnectionError("server closed the connection")
        self.buf += chunk

    def close(self):
        self.sock.close()


# ---------------------------------------------------------------------------
# Base class
# ---------------------------------------------------------------------------

class ServerTestCase(unittest.TestCase):
    max_fds = None
    event_only = False
    server_args = ()

    @classmethod
    def setUpClass(cls):
        if cls.event_only and ARGS.mode != "event":
            raise unittest.SkipTest("event-driven server only")
        cls.tmp = tempfile.mkdtemp(prefix="httpserver-test-")
        cls.root = make_doc_root(cls.tmp)
        cls.server = Server(ARGS.binary, ARGS.mode, cls.root, cls.max_fds, cls.server_args)
        cls.port = cls.server.port

    @classmethod
    def tearDownClass(cls):
        output = cls.server.stop()
        shutil.rmtree(cls.tmp, ignore_errors=True)
        match = SANITIZER_REPORT.search(output)
        if match:
            start = max(0, match.start() - 200)
            raise AssertionError("sanitizer report in server output:\n"
                                 + output[start:start + 6000].decode(errors="replace"))

    def tearDown(self):
        self.assertTrue(self.server.alive(),
                        f"server process died:\n{self.server.output()[-4000:]}")

    def connect(self, timeout=5):
        return socket.create_connection(("127.0.0.1", self.port), timeout=timeout)

    def exchange(self, raw, timeout=3.0):
        """Send raw bytes, read until the server closes (or timeout)."""
        with self.connect() as s:
            s.sendall(raw)
            data, how = read_until_close(s, timeout)
        return parse_responses(data), how


# ---------------------------------------------------------------------------
# Tests for both servers
# ---------------------------------------------------------------------------

class FileServing(ServerTestCase):
    def test_serves_file_with_content_type(self):
        [r], _ = self.exchange(get("/style.css", "Connection: close\r\n"))
        self.assertEqual(r.status, 200)
        self.assertEqual(r.headers["content-type"], "text/css")
        self.assertEqual(r.body, b"body { color: red; }")

    def test_root_serves_index(self):
        [r], _ = self.exchange(get("/", "Connection: close\r\n"))
        self.assertEqual(r.body, b"<html>index</html>")

    def test_missing_file_is_404(self):
        [r], _ = self.exchange(get("/nope.html", "Connection: close\r\n"))
        self.assertEqual(r.status, 404)

    def test_empty_file_has_content_length_zero(self):
        c = KeepAliveClient(self.port)
        r = c.request("/empty.txt")
        self.assertEqual((r.status, r.headers["content-length"]), (200, "0"))
        self.assertEqual(c.request("/style.css").status, 200)  # Connection still usable
        c.close()

    def test_directory_is_404(self):
        for path in ["/sub", "/sub/"]:
            with self.subTest(path=path):
                [r], _ = self.exchange(get(path, "Connection: close\r\n"))
                self.assertEqual(r.status, 404)

    def test_large_file_is_intact(self):
        [r], _ = self.exchange(get("/big.bin", "Connection: close\r\n"), timeout=30)
        self.assertEqual(len(r.body), BIG_FILE_SIZE)
        self.assertEqual(r.body.count(0), BIG_FILE_SIZE)

    def test_paths_inside_root(self):
        for path in ["/sub/../style.css", "/a..b.txt", "/style.css?v=1"]:
            with self.subTest(path=path):
                [r], _ = self.exchange(get(path, "Connection: close\r\n"))
                self.assertEqual(r.status, 200)

    def test_path_escapes_are_rejected(self):
        for path in ["-secret/key.txt", "/../www-secret/key.txt", "/sub/../../www-secret/key.txt",
                     "/../../../../../../etc/passwd", "//etc/passwd", "/link.txt", "*",
                     "http://test/style.css"]:
            with self.subTest(path=path):
                [r], _ = self.exchange(get(path, "Connection: close\r\n"))
                self.assertEqual(r.status, 404)
                self.assertNotIn(b"TOPSECRET", r.body)
                self.assertNotIn(b"root:", r.body)


class KeepAlive(ServerTestCase):
    def test_many_requests_on_one_connection(self):
        c = KeepAliveClient(self.port)
        for path in ["/", "/style.css", "/nope", "/script.js", "/empty.txt"] * 10:
            r = c.request(path)
            self.assertEqual(r.headers["connection"], "keep-alive")
        c.close()

    def test_http11_default_stays_open(self):
        with self.connect() as s:
            s.sendall(get("/style.css"))
            data, how = read_until_close(s, 0.5)
        self.assertEqual(parse_responses(data)[0].headers["connection"], "keep-alive")
        self.assertEqual(how, "OPEN")

    def test_connection_close_is_honoured(self):
        for header in ["Connection: close\r\n", "Connection: Close\r\n", "Connection: TE, close\r\n"]:
            with self.subTest(header=header):
                [r], how = self.exchange(get("/style.css", header))
                self.assertEqual(r.headers["connection"], "close")
                self.assertEqual(how, "EOF")

    def test_http10_defaults_to_close(self):
        [r], how = self.exchange(b"GET /style.css HTTP/1.0\r\n\r\n")
        self.assertEqual((r.headers["connection"], how), ("close", "EOF"))

    def test_http10_keep_alive_any_case(self):
        with self.connect() as s:
            s.sendall(b"GET /style.css HTTP/1.0\r\nConnection: Keep-Alive\r\n\r\n")
            data, how = read_until_close(s, 0.5)
        self.assertEqual(parse_responses(data)[0].headers["connection"], "keep-alive")
        self.assertEqual(how, "OPEN")


class Robustness(ServerTestCase):
    def test_survives_clients_aborting_large_downloads(self):
        # Writing to a reset connection raises SIGPIPE, which kills the
        # process unless the server suppresses it.
        for linger_reset in [True] * 5 + [False] * 5:
            s = self.connect()
            s.sendall(get("/big.bin"))
            s.recv(1000)
            if linger_reset:  # Close with RST instead of FIN
                s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
            s.close()
            time.sleep(0.1)
        self.assertTrue(self.server.alive())
        [r], _ = self.exchange(get("/style.css", "Connection: close\r\n"))
        self.assertEqual(r.status, 200)

    def test_concurrent_clients(self):
        # Exercises the thread pool (and TSan) in threaded mode.
        errors = []

        def worker():
            try:
                c = KeepAliveClient(self.port)
                for _ in range(20):
                    if c.request("/style.css").body != b"body { color: red; }":
                        errors.append("wrong body")
                c.close()
            except Exception as e:  # noqa: BLE001 - collected and reported below
                errors.append(repr(e))

        # Fewer clients than threaded-mode workers: that server serves one
        # connection per worker, so more would just queue (a known limitation).
        threads = [threading.Thread(target=worker) for _ in range(4)]
        for t in threads:
            t.start()
        for t in threads:
            t.join(60)
        self.assertEqual(errors, [])


# ---------------------------------------------------------------------------
# Event-driven server only. The threaded server is known to fail these:
# it drops pipelined requests and request bodies, and idle keep-alive
# connections block its workers. See README.
# ---------------------------------------------------------------------------

class Pipelining(ServerTestCase):
    event_only = True

    def test_pipelined_requests_answered_in_order(self):
        rs, how = self.exchange(get("/style.css") + get("/script.js") + get("/", "Connection: close\r\n"))
        self.assertEqual([r.headers["content-type"] for r in rs],
                         ["text/css", "application/javascript", "text/html"])
        self.assertEqual(how, "EOF")

    def test_body_in_later_packet_is_not_parsed_as_request(self):
        with self.connect() as s:
            s.sendall(b"POST /x HTTP/1.1\r\nHost: test\r\nContent-Length: 20\r\n\r\n")
            time.sleep(0.2)
            s.sendall(b"GET /notarequest 123")  # 20 bytes of body
            time.sleep(0.2)
            s.sendall(get("/style.css", "Connection: close\r\n"))
            data, _ = read_until_close(s)
        self.assertEqual([r.status for r in parse_responses(data)], [404, 200])

    def test_request_sent_one_byte_at_a_time(self):
        with self.connect() as s:
            for b in get("/style.css", "Connection: close\r\n"):
                s.send(bytes([b]))
                time.sleep(0.001)
            data, _ = read_until_close(s)
        self.assertEqual(parse_responses(data)[0].status, 200)

    def test_half_close_after_requests(self):
        with self.connect() as s:
            s.sendall(get("/style.css") + get("/script.js"))
            s.shutdown(socket.SHUT_WR)
            data, how = read_until_close(s)
        self.assertEqual(len(parse_responses(data)), 2)
        self.assertEqual(how, "EOF")

    def test_request_arriving_mid_write_waits_its_turn(self):
        # Regression: a second request arriving while a large response was
        # being written used to restart the first response from byte 0.
        with self.connect() as s:
            s.sendall(get("/big.bin"))
            time.sleep(0.3)  # Response fills the socket buffer and stalls
            s.sendall(get("/style.css", "Connection: close\r\n"))
            data, _ = read_until_close(s, timeout=30)
        rs = parse_responses(data)
        self.assertEqual(len(rs), 2)
        self.assertEqual(len(rs[0].body), BIG_FILE_SIZE)
        self.assertEqual(rs[1].body, b"body { color: red; }")

    def test_idle_connections_do_not_block_new_clients(self):
        idle = []
        for _ in range(20):
            c = KeepAliveClient(self.port)
            c.request("/style.css")
            idle.append(c)
        [r], _ = self.exchange(get("/style.css", "Connection: close\r\n"))
        self.assertEqual(r.status, 200)
        for c in idle:
            c.close()


class ErrorHandling(ServerTestCase):
    event_only = True

    def test_errors_reply_then_close(self):
        cases = [
            ("headers too large", b"GET / HTTP/1.1\r\nX: " + b"a" * 20000 + b"\r\n\r\n", 431),
            ("headers never end", b"GET / HTTP/1.1\r\nX: " + b"a" * 20000, 431),
            ("body too large", b"POST / HTTP/1.1\r\nContent-Length: 2000000\r\n\r\n", 413),
            ("chunked body", b"POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n", 501),
            ("non-numeric Content-Length", b"POST / HTTP/1.1\r\nContent-Length: abc\r\n\r\n", 400),
            ("negative Content-Length", b"POST / HTTP/1.1\r\nContent-Length: -1\r\n\r\n", 400),
            ("garbage request line", b"HELLO\r\n\r\n", 400),
            ("unsupported version", b"GET / HTTP/2.0\r\n\r\n", 400),
        ]
        for name, raw, status in cases:
            with self.subTest(name):
                rs, how = self.exchange(raw)
                self.assertEqual(rs[0].status, status)
                self.assertEqual(rs[0].headers["connection"], "close")
                self.assertIn(how, ("EOF", "RST"))


class Timeouts(ServerTestCase):
    """Server started with 2 s idle timeout, 1 s to finish a started request."""
    event_only = True
    server_args = FAST_TIMEOUTS

    def test_timeouts(self):
        results = {}

        def watch(name, sock):
            start = time.time()
            data, how = read_until_close(sock, timeout=10)
            results[name] = (how, time.time() - start, len(data))

        def idle():
            s = self.connect()
            s.sendall(get("/style.css"))
            s.recv(4096)
            return s

        def unfinished():
            s = self.connect()
            s.sendall(b"GET /style.css HTTP/1.1\r\nHost: test\r\n")
            return s

        def slowloris():
            # Keeps sending header bytes so the connection is never idle,
            # but never finishes the request.
            s = self.connect()
            s.sendall(b"GET / HTTP/1.1\r\n")

            def drip():
                try:
                    for _ in range(20):
                        time.sleep(0.25)
                        s.send(b"X")
                except OSError:
                    pass
            threading.Thread(target=drip, daemon=True).start()
            return s

        def stalled_reader():
            # Requests 50 MB and never reads: server's writes stop making progress.
            s = socket.socket()
            s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
            s.connect(("127.0.0.1", self.port))
            s.sendall(get("/big.bin"))
            time.sleep(3.5)  # Past the 2 s idle timeout
            return s

        cases = {"idle": idle, "unfinished": unfinished, "slowloris": slowloris}
        threads = [threading.Thread(target=lambda n=n, f=f: watch(n, f())) for n, f in cases.items()]
        threads.append(threading.Thread(target=lambda: watch("stalled", stalled_reader())))
        for t in threads:
            t.start()
        for t in threads:
            t.join(30)

        # Expected close time = timeout + up to one 250 ms tick; allow slack for CI.
        for name, low, high in [("idle", 1.9, 3.5), ("unfinished", 0.9, 2.5), ("slowloris", 0.9, 2.5)]:
            with self.subTest(name):
                how, elapsed, _ = results[name]
                self.assertIn(how, ("EOF", "RST"))
                self.assertGreaterEqual(elapsed, low)
                self.assertLessEqual(elapsed, high)
        with self.subTest("stalled"):
            how, _, received = results["stalled"]
            self.assertIn(how, ("EOF", "RST"))
            self.assertLess(received, BIG_FILE_SIZE)


class FdExhaustion(ServerTestCase):
    """Server limited to 40 fds; 60 clients connect."""
    event_only = True
    max_fds = 40
    server_args = FAST_TIMEOUTS  # 250 ms tick, so accepting resumes quickly

    def test_does_not_spin_and_recovers(self):
        socks = []
        for _ in range(60):
            try:
                socks.append(socket.create_connection(("127.0.0.1", self.port), timeout=1))
            except OSError:
                pass
        time.sleep(0.5)

        # A busy loop would burn ~1 s of CPU per second.
        cpu_before = self._server_cpu_seconds()
        time.sleep(1)
        cpu_used = self._server_cpu_seconds() - cpu_before
        self.assertLess(cpu_used, 0.3, "server is busy-looping while out of fds")

        for s in socks:
            s.close()
        time.sleep(0.5)  # Next tick resumes accepting
        [r], _ = self.exchange(get("/style.css", "Connection: close\r\n"))
        self.assertEqual(r.status, 200)

    def _server_cpu_seconds(self):
        out = subprocess.run(["ps", "-o", "time=", "-p", str(self.server.proc.pid)],
                             capture_output=True, text=True, check=True).stdout.strip()
        # Formats: "M:SS.ss" or "H:MM:SS.ss"
        seconds = 0.0
        for part in out.split(":"):
            seconds = seconds * 60 + float(part)
        return seconds


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", required=True, help="path to the CppHTTPServer executable")
    parser.add_argument("--mode", choices=["event", "threaded"], default="event")
    ARGS, rest = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0]] + rest, verbosity=2)
