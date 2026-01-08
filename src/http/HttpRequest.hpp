#ifndef CPPHTTPSERVER_HTTPREQUEST_HPP
#define CPPHTTPSERVER_HTTPREQUEST_HPP

#include <string>
#include <unordered_map>

namespace http {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

HttpRequest parseHttpRequest(const std::string& raw);

} // namespace http

#endif //CPPHTTPSERVER_HTTPREQUEST_HPP
