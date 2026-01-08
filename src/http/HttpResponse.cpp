#include "HttpResponse.hpp"
#include <sstream>

namespace http {

HttpResponse& HttpResponse::setStatus(int code, const std::string& reason) {
    status_code_ = code;
    status_reason_ = reason.empty() ? defaultReason(code) : reason;
    return *this;
}

HttpResponse& HttpResponse::setHeader(const std::string& key, const std::string& value) {
    headers_[key] = value;
    return *this;
}

HttpResponse& HttpResponse::setContentType(const std::string& content_type) {
    return setHeader("Content-Type", content_type);
}

HttpResponse& HttpResponse::setBody(const std::string& body) {
    body_ = body;
    return *this;
}

std::string HttpResponse::toString() const {
    std::ostringstream resp;

    resp << "HTTP/1.1 " << status_code_ << " " << status_reason_ << "\r\n";

    // Add Content-Length if body present and not already set
    if (!body_.empty() && headers_.find("Content-Length") == headers_.end()) {
        resp << "Content-Length: " << body_.size() << "\r\n";
    }

    for (const auto& [key, value] : headers_) {
        resp << key << ": " << value << "\r\n";
    }

    resp << "\r\n";
    resp << body_;

    return resp.str();
}

HttpResponse HttpResponse::ok(const std::string& body, const std::string& content_type) {
    return HttpResponse()
        .setStatus(200)
        .setContentType(content_type)
        .setHeader("Connection", "close")
        .setBody(body);
}

HttpResponse HttpResponse::notFound(const std::string& body) {
    return HttpResponse()
        .setStatus(404)
        .setContentType("text/plain")
        .setHeader("Connection", "close")
        .setBody(body);
}

std::string HttpResponse::defaultReason(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        default: return "Unknown";
    }
}

} // namespace http
