#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

constexpr int PORT = 8080;
constexpr int BUFFLEN = 1024;


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
    socklen_t remoteAdressLen = sizeof(serverAddress);

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
    char buffer[BUFFLEN] = {0};

    while (true) {
        memset(buffer, 0, BUFFLEN);

        const ssize_t recvCount = recv(clientSocket, buffer, BUFFLEN-1, 0);
        if (recvCount < 0) {
            perror("ERROR receiving");
            exit(1);
        }
        if (recvCount == 0) {
            std::cout << "Client " << clientIp << ":" << remotePort << " has disconnected" << std::endl;
            break;
        }

        if (buffer[recvCount-1] == '\n')
            buffer[recvCount-1] = 0;

        std::cout << "Message from client: " << buffer << std::endl;

        // Response
        std::string response = "Your message was \"" + std::string(buffer) + "\"\n";
        const ssize_t sendCount = send(clientSocket, response.c_str(), response.length(), 0);
        if (sendCount < 0) {
            perror("ERROR sending response");
            exit(1);
        }
    }

    shutdown(clientSocket, SHUT_RDWR);
    close(serverSocket);

    return 0;
}