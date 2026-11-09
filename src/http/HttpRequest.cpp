#include "HttpRequest.hpp"
#include <sstream>
#include <algorithm>
#include <charconv>

namespace http {

namespace detail {

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

} // namespace detail

HttpRequest parseHttpRequest(const std::string& raw) {
    HttpRequest req;
    std::istringstream stream(raw);
    std::string line;

    // Parse request line
    if (!std::getline(stream, line)) return req;

    // Remove trailing \r
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }

    std::istringstream request_line(line);
    request_line >> req.method >> req.path >> req.version;

    // Parse headers
    while (std::getline(stream, line)) {
        if (line == "\r" || line.empty()) break;

        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = detail::toLower(detail::trim(line.substr(0, colon)));
        std::string value = detail::trim(line.substr(colon + 1));
        req.headers[key] = value;
    }

    // Read remaining body (if any)
    std::ostringstream body_stream;
    body_stream << stream.rdbuf();
    req.body = body_stream.str();

    return req;
}

ParseResult parseRequestFrame(std::string_view buffer, const RequestLimits& limits) {
    ParseResult result;

    const size_t header_end = buffer.find("\r\n\r\n");
    if (header_end == std::string_view::npos) {
        if (buffer.size() > limits.max_header_bytes) {
            result.status = ParseStatus::HeadersTooLarge;
        }
        return result;  // Incomplete
    }

    const size_t header_len = header_end + 4;
    if (header_len > limits.max_header_bytes) {
        result.status = ParseStatus::HeadersTooLarge;
        return result;
    }

    HttpRequest req = parseHttpRequest(std::string(buffer.substr(0, header_len)));
    if (req.method.empty() || req.path.empty() || !req.version.starts_with("HTTP/1.")) {
        result.status = ParseStatus::BadRequest;
        return result;
    }

    // Chunked request bodies aren't supported. Rejecting them is required:
    // ignoring the header would misread the chunks as the next request.
    if (req.headers.contains("transfer-encoding")) {
        result.status = ParseStatus::NotImplemented;
        return result;
    }

    size_t body_len = 0;
    if (auto it = req.headers.find("content-length"); it != req.headers.end()) {
        const std::string& v = it->second;
        auto [end, ec] = std::from_chars(v.data(), v.data() + v.size(), body_len);
        if (ec != std::errc{} || end != v.data() + v.size()) {
            result.status = ParseStatus::BadRequest;
            return result;
        }
        if (body_len > limits.max_body_bytes) {
            result.status = ParseStatus::BodyTooLarge;
            return result;
        }
    }

    if (buffer.size() - header_len < body_len) {
        return result;  // Incomplete: body still arriving
    }

    req.body = std::string(buffer.substr(header_len, body_len));
    result.status = ParseStatus::Complete;
    result.consumed = header_len + body_len;
    result.request = std::move(req);
    return result;
}

bool wantsKeepAlive(const HttpRequest& req) {
    bool keep_alive = req.version != "HTTP/1.0";

    auto it = req.headers.find("connection");
    if (it == req.headers.end()) {
        return keep_alive;
    }

    const std::string value = detail::toLower(it->second);
    size_t pos = 0;
    while (true) {
        const size_t comma = value.find(',', pos);
        const std::string token = detail::trim(value.substr(pos, comma - pos));
        if (token == "close") return false;
        if (token == "keep-alive") keep_alive = true;
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return keep_alive;
}

} // namespace http
