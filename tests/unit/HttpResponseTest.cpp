#include "src/http/HttpResponse.hpp"

#include <gtest/gtest.h>
#include <string>

using http::HttpResponse;

namespace {

size_t countOccurrences(const std::string& haystack, const std::string& needle) {
    size_t count = 0;
    for (size_t pos = haystack.find(needle); pos != std::string::npos;
         pos = haystack.find(needle, pos + 1)) {
        ++count;
    }
    return count;
}

std::string headPart(const std::string& wire) {
    return wire.substr(0, wire.find("\r\n\r\n") + 2);
}

} // namespace

TEST(HttpResponse, SerializesStatusLineHeadersAndBody) {
    HttpResponse resp;
    resp.setStatus(200).setContentType("text/plain").setBody("hello");
    const std::string wire = resp.toString();

    EXPECT_TRUE(wire.starts_with("HTTP/1.1 200 OK\r\n"));
    EXPECT_NE(wire.find("Content-Type: text/plain\r\n"), std::string::npos);
    EXPECT_NE(wire.find("Content-Length: 5\r\n"), std::string::npos);
    EXPECT_TRUE(wire.ends_with("\r\n\r\nhello"));
}

TEST(HttpResponse, EmptyBodyStillSendsContentLengthZero) {
    // Without it a keep-alive client can't tell where the response ends.
    HttpResponse resp;
    resp.setStatus(200);
    EXPECT_NE(resp.toString().find("Content-Length: 0\r\n"), std::string::npos);
}

TEST(HttpResponse, NoContentLengthOn204) {
    HttpResponse resp;
    resp.setStatus(204);
    EXPECT_EQ(resp.toString().find("Content-Length"), std::string::npos);
}

TEST(HttpResponse, ExplicitContentLengthIsNotDuplicated) {
    HttpResponse resp;
    resp.setStatus(200).setHeader("Content-Length", "5").setBody("hello");
    EXPECT_EQ(countOccurrences(resp.toString(), "Content-Length"), 1u);
}

TEST(HttpResponse, BinaryBodyIsPreservedExactly) {
    const std::string body("a\0b\r\n\r\nc", 8);
    HttpResponse resp;
    resp.setBody(body);
    const std::string wire = resp.toString();
    EXPECT_TRUE(wire.ends_with(body));
    EXPECT_NE(wire.find("Content-Length: 8\r\n"), std::string::npos);
}

TEST(HttpResponse, DefaultReasonPhrases) {
    const std::pair<int, const char*> cases[] = {
        {400, "Bad Request"}, {404, "Not Found"}, {408, "Request Timeout"},
        {413, "Content Too Large"}, {431, "Request Header Fields Too Large"},
        {501, "Not Implemented"},
    };
    for (const auto& [code, reason] : cases) {
        HttpResponse resp;
        resp.setStatus(code);
        EXPECT_TRUE(resp.toString().starts_with("HTTP/1.1 " + std::to_string(code) + " " + reason + "\r\n"))
            << code;
    }
}

TEST(HttpResponse, FactoriesLeaveConnectionHeaderToTheServer) {
    EXPECT_EQ(headPart(HttpResponse::ok("x").toString()).find("Connection"), std::string::npos);
    EXPECT_EQ(headPart(HttpResponse::notFound().toString()).find("Connection"), std::string::npos);
}
