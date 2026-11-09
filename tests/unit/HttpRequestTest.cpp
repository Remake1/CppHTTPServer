#include "src/http/HttpRequest.hpp"

#include <gtest/gtest.h>
#include <string>

using http::ParseStatus;
using http::parseRequestFrame;

namespace {

const std::string kGet = "GET /index.html HTTP/1.1\r\nHost: example.com\r\n\r\n";

} // namespace

// ---- parseRequestFrame: framing ----

TEST(ParseRequestFrame, ParsesCompleteRequest) {
    auto r = parseRequestFrame(kGet);
    ASSERT_EQ(r.status, ParseStatus::Complete);
    EXPECT_EQ(r.consumed, kGet.size());
    EXPECT_EQ(r.request.method, "GET");
    EXPECT_EQ(r.request.path, "/index.html");
    EXPECT_EQ(r.request.version, "HTTP/1.1");
    EXPECT_EQ(r.request.headers.at("host"), "example.com");
}

TEST(ParseRequestFrame, LowercasesHeaderNamesAndTrimsValues) {
    auto r = parseRequestFrame("GET / HTTP/1.1\r\nX-Custom-Header:   Value  \r\n\r\n");
    ASSERT_EQ(r.status, ParseStatus::Complete);
    EXPECT_EQ(r.request.headers.at("x-custom-header"), "Value");
}

TEST(ParseRequestFrame, IncompleteUntilBlankLine) {
    EXPECT_EQ(parseRequestFrame("").status, ParseStatus::Incomplete);
    EXPECT_EQ(parseRequestFrame("GET / HTTP/1.1\r\nHost: x\r\n").status, ParseStatus::Incomplete);
    EXPECT_EQ(parseRequestFrame("GET / HTTP/1.1\r\nHost: x\r\n\r").status, ParseStatus::Incomplete);
}

TEST(ParseRequestFrame, WaitsForFullBody) {
    const std::string head = "POST /x HTTP/1.1\r\nContent-Length: 5\r\n\r\n";
    EXPECT_EQ(parseRequestFrame(head).status, ParseStatus::Incomplete);
    EXPECT_EQ(parseRequestFrame(head + "hel").status, ParseStatus::Incomplete);

    auto r = parseRequestFrame(head + "hello");
    ASSERT_EQ(r.status, ParseStatus::Complete);
    EXPECT_EQ(r.request.body, "hello");
    EXPECT_EQ(r.consumed, head.size() + 5);
}

TEST(ParseRequestFrame, ConsumesOnlyFirstOfPipelinedRequests) {
    const std::string second = "GET /second HTTP/1.1\r\n\r\n";
    auto r = parseRequestFrame(kGet + second);
    ASSERT_EQ(r.status, ParseStatus::Complete);
    EXPECT_EQ(r.consumed, kGet.size());
    EXPECT_EQ(r.request.path, "/index.html");

    auto r2 = parseRequestFrame(std::string_view(kGet + second).substr(r.consumed));
    ASSERT_EQ(r2.status, ParseStatus::Complete);
    EXPECT_EQ(r2.request.path, "/second");
}

TEST(ParseRequestFrame, BodyBytesAreNotParsedAsNextRequest) {
    // The body looks like a request line; it must be treated as body.
    const std::string req = "POST /x HTTP/1.1\r\nContent-Length: 18\r\n\r\nGET /evil HTTP/1.1";
    auto r = parseRequestFrame(req);
    ASSERT_EQ(r.status, ParseStatus::Complete);
    EXPECT_EQ(r.request.body, "GET /evil HTTP/1.1");
    EXPECT_EQ(r.consumed, req.size());
}

// ---- parseRequestFrame: limits and errors ----

TEST(ParseRequestFrame, HeadersTooLargeWithoutTerminator) {
    http::RequestLimits limits{.max_header_bytes = 64, .max_body_bytes = 1024};
    const std::string big = "GET / HTTP/1.1\r\nX: " + std::string(100, 'a');
    EXPECT_EQ(parseRequestFrame(big, limits).status, ParseStatus::HeadersTooLarge);
}

TEST(ParseRequestFrame, HeadersTooLargeWithTerminator) {
    http::RequestLimits limits{.max_header_bytes = 64, .max_body_bytes = 1024};
    const std::string big = "GET / HTTP/1.1\r\nX: " + std::string(100, 'a') + "\r\n\r\n";
    EXPECT_EQ(parseRequestFrame(big, limits).status, ParseStatus::HeadersTooLarge);
}

TEST(ParseRequestFrame, HeadersExactlyAtLimitAreAccepted) {
    http::RequestLimits limits{.max_header_bytes = kGet.size(), .max_body_bytes = 0};
    EXPECT_EQ(parseRequestFrame(kGet, limits).status, ParseStatus::Complete);
}

TEST(ParseRequestFrame, BodyTooLargeIsRejectedBeforeItArrives) {
    http::RequestLimits limits{.max_header_bytes = 1024, .max_body_bytes = 10};
    auto r = parseRequestFrame("POST / HTTP/1.1\r\nContent-Length: 11\r\n\r\n", limits);
    EXPECT_EQ(r.status, ParseStatus::BodyTooLarge);
}

TEST(ParseRequestFrame, TransferEncodingIsNotImplemented) {
    auto r = parseRequestFrame("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n");
    EXPECT_EQ(r.status, ParseStatus::NotImplemented);
}

class InvalidContentLength : public testing::TestWithParam<const char*> {};

TEST_P(InvalidContentLength, IsBadRequest) {
    const std::string req = std::string("POST / HTTP/1.1\r\nContent-Length: ") + GetParam() + "\r\n\r\n";
    EXPECT_EQ(parseRequestFrame(req).status, ParseStatus::BadRequest);
}

INSTANTIATE_TEST_SUITE_P(Values, InvalidContentLength,
                         testing::Values("abc", "-1", "12abc", "", "1 2", "99999999999999999999999"));

class MalformedRequestLine : public testing::TestWithParam<const char*> {};

TEST_P(MalformedRequestLine, IsBadRequest) {
    EXPECT_EQ(parseRequestFrame(GetParam()).status, ParseStatus::BadRequest);
}

INSTANTIATE_TEST_SUITE_P(Values, MalformedRequestLine,
                         testing::Values("HELLO\r\n\r\n",
                                         "GET /\r\n\r\n",
                                         "GET / HTTP/2.0\r\n\r\n",
                                         "GET / FOO/1.1\r\n\r\n",
                                         "\r\n\r\n"));

// ---- wantsKeepAlive ----

struct KeepAliveCase {
    const char* version;
    const char* connection;  // nullptr = header absent
    bool expected;
};

// Used by GoogleTest in failure messages, and by CMake's gtest_discover_tests
// for the CTest test names (otherwise they're a raw byte dump of the struct).
void PrintTo(const KeepAliveCase& c, std::ostream* os) {
    *os << c.version << " Connection=" << (c.connection ? c.connection : "(none)");
}

class WantsKeepAlive : public testing::TestWithParam<KeepAliveCase> {};

TEST_P(WantsKeepAlive, FollowsVersionDefaultAndConnectionHeader) {
    const auto& c = GetParam();
    http::HttpRequest req;
    req.method = "GET";
    req.path = "/";
    req.version = c.version;
    if (c.connection) {
        req.headers["connection"] = c.connection;
    }
    EXPECT_EQ(http::wantsKeepAlive(req), c.expected)
        << c.version << ", Connection: " << (c.connection ? c.connection : "(absent)");
}

INSTANTIATE_TEST_SUITE_P(Cases, WantsKeepAlive, testing::Values(
    KeepAliveCase{"HTTP/1.1", nullptr, true},
    KeepAliveCase{"HTTP/1.1", "close", false},
    KeepAliveCase{"HTTP/1.1", "Close", false},
    KeepAliveCase{"HTTP/1.1", "TE, close", false},
    KeepAliveCase{"HTTP/1.1", "keep-alive", true},
    KeepAliveCase{"HTTP/1.1", "upgrade", true},
    KeepAliveCase{"HTTP/1.0", nullptr, false},
    KeepAliveCase{"HTTP/1.0", "keep-alive", true},
    KeepAliveCase{"HTTP/1.0", "Keep-Alive", true},
    KeepAliveCase{"HTTP/1.0", " keep-alive , foo", true},
    KeepAliveCase{"HTTP/1.0", "keep-alive, close", false},
    // Substring matches must not count: these are different tokens.
    KeepAliveCase{"HTTP/1.1", "not-close", true},
    KeepAliveCase{"HTTP/1.0", "x-keep-alive", false}
));
