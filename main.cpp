#include "src/EventDrivenServer.hpp"
#include "src/HttpServer.hpp"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void usage(const char* prog) {
    std::cerr << "Usage: " << prog
              << " [--mode event|threaded] [--port N] [--root DIR] [--threads N]\n"
                 "         [--idle-timeout SEC] [--request-timeout SEC]\n"
                 "  --mode             server implementation (default: event)\n"
                 "  --port             TCP port to listen on (default: 3000)\n"
                 "  --root             directory to serve (default: ./www)\n"
                 "  --threads          worker threads for --mode threaded (default: 4)\n"
                 "  --idle-timeout     event mode: close idle connections after SEC (default: 15)\n"
                 "  --request-timeout  event mode: time allowed to send a request (default: 10)\n";
}

// Parses fractional seconds ("1.5") into milliseconds.
std::chrono::milliseconds secondsArg(const char* value) {
    return std::chrono::milliseconds(static_cast<long long>(std::atof(value) * 1000));
}

} // namespace

int main(int argc, char** argv) {
    ServerConfig config;
    config.port = 3000;
    config.doc_root = "./www";
    std::string mode = "event";

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
            return 0;
        }
        if (i + 1 >= argc) {
            usage(argv[0]);
            return 2;
        }
        const char* value = argv[++i];
        if (arg == "--mode") {
            mode = value;
        } else if (arg == "--port") {
            config.port = std::atoi(value);
        } else if (arg == "--root") {
            config.doc_root = value;
        } else if (arg == "--threads") {
            config.thread_pool_size = static_cast<size_t>(std::atoi(value));
        } else if (arg == "--idle-timeout") {
            config.idle_timeout = secondsArg(value);
        } else if (arg == "--request-timeout") {
            config.request_timeout = secondsArg(value);
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    try {
        if (mode == "event") {
            EventDrivenServer server(std::move(config));
            server.start();
        } else if (mode == "threaded") {
            HttpServer server(std::move(config));
            server.start();
        } else {
            usage(argv[0]);
            return 2;
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
