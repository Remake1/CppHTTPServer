## HTTP static files server in C++ (using Socket API)

[![CI](https://github.com/Remake1/CppHTTPServer/actions/workflows/ci.yml/badge.svg)](https://github.com/Remake1/CppHTTPServer/actions/workflows/ci.yml)

This is a simple HTTP static files server implemented in C++. Serves files from a specified directory over HTTP.

Two implementations:
1. [HttpServer](src/HttpServer.hpp) - blocking I/O, using thread pool for concurrency.
2. [EventDrivenHttpServer](src/EventDrivenServer.hpp) - non-blocking I/O, using `kqueue` for event loop.

### Running the server

CMake required. macOS (or BSD) only, since the event loop uses `kqueue`.

```bash
cmake -B build && cmake --build build
./build/CppHTTPServer --mode event --port 3000 --root ./www
```

Options: `--mode event|threaded`, `--port N`, `--root DIR`, `--threads N` (threaded mode),
`--idle-timeout SEC` and `--request-timeout SEC` (event mode; defaults 15 and 10).

### Tests

```bash
cmake -B build && cmake --build build
ctest --test-dir build --output-on-failure --parallel 8
```

- **Unit tests** ([tests/unit](tests/unit), GoogleTest): request framing and limits, keep-alive
  rules, response serialisation, path mapping and escape attempts, `Connection` buffering over
  a socket pair, and `EventLoop` (interest changes, timers, cross-thread `stop()`, removing an fd
  from inside its own callback).
- **Integration tests** ([tests/integration/test_server.py](tests/integration/test_server.py),
  Python standard library only): start the real binary and send raw bytes over TCP: keep-alive,
  pipelining, split request bodies, clients aborting downloads, oversized and malformed requests,
  timeouts (including a slowloris-style client), and more clients than the fd limit allows. After each
  test class, the server's stderr is checked for sanitizer reports.

Build with sanitizers using `-DSANITIZE=address,undefined` or `-DSANITIZE=thread`. CI runs the
suite in three configurations: Release, ASan + UBSan, and TSan.

**Known limitations of the threaded server**, which is why the event-only test classes
(pipelining, error handling, timeouts, connection limit) don't run against it: it drops pipelined
requests and request bodies, and each idle keep-alive connection occupies a worker thread until
the client disconnects.

### Benchmark

[bench/benchmark.py](bench/benchmark.py) compares the two servers with [wrk](https://github.com/wg/wrk)
(`brew install wrk`; otherwise Python standard library only). For each concurrency level, with
keep-alive and with `Connection: close`, it starts a fresh server in each mode and measures
throughput, latency percentiles and errors. It also records the server's CPU use (in total and per
request), its peak RSS and its thread count.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
python3 bench/benchmark.py                 # 10,100,1000 connections, both modes (~4 min)
python3 bench/benchmark.py --quick         # short smoke run
python3 bench/benchmark.py --connections 50,500 --keep-alive on --server-threads 12 \
    --json results.json --markdown results.md
```

wrk records only the requests that complete, so connections a server never serves don't appear in
its latency or error numbers. The threaded server's workers each block on one keep-alive
connection, which is exactly what happens to it. Two columns expose this: **served** estimates how
many connections were making progress (req/s × mean latency), and **new client** is the response
time of a fresh connection opened every second during the run (`timeout ...!` = starved).

Each `Connection: close` request leaves a socket in TIME_WAIT for 30 s on macOS, and new
connections stall at around 31k of them. So close-mode runs are short (`--close-duration`, default
2 s), skip the warm-up, and first wait for earlier TIME_WAIT sockets to expire.
`sudo sysctl net.inet.tcp.msl=1000` makes those waits much shorter.
