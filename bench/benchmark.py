#!/usr/bin/env python3
"""Benchmark the two server implementations against each other with wrk.

For every scenario (concurrent connections x keep-alive on/off) each mode gets
a fresh server process, a warm-up run, then a measured wrk run. Besides wrk's
throughput, latency and error counts, it records the server's CPU time during
the measured run and its peak resident memory.

    cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
    python3 bench/benchmark.py
    python3 bench/benchmark.py --quick
    python3 bench/benchmark.py --connections 50,500 --keep-alive on --duration 20 \\
        --json results.json --markdown results.md

Requires wrk (brew install wrk). Standard library only otherwise.

Load generator and server share the machine's cores, so absolute numbers are
only comparable between runs on the same machine; the ratio between modes is
the interesting part.

Two things wrk doesn't show on its own, handled here:

* Starved connections. wrk only records requests that complete, so keep-alive
  connections the threaded server never serves (each worker stays blocked on
  one connection) are invisible in its latency and error stats. Two columns
  expose them: "served" estimates how many connections were making progress
  (Little's law: req/s x mean latency; it reads a little low because it leaves
  out wrk's own turnaround), and "new client" is the time a fresh connection,
  opened once a second during the run, waited for its response.
* Ephemeral port exhaustion. Every "Connection: close" request leaves a socket
  in TIME_WAIT for 2 x MSL (30 s on macOS); at around 31k of them new connects
  stall and throughput collapses. Close-mode runs therefore skip the warm-up,
  are short (--close-duration), and first wait for TIME_WAIT sockets to expire.
  `sudo sysctl net.inet.tcp.msl=1000` makes that wait much shorter.
"""

import argparse
import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
LUA_SCRIPT = os.path.join(HERE, "wrk_report.lua")
MODES = ("event", "threaded")

# Close-mode throughput collapses at ~31k TIME_WAIT sockets (macOS, loopback).
TIME_WAIT_DRAINED = 1000
TIME_WAIT_DANGER = 25_000


# ---------------------------------------------------------------------------
# Server process
# ---------------------------------------------------------------------------

def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Server:
    def __init__(self, binary, mode, root, server_threads):
        self.mode = mode
        self.port = free_port()
        self.log = tempfile.TemporaryFile()
        args = [binary, "--mode", mode, "--port", str(self.port), "--root", root]
        if mode == "threaded":
            args += ["--threads", str(server_threads)]
        # Both servers log every request to stdout; discard it so the benchmark
        # measures serving rather than terminal output.
        self.proc = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=self.log)
        self._wait_ready()

    def _wait_ready(self):
        deadline = time.time() + 15
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError(f"{self.mode} server exited during startup:\n{self.stderr()}")
            try:
                socket.create_connection(("127.0.0.1", self.port), timeout=0.2).close()
                return
            except OSError:
                time.sleep(0.05)
        raise RuntimeError(f"{self.mode} server did not start listening within 15 s")

    def stderr(self):
        self.log.seek(0)
        return self.log.read().decode(errors="replace")

    def cpu_seconds(self):
        """User + system CPU time used so far (ps reports "M:SS.ss")."""
        out = subprocess.run(["ps", "-o", "time=", "-p", str(self.proc.pid)],
                             capture_output=True, text=True, check=True).stdout.strip()
        seconds = 0.0
        for part in out.split(":"):
            seconds = seconds * 60 + float(part)
        return seconds

    def thread_count(self):
        out = subprocess.run(["ps", "-M", "-p", str(self.proc.pid)],
                             capture_output=True, text=True).stdout
        return max(len(out.splitlines()) - 1, 0)  # Minus the header line

    def stop(self):
        """Stops the server; returns its peak RSS in bytes (None if unknown)."""
        if self.proc.poll() is not None:
            return None
        self.proc.send_signal(signal.SIGTERM)
        deadline = time.time() + 10
        while True:
            pid, status, usage = os.wait4(self.proc.pid, os.WNOHANG)
            if pid:
                self.proc.returncode = os.waitstatus_to_exitcode(status)
                # ru_maxrss is in bytes on macOS, kilobytes on Linux.
                return usage.ru_maxrss if sys.platform == "darwin" else usage.ru_maxrss * 1024
            if time.time() > deadline:
                self.proc.kill()
                deadline = float("inf")
            time.sleep(0.02)


def check_serves(port, path):
    with socket.create_connection(("127.0.0.1", port), timeout=5) as s:
        s.sendall(f"GET {path} HTTP/1.1\r\nHost: bench\r\nConnection: close\r\n\r\n".encode())
        status_line = s.makefile("rb").readline().decode(errors="replace").strip()
    if " 200 " not in status_line + " ":
        raise RuntimeError(f"GET {path} returned {status_line!r}, expected 200 "
                           "(check --root and --path)")


def time_wait_count():
    out = subprocess.run(["netstat", "-an", "-p", "tcp"],
                         capture_output=True, text=True).stdout
    return out.count("TIME_WAIT")


def wait_for_time_wait_drain(max_wait=90):
    count = time_wait_count()
    if count <= TIME_WAIT_DRAINED:
        return
    print(f"    waiting for {count} TIME_WAIT sockets to expire ...", flush=True)
    deadline = time.time() + max_wait
    while count > TIME_WAIT_DRAINED and time.time() < deadline:
        time.sleep(1)
        count = time_wait_count()


class Prober(threading.Thread):
    """While the load runs, opens a fresh connection every `interval` seconds
    and times how long its request takes to get a response (None: timed out)."""

    def __init__(self, port, path, interval, timeout):
        super().__init__(daemon=True)
        self.port, self.interval, self.timeout = port, interval, timeout
        self.request = f"GET {path} HTTP/1.1\r\nHost: bench\r\nConnection: close\r\n\r\n".encode()
        self.results = []
        self.done = threading.Event()

    def run(self):
        while not self.done.wait(self.interval):
            self.results.append(self.probe())

    def probe(self):
        start = time.perf_counter()
        try:
            with socket.create_connection(("127.0.0.1", self.port), timeout=self.timeout) as s:
                s.sendall(self.request)
                s.settimeout(max(0.01, self.timeout - (time.perf_counter() - start)))
                if not s.recv(1):
                    return None
        except OSError:  # Includes socket.timeout
            return None
        return (time.perf_counter() - start) * 1e6

    def stop(self):
        self.done.set()
        self.join()
        return self.results


# ---------------------------------------------------------------------------
# wrk
# ---------------------------------------------------------------------------

def run_wrk(port, path, connections, keep_alive, duration, wrk_threads, timeout):
    cmd = ["wrk", "-t", str(max(1, min(wrk_threads, connections))), "-c", str(connections),
           "-d", f"{duration}s", "--timeout", f"{timeout}s", "-s", LUA_SCRIPT,
           f"http://127.0.0.1:{port}{path}"]
    if not keep_alive:
        cmd += ["--", "close"]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=duration + timeout + 60)
    for line in proc.stdout.splitlines():
        if line.startswith("BENCH_JSON "):
            return json.loads(line[len("BENCH_JSON "):])
    raise RuntimeError(f"wrk produced no results (exit {proc.returncode}):\n"
                       f"{proc.stdout}\n{proc.stderr}")


def bench_one(args, mode, connections, keep_alive):
    duration = args.duration if keep_alive else args.close_duration
    if not keep_alive:
        wait_for_time_wait_drain()
    server = Server(args.binary, mode, args.root, args.server_threads)
    try:
        check_serves(server.port, args.path)
        if args.warmup > 0 and keep_alive:  # Close mode can't spare the ports
            run_wrk(server.port, args.path, connections, keep_alive, args.warmup,
                    args.wrk_threads, args.timeout)
            time.sleep(0.5)  # Let the warm-up's connections drain
        cpu_before = server.cpu_seconds()
        prober = Prober(server.port, args.path, interval=min(1.0, duration / 2),
                        timeout=args.timeout)
        prober.start()
        try:
            wrk = run_wrk(server.port, args.path, connections, keep_alive, duration,
                          args.wrk_threads, args.timeout)
        finally:
            probes = prober.stop()
        cpu = server.cpu_seconds() - cpu_before
        time_wait = time_wait_count() if not keep_alive else None
        threads = server.thread_count()
        alive = server.proc.poll() is None
    finally:
        peak_rss = server.stop()

    if not alive:
        raise RuntimeError(f"{mode} server died during the run:\n{server.stderr()}")

    seconds = wrk["duration_us"] / 1e6
    requests = wrk["requests"]
    errors = wrk["errors"]
    rps = requests / seconds if seconds else 0.0
    # Little's law: connections in flight = throughput x time per request.
    # Only meaningful with keep-alive, where wrk's latency covers the whole
    # cycle; with close it leaves out connection setup.
    served = rps * wrk["latency_us"]["mean"] / 1e6 if keep_alive else None
    answered = sorted(p for p in probes if p is not None)
    probe_timeouts = len(probes) - len(answered)
    return {
        "mode": mode,
        "connections": connections,
        "keep_alive": keep_alive,
        "duration_s": seconds,
        "requests": requests,
        "requests_per_s": rps,
        "transfer_bytes_per_s": wrk["bytes"] / seconds if seconds else 0.0,
        "latency_us": wrk["latency_us"],
        "errors": errors,
        "errors_total": sum(errors.values()),
        "server_cpu_s": cpu,
        "server_cpu_util": cpu / seconds if seconds else 0.0,  # 1.0 = one core busy
        "server_cpu_us_per_request": cpu * 1e6 / requests if requests else None,
        "server_peak_rss_bytes": peak_rss,
        "server_threads": threads,
        "timeout": args.timeout,
        "served_connections": min(served, connections) if served is not None else None,
        "new_client_probes": len(probes),
        "new_client_timeouts": probe_timeouts,
        "new_client_median_us": answered[len(answered) // 2] if answered else None,
        "starved": bool(probes) and probe_timeouts * 2 >= len(probes),
        "port_limited": time_wait is not None and time_wait >= TIME_WAIT_DANGER,
    }


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------

def fmt_latency(us):
    if us >= 1_000_000:
        return f"{us / 1e6:.2f}s"
    if us >= 1000:
        return f"{us / 1000:.2f}ms"
    return f"{us:.0f}us"


def fmt_bytes(n):
    if n is None:
        return "?"
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return f"{n:.1f}{unit}" if unit != "B" else f"{n}B"
        n /= 1024


def fmt_errors(r):
    if not r["errors_total"]:
        return "0"
    parts = [f"{k} {v}" for k, v in r["errors"].items() if v]
    return f"{r['errors_total']} ({', '.join(parts)})"


COLUMNS = ("mode", "req/s", "p50", "p99", "max", "served", "new client", "CPU", "CPU/req",
           "peak RSS", "threads", "errors")


def fmt_probe(r):
    n, lost, median = r["new_client_probes"], r["new_client_timeouts"], r["new_client_median_us"]
    if not n:
        return "-"
    if lost == n:
        return f"timeout {lost}/{n}!"
    text = fmt_latency(median)
    return f"{text}, {lost}/{n} lost" + ("!" if r["starved"] else "") if lost else text


def row(r):
    lat = r["latency_us"]
    per_req = r["server_cpu_us_per_request"]
    return (
        r["mode"],
        f"{r['requests_per_s']:,.0f}",
        fmt_latency(lat["50"]),
        fmt_latency(lat["99"]),
        fmt_latency(lat["max"]),
        "-" if r["served_connections"] is None
        else f"~{r['served_connections']:.0f}/{r['connections']}",
        fmt_probe(r),
        f"{r['server_cpu_util'] * 100:.0f}%",
        f"{per_req:.1f}us" if per_req is not None else "-",
        fmt_bytes(r["server_peak_rss_bytes"]),
        str(r["server_threads"]),
        fmt_errors(r),
    )


def scenario_title(connections, keep_alive):
    return f"{connections} connections, {'keep-alive' if keep_alive else 'Connection: close'}"


def notes(results):
    out = []
    by_mode = {r["mode"]: r for r in results}
    ev, th = by_mode.get("event"), by_mode.get("threaded")
    if ev and th and th["requests_per_s"]:
        out.append(f"event / threaded throughput: "
                   f"{ev['requests_per_s'] / th['requests_per_s']:.2f}x")
    for r in results:
        if r["starved"]:
            served = r["served_connections"]
            detail = (f"only ~{served:.0f} of {r['connections']} connections were served, "
                      "and wrk's latency and error stats cover those alone"
                      if served is not None else "wrk's stats cover served connections alone")
            out.append(f"! {r['mode']}: new clients got no response within the "
                       f"{r['timeout']:g}s timeout; {detail}")
        if r["port_limited"]:
            out.append(f"! {r['mode']}: run ended near the TIME_WAIT limit, so its "
                       "throughput may be capped by ephemeral ports (try --close-duration 1)")
    return out


def print_scenario(results):
    first = results[0]
    print(f"\n=== {scenario_title(first['connections'], first['keep_alive'])} ===")
    rows = [COLUMNS] + [row(r) for r in results]
    widths = [max(len(rw[i]) for rw in rows) for i in range(len(COLUMNS))]
    for rw in rows:
        print("  ".join(cell.ljust(w) if i in (0, len(COLUMNS) - 1) else cell.rjust(w)
                        for i, (cell, w) in enumerate(zip(rw, widths))))
    for line in notes(results):
        print(line)


def write_markdown(path, scenarios, meta):
    lines = ["# Server benchmark: event vs threaded", "",
             f"- Path: `{meta['path']}`, keep-alive runs {meta['duration']}s "
             f"(after {meta['warmup']}s warm-up), close runs {meta['close_duration']}s, "
             f"wrk threads {meta['wrk_threads']}, "
             f"threaded pool size {meta['server_threads']}, CPUs {meta['cpus']}",
             "- served: estimated connections getting responses (req/s x mean latency, "
             "keep-alive only; reads slightly low). new client: response time for a fresh "
             "connection opened during the load; `!` marks starvation, which wrk's own "
             "stats omit.",
             "- CPU: server CPU time / wall time (100% = one core). "
             "CPU/req: server CPU time per completed request.", ""]
    for results in scenarios:
        first = results[0]
        lines += [f"## {scenario_title(first['connections'], first['keep_alive'])}", "",
                  "| " + " | ".join(COLUMNS) + " |",
                  "|" + "|".join("---" for _ in COLUMNS) + "|"]
        lines += ["| " + " | ".join(row(r)) + " |" for r in results]
        lines += [""] + [f"- {n}" for n in notes(results)] + [""]
    with open(path, "w") as f:
        f.write("\n".join(lines))


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def build_warning(binary):
    """Warns when the binary's CMake build isn't optimized or is instrumented."""
    cache = os.path.join(os.path.dirname(os.path.abspath(binary)), "CMakeCache.txt")
    try:
        with open(cache) as f:
            entries = dict(line.rstrip("\n").split("=", 1) for line in f
                           if "=" in line and not line.startswith(("#", "//")))
    except OSError:
        return None
    build_type = entries.get("CMAKE_BUILD_TYPE:STRING", "")
    sanitize = entries.get("SANITIZE:STRING", "")
    if sanitize:
        return f"binary is built with sanitizers ({sanitize}); results will be far slower"
    if build_type not in ("Release", "RelWithDebInfo"):
        return (f"binary is a {build_type or 'default (unoptimized)'} build; configure with "
                "-DCMAKE_BUILD_TYPE=Release for meaningful numbers")
    return None


def int_list(text):
    return [int(x) for x in text.split(",") if x]


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--binary", default=os.path.join(REPO, "build", "CppHTTPServer"))
    p.add_argument("--root", default=os.path.join(REPO, "www"), help="document root to serve")
    p.add_argument("--path", default="/index.html", help="URL path to request")
    p.add_argument("--modes", default=",".join(MODES), help="comma-separated: event,threaded")
    p.add_argument("--connections", type=int_list, default=[10, 100, 1000],
                   help="comma-separated concurrency levels (default 10,100,1000)")
    p.add_argument("--keep-alive", choices=("on", "off", "both"), default="both")
    p.add_argument("--duration", type=int, default=10,
                   help="seconds per measured keep-alive run")
    p.add_argument("--close-duration", type=int, default=2,
                   help="seconds per measured Connection: close run (kept short: see above)")
    p.add_argument("--warmup", type=int, default=2, help="seconds of warm-up per run (0: none)")
    p.add_argument("--timeout", type=int, default=2, help="wrk per-request timeout in seconds")
    p.add_argument("--wrk-threads", type=int, default=4)
    p.add_argument("--server-threads", type=int, default=4, help="thread pool size, threaded mode")
    p.add_argument("--quick", action="store_true",
                   help="short smoke run: 10,100 connections, 3 s, 1 s warm-up")
    p.add_argument("--json", metavar="FILE", help="write raw results as JSON")
    p.add_argument("--markdown", metavar="FILE", help="write the tables as Markdown")
    args = p.parse_args()

    if args.quick:
        args.connections, args.duration, args.warmup = [10, 100], 3, 1
        args.close_duration = min(args.close_duration, 1)
    modes = [m for m in args.modes.split(",") if m]
    for m in modes:
        if m not in MODES:
            p.error(f"unknown mode {m!r}")
    if not shutil.which("wrk"):
        p.error("wrk not found on PATH (brew install wrk)")
    if not os.access(args.binary, os.X_OK):
        p.error(f"server binary not found: {args.binary} (build it first)")
    keep_alive = {"on": [True], "off": [False], "both": [True, False]}[args.keep_alive]
    warning = build_warning(args.binary)
    if warning:
        print(f"WARNING: {warning}")

    meta = {k: getattr(args, k) for k in ("path", "duration", "close_duration", "warmup",
                                          "timeout", "wrk_threads", "server_threads")}
    meta["cpus"] = os.cpu_count()
    runs = len(args.connections) * len(modes)
    est = sum(runs * (args.duration + args.warmup + 1.5) if ka else runs * (args.close_duration + 1.5)
              for ka in keep_alive)
    print(f"Running {runs * len(keep_alive)} benchmarks, about {est / 60:.1f} min "
          "plus any waits for TIME_WAIT sockets. "
          f"Serving {args.path} from {args.root}")

    scenarios = []
    for ka in keep_alive:
        for conns in args.connections:
            results = []
            for mode in modes:
                print(f"  {mode:8} {scenario_title(conns, ka)} ...", flush=True)
                results.append(bench_one(args, mode, conns, ka))
                time.sleep(1)  # Let sockets from the previous run wind down
            scenarios.append(results)
            print_scenario(results)

    if args.json:
        with open(args.json, "w") as f:
            json.dump({"meta": meta, "results": [r for s in scenarios for r in s]}, f, indent=2)
        print(f"\nWrote {args.json}")
    if args.markdown:
        write_markdown(args.markdown, scenarios, meta)
        print(f"Wrote {args.markdown}")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(130)
