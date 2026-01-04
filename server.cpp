#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

constexpr int PORT = 3000;
constexpr int BUFFLEN = 4096;

constexpr const char* DOC_ROOT = "../www";

struct HttpRequest {
    std::string method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
};

int create_server_socket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        exit(1);
    }

    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(1);
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(fd, 1) < 0) {
        perror("listen");
        exit(1);
    }

    return fd;
}

// HTTP Request parser
static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end   = s.find_last_not_of(" \t\r\n");

    if (start == std::string::npos)
        return "";

    return s.substr(start, end - start + 1);
}

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

HttpRequest parse_http_request(const std::string& raw) {
    HttpRequest req;
    std::istringstream stream(raw);
    std::string line;

    // Parse request line
    if (!std::getline(stream, line))
        return req;

    // remove trailing '\r'
    if (!line.empty() && line.back() == '\r')
        line.pop_back();

    std::istringstream request_line(line);
    request_line >> req.method >> req.path >> req.version;

    // Parse headers
    while (std::getline(stream, line)) {
        if (line == "\r" || line.empty())
            break; // end of headers

        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        auto colon = line.find(':');
        if (colon == std::string::npos)
            continue; // malformed header, ignore

        std::string key   = to_lower(trim(line.substr(0, colon)));
        std::string value = trim(line.substr(colon + 1));

        req.headers[key] = value;
    }

    return req;
}

int accept_client(int server_fd, std::string& ip, int& port) {
    sockaddr_in client_addr{};
    socklen_t len = sizeof(client_addr);

    int client_fd = accept(server_fd, (sockaddr*)&client_addr, &len);
    if (client_fd < 0) {
        perror("accept");
        exit(1);
    }

    ip = inet_ntoa(client_addr.sin_addr);
    port = ntohs(client_addr.sin_port);

    return client_fd;
}

std::string read_http_request(int client_fd) {
    std::string request;
    char buffer[BUFFLEN];

    while (request.find("\r\n\r\n") == std::string::npos) {
        ssize_t n = recv(client_fd, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        request.append(buffer, n);
    }

    return request;
}

void send_all(int client_fd, const std::string& response) {
    size_t sent_total = 0;

    while (sent_total < response.size()) {
        ssize_t sent = send(
            client_fd,
            response.data() + sent_total,
            response.size() - sent_total,
            0
        );
        if (sent <= 0) break;
        sent_total += sent;
    }
}

// ------------------
// Serve Static files
// ------------------

std::string map_path(const std::string& url_path) {
    if (url_path.find("..") != std::string::npos)
        return "";

    std::string clean = url_path;
    auto q = clean.find('?');
    if (q != std::string::npos)
        clean = clean.substr(0, q);

    if (clean == "/")
        return std::string(DOC_ROOT) + "/index.html";

    return std::string(DOC_ROOT) + clean;
}

bool read_file(const std::string& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;

    std::ostringstream ss;
    ss << file.rdbuf();
    out = ss.str();

    return true;
}

std::string get_content_type(const std::string& path) {
    if (path.ends_with(".html")) return "text/html";
    if (path.ends_with(".css"))  return "text/css";
    if (path.ends_with(".js"))   return "application/javascript";
    if (path.ends_with(".png"))  return "image/png";
    if (path.ends_with(".jpg") || path.ends_with(".jpeg"))
        return "image/jpeg";

    return "application/octet-stream";
}

std::string build_response(
    int status,
    const std::string& content_type,
    const std::string& body
) {
    std::ostringstream resp;

    if (status == 200)
        resp << "HTTP/1.1 200 OK\r\n";
    else
        resp << "HTTP/1.1 404 Not Found\r\n";

    resp << "Content-Type: " << content_type << "\r\n";
    resp << "Content-Length: " << body.size() << "\r\n";
    resp << "Connection: close\r\n";
    resp << "\r\n";
    resp << body;

    return resp.str();
}


int main() {
    std::cout << "Using port: " << PORT << std::endl;

    int server_fd = create_server_socket(PORT);

    std::cout << "Waiting for new connection...\n";

    while (true) {
        std::string client_ip;
        int client_port;
        int client_fd = accept_client(server_fd, client_ip, client_port);

        std::cout << "New client at " << client_ip << ":" << client_port << std::endl;

        const std::string raw = read_http_request(client_fd);

        // Parse request line
        HttpRequest req = parse_http_request(raw);

        std::string fs_path = map_path(req.path);
        std::string body;

        std::string response;

        if (!fs_path.empty() && read_file(fs_path, body)) {
            std::string type = get_content_type(fs_path);
            response = build_response(200, type, body);
        } else {
            body = "404 Not Found\n";
            response = build_response(404, "text/plain", body);
        }

        send_all(client_fd, response);

        // Close
        shutdown(client_fd, SHUT_WR);
        close(client_fd);
    }

    close(server_fd);


    return 0;
}