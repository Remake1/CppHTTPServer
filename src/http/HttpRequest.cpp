#include "HttpRequest.hpp"
#include <sstream>
#include <algorithm>

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

} // namespace http
