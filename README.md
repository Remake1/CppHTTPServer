## HTTP static files server in C++ (using Socket API)


This is a simple HTTP static files server implemented in C++. Serves files from a specified directory over HTTP.

Two implementations:
1. [HttpServer](src/HttpServer.hpp) - blocking I/O, using thread pool for concurrency.
2. [EventDrivenHttpServer](src/EventDrivenServer.hpp) - non-blocking I/O, using `kqueue` for event loop.

### Running the server

CMake required.

```bash
cmake -B cmake-build-debug && cmake --build cmake-build-debug
```
