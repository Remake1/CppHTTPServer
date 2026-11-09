#ifndef CPPHTTPSERVER_STATICFILEHANDLER_HPP
#define CPPHTTPSERVER_STATICFILEHANDLER_HPP

#include "../http/HttpRequest.hpp"
#include "../http/HttpResponse.hpp"
#include <filesystem>
#include <string>

namespace handlers {

class StaticFileHandler {
public:
    explicit StaticFileHandler(std::string doc_root);

    http::HttpResponse handle(const http::HttpRequest& req);

private:
    std::string mapPath(const std::string& url_path);
    static bool readFile(const std::string& path, std::string& out);
    static std::string getContentType(const std::string& path);

    std::filesystem::path root_;  // Absolute, canonical document root
};

} // namespace handlers

#endif //CPPHTTPSERVER_STATICFILEHANDLER_HPP
