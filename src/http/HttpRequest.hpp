#ifndef CPPHTTPSERVER_HTTPREQUEST_HPP
#define CPPHTTPSERVER_HTTPREQUEST_HPP

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>

namespace http {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;  // Keys lowercased
    std::string body;
};

HttpRequest parseHttpRequest(const std::string& raw);

struct RequestLimits {
    size_t max_header_bytes = 8 * 1024;   // Request line + headers + blank line
    size_t max_body_bytes = 1024 * 1024;
};

enum class ParseStatus {
    Complete,         // A full request is at the start of the buffer
    Incomplete,       // Need more bytes
    BadRequest,       // 400: malformed request line or Content-Length
    HeadersTooLarge,  // 431
    BodyTooLarge,     // 413
    NotImplemented,   // 501: Transfer-Encoding (chunked request bodies)
};

struct ParseResult {
    ParseStatus status = ParseStatus::Incomplete;
    size_t consumed = 0;  // Bytes the request occupies in the buffer (when Complete)
    HttpRequest request;
};

// Extracts the first request from a buffer that may hold a partial request or
// several pipelined ones. The body is delimited by Content-Length, so bytes
// that belong to the next request are left alone.
ParseResult parseRequestFrame(std::string_view buffer, const RequestLimits& limits = {});

// Whether the client wants the connection kept open after this request:
// HTTP/1.1 defaults to keep-alive, HTTP/1.0 to close, and the Connection
// header (comma-separated, case-insensitive tokens) overrides either.
bool wantsKeepAlive(const HttpRequest& req);

} // namespace http

#endif //CPPHTTPSERVER_HTTPREQUEST_HPP
