#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

constexpr int PORT = 3000;
constexpr int BUFFLEN = 4096;


int main() {
    std::cout << "Using port: " << PORT << std::endl;

    // Socket
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        perror("ERROR opening socket");
        exit(1);
    }

    // Bind
    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(PORT);
    serverAddress.sin_addr.s_addr = INADDR_ANY;

    // Ignore when port in TIME_WAIT
    int opt = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(1);
    }

    int bindVal = bind(serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
    if (bindVal < 0) {
        perror("ERROR binding socket");
        exit(1);
    }

    // Listen
    int listenVal = listen(serverSocket, 1);
    if (listenVal < 0) {
        perror("ERROR listening socket");
        exit(1);
    }

    // Accept
    sockaddr_in remoteAdress{};
    socklen_t remoteAdressLen = sizeof(remoteAdress);

    std::cout << "Waiting for new connection..." << std::endl;
    int clientSocket = accept(serverSocket, (struct sockaddr*)&remoteAdress, &remoteAdressLen);
    if (clientSocket < 0) {
        perror("ERROR accepting");
        exit(1);
    }

    std::string clientIp = inet_ntoa(remoteAdress.sin_addr);
    int remotePort = ntohs(remoteAdress.sin_port);

    std::cout << "New client at " << clientIp << ":" << remotePort << std::endl;

    // Receive

    // Read request
    std::string request;
    char buffer[BUFFLEN] = {0};

    while (request.find("\r\n\r\n") == std::string::npos) {
        ssize_t n = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        request.append(buffer, n);
    }

    // Parse
    std::istringstream stream(request);
    std::string method, path, version;

    stream >> method >> path >> version;

    std::cout << "Received: " << method << " " << path << " from " << clientIp << ":" << remotePort << std::endl;

    std::string body = "Hello world\n";

    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: text/plain\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    response << body;

    // Send HTTP response
    std::string resp = response.str();
    size_t total_sent = 0;

    while (total_sent < resp.size()) {
        ssize_t sent = send(clientSocket,
            resp.data() + total_sent,
            resp.size() - total_sent,
            0
        );
        if (sent <= 0) break;
        total_sent += sent;
    }

    // Close
    shutdown(clientSocket, SHUT_WR);
    close(clientSocket);
    close(serverSocket);


    return 0;
}