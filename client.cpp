#include <iostream>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

class DROCSIDClient {
public:
    DROCSIDClient(const std::string& host, int port) {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr);

        if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Connexion échouée\n";
            exit(1);
        }

        char buffer[1024];
        recv(sock, buffer, sizeof(buffer), 0);  // Reçoit "TCHAT 1"
        std::cout << "Serveur: " << buffer;
    }

    void send_command(const std::string& cmd) {
        send(sock, (cmd + "\n").c_str(), cmd.size() + 1, 0);
        char buffer[1024];
        recv(sock, buffer, sizeof(buffer), 0);
        std::cout << "Réponse: " << buffer;
    }

private:
    int sock;
};

int main() {
    DROCSIDClient client("127.0.0.1", 8888);
    client.send_command("LOGIN Alice");
    client.send_command("CREAT General");
    return 0;
}