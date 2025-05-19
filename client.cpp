#include <iostream>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <vector>

using namespace std;

class DROCSIDClient {
public:
    DROCSIDClient(const string& host, int port) : running(true) {
        // Configuration du socket
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) throw runtime_error("Socket creation failed");

        sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, host.c_str(), &serv_addr.sin_addr) <= 0)
            throw runtime_error("Invalid address");

        // Connexion
        if (connect(sock, (sockaddr*)&serv_addr, sizeof(serv_addr)))
            throw runtime_error("Connection failed");

        // Thread de réception
        receiver = thread([this]() {
            char buffer[4096];
            string remaining;
            
            while (running) {
                int len = recv(sock, buffer, sizeof(buffer)-1, 0);
                if (len <= 0) {
                    cerr << "Server disconnected" << endl;
                    break;
                }

                buffer[len] = '\0';
                remaining += buffer;

                // Traitement ligne par ligne
                size_t pos;
                while ((pos = remaining.find('\n')) != string::npos) {
                    string line = remaining.substr(0, pos);
                    remaining.erase(0, pos+1);
                    
                    if (!line.empty()) {
                        cout << "SERVER: " << line << endl;
                        if (line == ".") cout << "--- End of message ---" << endl;
                    }
                }
            }
        });
    }

    void send_command(const string& cmd) {
        string full_cmd = cmd + "\n";
        send(sock, full_cmd.c_str(), full_cmd.size(), 0);
    }

    ~DROCSIDClient() {
        running = false;
        close(sock);
        if (receiver.joinable()) receiver.join();
    }

private:
    int sock;
    atomic<bool> running;
    thread receiver;
};

void interactive_session(DROCSIDClient& client) {
    string cmd;
    while (true) {
        cout << "> ";
        getline(cin, cmd);
        
        if (cmd == "exit") break;
        
        if (cmd.rfind("SPEAK ", 0) == 0) {
            string group = cmd.substr(6);
            cout << "Enter message (end with '.'):\n";
            string msg, line;
            while (getline(cin, line) && line != ".") {
                msg += line + "\n";
            }
            client.send_command("SPEAK " + group + "\n" + msg + ".");
        } else {
            client.send_command(cmd);
        }
    }
}

int main() {
    try {
        cout << "Connecting to server..." << endl;
        DROCSIDClient client("127.0.0.1", 8887);
        
        string pseudo;
        cout << "Enter username: ";
        getline(cin, pseudo);
        client.send_command("LOGIN " + pseudo);
        
        interactive_session(client);
    } catch (const exception& e) {
        cerr << "ERROR: " << e.what() << endl;
        return 1;
    }
    return 0;
}