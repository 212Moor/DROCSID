#include <iostream>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <vector>
#include <ctime>
#include <limits>

using namespace std;

class DROCSIDClient {
public:
    DROCSIDClient(const string& host, int port) : running(true), last_activity(time(nullptr)) {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) throw runtime_error("Échec création socket");

        sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, host.c_str(), &serv_addr.sin_addr) <= 0)
            throw runtime_error("Adresse invalide");

        if (connect(sock, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
            throw runtime_error("Échec connexion");

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
                cerr << "Serveur déconnecté" << endl;
                break;
            }

            buffer[bytes] = '\0';
            remaining += buffer;

            size_t pos;
            while ((pos = remaining.find('\n')) != string::npos) {
                string line = remaining.substr(0, pos);
                remaining.erase(0, pos+1);
                
                if (!line.empty()) {
                    if (line == "srv: ALIVE") {
                        send(sock, "ALIVE\n", 6, 0); // Réponse keepalive
                    } else {
                        cout << ">> " << line << endl;
                        if (line == ".") cout << "--- Fin du message ---" << endl;
                    }
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

void clear_input() {
    cin.clear();
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
}

void show_main_menu() {
    cout << "\n=== MENU PRINCIPAL ===\n"
         << "1. Créer un groupe\n"
         << "2. Rejoindre un groupe\n"
         << "3. Envoyer message privé\n"
         << "4. Lister membres groupe\n"
         << "5. Quitter un groupe\n"
         << "6. Envoyer message groupe\n"
         << "0. Quitter\n"
         << "Votre choix : ";
}

void show_group_menu(const string& group_name) {
    cout << "\n=== MENU GROUPE [" << group_name << "] ===\n"
         << "1. Envoyer message\n"
         << "2. Lister membres\n"
         << "3. Quitter groupe\n"
         << "0. Retour menu principal\n"
         << "Votre choix : ";
}

string read_multiline_message() {
    string message, line;
    cout << "Entrez votre message (finir par '.' seul):\n";
    while (getline(cin, line) && line != ".") {
        message += line + "\n";
    }
    return message;
}

void handle_group_operations(DROCSIDClient& client, const string& group) {
    int choice;
    do {
        show_group_menu(group);
        cin >> choice;
        clear_input();
        
        switch(choice) {
            case 1:
                client.send_group_message(group, read_multiline_message());
                break;
            case 2:
                client.list_members(group);
                break;
            case 3:
                client.leave_group(group);
                return;
            case 0:
                return;
            default:
                cout << "Choix invalide!\n";
        }
    } while (true);
}

int main() {
    try {
        cout << "=== CLIENT DROCSID ===" << endl;
        DROCSIDClient client("127.0.0.1", 8888);

        // Authentification
        string pseudo;
        cout << "Entrez votre pseudo : ";
        getline(cin, pseudo);
        client.login(pseudo);

        // Menu principal
        int choice;
        string input;
        
        do {
            show_main_menu();
            cin >> choice;
            clear_input();
            
            switch(choice) {
                case 1: {
                    cout << "Nom du groupe : ";
                    getline(cin, input);
                    client.create_group(input);
                    break;
                }
                case 2: {
                    cout << "Nom du groupe : ";
                    getline(cin, input);
                    client.enter_group(input);
                    handle_group_operations(client, input);
                    break;
                }
                case 3: {
                    string dest, msg;
                    cout << "Destinataire : ";
                    getline(cin, dest);
                    msg = read_multiline_message();
                    client.send_private_message(dest, msg);
                    break;
                }
                case 4: {
                    cout << "Nom du groupe : ";
                    getline(cin, input);
                    client.list_members(input);
                    break;
                }
                case 5: {
                    cout << "Nom du groupe : ";
                    getline(cin, input);
                    client.leave_group(input);
                    break;
                }
                case 6: {
                    cout << "Nom du groupe : ";
                    getline(cin, input);
                    client.send_group_message(input, read_multiline_message());
                    break;
                }
                case 0:
                    cout << "Déconnexion..." << endl;
                    break;
                default:
                    cout << "Option invalide!" << endl;
            }
        } while (choice != 0);

    } catch (const exception& e) {
        cerr << "ERREUR : " << e.what() << endl;
        return 1;
    }
    return 0;
}