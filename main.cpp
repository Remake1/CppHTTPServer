#include "src/EventDrivenServer.hpp"
#include "src/HttpServer.hpp"

int main() {
    ServerConfig config;
    config.port = 3000;
    config.doc_root = "./www";
    //
    // HttpServer server(config);
    // server.start();

    EventDrivenServer server(std::move(config));
    server.start();

    return 0;
}
