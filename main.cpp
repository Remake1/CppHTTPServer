#include "src/HttpServer.hpp"

int main() {
    ServerConfig config;
    config.port = 3000;
    config.doc_root = "./www";

    HttpServer server(config);
    server.start();

    return 0;
}
