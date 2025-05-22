#include <iostream>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <vector>
#include <ctime>

using namespace std;

class DROCSIDClient {
public:
    DROCSIDClient(const string& host, int port) : running(true), last_activity(time(nullptr)) {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) throw runtime_error("Socket creation failed");

        sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, host.c_str(), &serv_addr.sin_addr) <= 0)
            throw runtime_error("Invalid address");

        if (connect(sock, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
            throw runtime_error("Connection failed");

        receiver_thread = thread(&DROCSIDClient::receive_messages, this);
        keepalive_thread = thread(&DROCSIDClient::send_keepalive, this);
    }

    void login(const string& pseudo) {
        send_command("LOGIN " + pseudo);
    }

    void create_group(const string& group) {
        send_command("CREAT " + group);
    }

    void enter_group(const string& group) {
        send_command("ENTER " + group);
    }

    void leave_group(const string& group) {
        send_command("LEAVE " + group);
    }

    void list_members(const string& group) {
        send_command("LSMEM " + group);
    }

    void send_group_message(const string& group, const string& message) {
        string full_cmd = "SPEAK " + group + "\n" + message + "\n.\n";
        send_message(full_cmd);
    }

    void send_private_message(const string& pseudo, const string& message) {
        string full_cmd = "MSGPV " + pseudo + "\n" + message + "\n.\n";
        send_message(full_cmd);
    }

    ~DROCSIDClient() {
        running = false;
        close(sock);
        if (receiver_thread.joinable()) receiver_thread.join();
        if (keepalive_thread.joinable()) keepalive_thread.join();
    }

private:
    int sock;
    atomic<bool> running;
    thread receiver_thread;
    thread keepalive_thread;
    time_t last_activity;

    void send_command(const string& cmd) {
        string full_cmd = cmd + "\n";
        send_message(full_cmd);
    }

    void send_message(const string& msg) {
        last_activity = time(nullptr);
        send(sock, msg.c_str(), msg.size(), 0);
    }

    void receive_messages() {
        char buffer[4096];
        string remaining;

        while (running) {
            int bytes = recv(sock, buffer, sizeof(buffer)-1, 0);
            if (bytes <= 0) {
                cerr << "Server disconnected" << endl;
                break;
            }

            buffer[bytes] = '\0';
            remaining += buffer;

            size_t pos;
            while ((pos = remaining.find('\n')) != string::npos) {
                string line = remaining.substr(0, pos);
                remaining.erase(0, pos+1);
                
                if (!line.empty()) {
                    cout << ">> " << line << endl;
                    if (line == ".") cout << "--- End of message ---" << endl;
                }
            }
        }
    }

    void send_keepalive() {
        while (running) {
            this_thread::sleep_for(chrono::seconds(10));
            if (time(nullptr) - last_activity > 10) {
                send(sock, "ALIVE\n", 6, 0);
            }
        }
    }
};

void show_help() {
    cout << "Commandes disponibles:\n"
         << "  login <pseudo>\n"
         << "  create <groupe>\n"
         << "  enter <groupe>\n"
         << "  leave <groupe>\n"
         << "  speak <groupe> <message>\n"
         << "  private <pseudo> <message>\n"
         << "  members <groupe>\n"
         << "  exit\n";
}

void process_command(DROCSIDClient& client, const string& cmd) {
    if (cmd.empty()) return;

    size_t space_pos = cmd.find(' ');
    string action = (space_pos == string::npos) ? cmd : cmd.substr(0, space_pos);

    if (action == "login" && space_pos != string::npos) {
        client.login(cmd.substr(space_pos + 1));
    } else if (action == "create" && space_pos != string::npos) {
        client.create_group(cmd.substr(space_pos + 1));
    } else if (action == "enter" && space_pos != string::npos) {
        client.enter_group(cmd.substr(space_pos + 1));
    } else if (action == "leave" && space_pos != string::npos) {
        client.leave_group(cmd.substr(space_pos + 1));
    } else if (action == "members" && space_pos != string::npos) {
        client.list_members(cmd.substr(space_pos + 1));
    } else if (action == "speak" && space_pos != string::npos) {
        size_t group_end = cmd.find(' ', space_pos + 1);
        if (group_end != string::npos) {
            string group = cmd.substr(space_pos + 1, group_end - space_pos - 1);
            string message = cmd.substr(group_end + 1);
            client.send_group_message(group, message);
        }
    } else if (action == "private" && space_pos != string::npos) {
        size_t pseudo_end = cmd.find(' ', space_pos + 1);
        if (pseudo_end != string::npos) {
            string pseudo = cmd.substr(space_pos + 1, pseudo_end - space_pos - 1);
            string message = cmd.substr(pseudo_end + 1);
            client.send_private_message(pseudo, message);
        }
    } else if (action == "help") {
        show_help();
    } else if (action == "exit") {
        return;
    } else {
        cout << "Commande invalide. Tapez 'help' pour l'aide." << endl;
    }
}

int main() {
    try {
        cout << "Connexion au serveur..." << endl;
        DROCSIDClient client("127.0.0.1", 8888);

        show_help();
        string cmd;
        while (true) {
            cout << "> ";
            getline(cin, cmd);
            if (cmd == "exit") break;
            process_command(client, cmd);
        }
    } catch (const exception& e) {
        cerr << "Erreur: " << e.what() << endl;
        return 1;
    }
    return 0;
}