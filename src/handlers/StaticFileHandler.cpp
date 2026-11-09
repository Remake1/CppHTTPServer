#include "StaticFileHandler.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace handlers {

namespace fs = std::filesystem;

StaticFileHandler::StaticFileHandler(std::string doc_root) {
    std::error_code ec;
    root_ = fs::weakly_canonical(fs::absolute(doc_root), ec);
    if (ec) {
        root_ = fs::absolute(doc_root).lexically_normal();
    }
    // Drop a trailing separator ("/srv/www/" -> "/srv/www") so the
    // component-wise containment check in mapPath() compares like with like.
    if (!root_.has_filename()) {
        root_ = root_.parent_path();
    }
}

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

// Maps a URL path to a file under the document root.
// Returns "" if the path is malformed or resolves outside the root.
std::string StaticFileHandler::mapPath(const std::string& url_path) {
    std::string clean = url_path;

    // Strip query string
    auto q = clean.find('?');
    if (q != std::string::npos) {
        clean = clean.substr(0, q);
    }

    // Only origin-form targets ("/path") are valid here. Without this check,
    // "GET -secret/x" would be concatenated into "<root>-secret/x".
    if (clean.empty() || clean.front() != '/') {
        return "";
    }

    // Default to index.html for root
    if (clean == "/") {
        clean = "/index.html";
    }

    // Resolve ".", "..", and symlinks, then verify the result is still inside
    // root_. Compare whole path components rather than string prefixes:
    // "/srv/www-secret" starts with the string "/srv/www" but is not inside it.
    std::error_code ec;
    fs::path resolved = fs::weakly_canonical(root_ / fs::path(clean).relative_path(), ec);
    if (ec) {
        return "";
    }

    auto [root_it, _] = std::mismatch(root_.begin(), root_.end(),
                                      resolved.begin(), resolved.end());
    if (root_it != root_.end()) {
        return "";  // Escapes the document root
    }

    return resolved.string();
}

bool StaticFileHandler::readFile(const std::string& path, std::string& out) {
    // Only serve regular files. On macOS an ifstream can open a directory
    // and then read 0 bytes, which would be served as an empty 200.
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        return false;
    }

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
