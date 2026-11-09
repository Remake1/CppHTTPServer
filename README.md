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
