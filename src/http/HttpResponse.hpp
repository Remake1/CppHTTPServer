#ifndef CPPHTTPSERVER_HTTPRESPONSE_HPP
#define CPPHTTPSERVER_HTTPRESPONSE_HPP

#include <string>
#include <unordered_map>

namespace http {

class HttpResponse {
public:
    HttpResponse& setStatus(int code, const std::string& reason = "");
    HttpResponse& setHeader(const std::string& key, const std::string& value);
    HttpResponse& setContentType(const std::string& content_type);
    HttpResponse& setBody(const std::string& body);

    [[nodiscard]] std::string toString() const;

    static HttpResponse ok(const std::string& body = "",
                           const std::string& content_type = "text/plain");
    static HttpResponse notFound(const std::string& body = "404 Not Found\n");

private:
    static std::string defaultReason(int code);

    int status_code_ = 200;
    std::string status_reason_ = "OK";
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
};

} // namespace http

#endif //CPPHTTPSERVER_HTTPRESPONSE_HPP
