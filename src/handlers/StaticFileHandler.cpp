#include "StaticFileHandler.hpp"
#include <fstream>
#include <sstream>
#include <utility>

namespace handlers {

StaticFileHandler::StaticFileHandler(std::string doc_root)
    : doc_root_(std::move(doc_root)) {}

http::HttpResponse StaticFileHandler::handle(const http::HttpRequest& req) {
    if (req.method != "GET") {
        return http::HttpResponse::notFound("Method not allowed\n");
    }

    std::string fs_path = mapPath(req.path);
    if (fs_path.empty()) {
        return http::HttpResponse::notFound();
    }

    std::string body;
    if (!readFile(fs_path, body)) {
        return http::HttpResponse::notFound();
    }

    std::string content_type = getContentType(fs_path);
    return http::HttpResponse::ok(body, content_type);
}

std::string StaticFileHandler::mapPath(const std::string& url_path) {
    // Security: reject paths with ..
    if (url_path.find("..") != std::string::npos) {
        return "";
    }

    std::string clean = url_path;

    // Strip query string
    auto q = clean.find('?');
    if (q != std::string::npos) {
        clean = clean.substr(0, q);
    }

    // Default to index.html for root
    if (clean == "/") {
        return doc_root_ + "/index.html";
    }

    return doc_root_ + clean;
}

bool StaticFileHandler::readFile(const std::string& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    std::ostringstream ss;
    ss << file.rdbuf();
    out = ss.str();
    return true;
}

std::string StaticFileHandler::getContentType(const std::string& path) {
    if (path.ends_with(".html")) return "text/html";
    if (path.ends_with(".css"))  return "text/css";
    if (path.ends_with(".js"))   return "application/javascript";
    if (path.ends_with(".json")) return "application/json";
    if (path.ends_with(".png"))  return "image/png";
    if (path.ends_with(".jpg") || path.ends_with(".jpeg")) return "image/jpeg";
    if (path.ends_with(".gif"))  return "image/gif";
    if (path.ends_with(".svg"))  return "image/svg+xml";
    if (path.ends_with(".ico"))  return "image/x-icon";
    return "application/octet-stream";
}

} // namespace handlers
