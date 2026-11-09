#include "src/handlers/StaticFileHandler.hpp"

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace fs = std::filesystem;

namespace {

void writeFile(const fs::path& path, const std::string& content) {
    std::ofstream(path, std::ios::binary) << content;
}

// Layout:
//   <base>/www/index.html, style.css, a..b.txt, empty.txt, sub/inner.txt
//   <base>/www/link.txt -> <base>/www-secret/key.txt   (symlink escaping the root)
//   <base>/www-secret/key.txt                           (sibling sharing the "www" prefix)
class StaticFileHandlerTest : public testing::Test {
protected:
    void SetUp() override {
        base_ = fs::temp_directory_path() / ("sfh-test-" + std::to_string(std::random_device{}()));
        root_ = base_ / "www";
        fs::create_directories(root_ / "sub");
        fs::create_directories(base_ / "www-secret");

        writeFile(root_ / "index.html", "<h1>index</h1>");
        writeFile(root_ / "style.css", "body{}");
        writeFile(root_ / "a..b.txt", "dots");
        writeFile(root_ / "empty.txt", "");
        writeFile(root_ / "sub" / "inner.txt", "inner");
        writeFile(base_ / "www-secret" / "key.txt", "TOPSECRET");
        fs::create_symlink(base_ / "www-secret" / "key.txt", root_ / "link.txt");
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(base_, ec);
    }

    http::HttpResponse get(const std::string& path, const std::string& method = "GET") {
        handlers::StaticFileHandler handler(root_.string());
        http::HttpRequest req;
        req.method = method;
        req.path = path;
        req.version = "HTTP/1.1";
        return handler.handle(req);
    }

    static bool hasStatus(const http::HttpResponse& resp, int code) {
        return resp.toString().starts_with("HTTP/1.1 " + std::to_string(code) + " ");
    }

    static std::string body(const http::HttpResponse& resp) {
        const std::string wire = resp.toString();
        return wire.substr(wire.find("\r\n\r\n") + 4);
    }

    fs::path base_;
    fs::path root_;
};

} // namespace

TEST_F(StaticFileHandlerTest, ServesFileWithContentType) {
    auto resp = get("/style.css");
    EXPECT_TRUE(hasStatus(resp, 200));
    EXPECT_EQ(body(resp), "body{}");
    EXPECT_NE(resp.toString().find("Content-Type: text/css\r\n"), std::string::npos);
}

TEST_F(StaticFileHandlerTest, RootServesIndexHtml) {
    auto resp = get("/");
    EXPECT_TRUE(hasStatus(resp, 200));
    EXPECT_EQ(body(resp), "<h1>index</h1>");
}

TEST_F(StaticFileHandlerTest, QueryStringIsIgnored) {
    EXPECT_EQ(body(get("/style.css?v=123")), "body{}");
}

TEST_F(StaticFileHandlerTest, NestedFile) {
    EXPECT_EQ(body(get("/sub/inner.txt")), "inner");
}

TEST_F(StaticFileHandlerTest, EmptyFileIs200WithEmptyBody) {
    auto resp = get("/empty.txt");
    EXPECT_TRUE(hasStatus(resp, 200));
    EXPECT_EQ(body(resp), "");
}

TEST_F(StaticFileHandlerTest, DirectoriesAreNotServed) {
    EXPECT_TRUE(hasStatus(get("/sub"), 404));
    EXPECT_TRUE(hasStatus(get("/sub/"), 404));
}

TEST_F(StaticFileHandlerTest, MissingFileIs404) {
    EXPECT_TRUE(hasStatus(get("/nope.html"), 404));
}

TEST_F(StaticFileHandlerTest, UnreadableFileIs500Not404) {
    // The file exists, so "Not Found" would be wrong. Same path is taken when
    // the server is out of file descriptors and can't open it.
    writeFile(root_ / "private.txt", "secret");
    fs::permissions(root_ / "private.txt", fs::perms::none);
    if (std::ifstream(root_ / "private.txt")) {
        GTEST_SKIP() << "running as root: permissions don't block reads";
    }
    EXPECT_TRUE(hasStatus(get("/private.txt"), 500));
}

TEST_F(StaticFileHandlerTest, DotsInsideFileNamesAreAllowed) {
    EXPECT_EQ(body(get("/a..b.txt")), "dots");
}

TEST_F(StaticFileHandlerTest, DotDotThatStaysInsideRootIsAllowed) {
    EXPECT_EQ(body(get("/sub/../style.css")), "body{}");
}

TEST_F(StaticFileHandlerTest, NonGetIsRejected) {
    EXPECT_FALSE(hasStatus(get("/style.css", "POST"), 200));
}

TEST_F(StaticFileHandlerTest, TrailingSlashOnDocRootIsEquivalent) {
    handlers::StaticFileHandler handler(root_.string() + "/");
    http::HttpRequest req;
    req.method = "GET";
    req.path = "/style.css";
    EXPECT_TRUE(handler.handle(req).toString().ends_with("body{}"));
}

class PathEscape : public StaticFileHandlerTest,
                   public testing::WithParamInterface<const char*> {};

TEST_P(PathEscape, IsRejected) {
    auto resp = get(GetParam());
    EXPECT_TRUE(hasStatus(resp, 404)) << GetParam();
    EXPECT_EQ(resp.toString().find("TOPSECRET"), std::string::npos) << GetParam();
}

INSTANTIATE_TEST_SUITE_P(Paths, PathEscape, testing::Values(
    "-secret/key.txt",                // No leading slash: "<root>" + "-secret/..." = sibling dir
    "/../www-secret/key.txt",
    "/sub/../../www-secret/key.txt",
    "/../../../../../../etc/passwd",
    "//etc/passwd",
    "/link.txt",                      // Symlink pointing outside the root
    "*",
    "http://example.com/style.css",
    ""
));
