#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <pthread.h>
#include <ifaddrs.h>
#include <net/if.h>

#define MAX_CLIENTS 30
#define BUFFER_SIZE 1024

typedef enum {
    STATE_HELP,
    STATE_MYIP,
    STATE_MYPORT,
    STATE_CONNECT,
    STATE_LIST,
    STATE_TERMINATE,
    STATE_SEND,
    STATE_EXIT,
    STATE_INVALID
} State;

typedef struct {
    int socket_fd;
    struct sockaddr_in address;
    int connection_id;
    int is_incoming; // 0 for outgoing, 1 for incoming
} Connection;

Connection connections[MAX_CLIENTS];
int connection_count = 0;

pthread_mutex_t connection_mutex = PTHREAD_MUTEX_INITIALIZER;

int MY_PORT;
fd_set set_socket;
int server_fd, max_fd;

void initialize_server();
State getState(const char *command);
void processHelp();
void processMyIP();
void processMyPort();
void processConnect(const char *args);
void processList();
void processTerminate(const char *args);
void processSend(const char *args);
void processExit();
void handle_new_connection();
void handle_client_message(int client_socket);
void* command_listener(void* arg);
int is_duplicate_connection(const char* ip, int port, int is_incoming);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    MY_PORT = atoi(argv[1]);
    if (MY_PORT <= 0) {
        fprintf(stderr, "Invalid port number: %s\n", argv[1]);
        return 1;
    }

    initialize_server();

    pthread_t command_thread;
    pthread_create(&command_thread, NULL, command_listener, NULL);
    pthread_detach(command_thread);

    char in_buffer[BUFFER_SIZE];
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    while (1) {
        FD_ZERO(&set_socket);
        FD_SET(server_fd, &set_socket);
        max_fd = server_fd;

        pthread_mutex_lock(&connection_mutex);
        for (int i = 0; i < connection_count; i++) {
            int client_socket = connections[i].socket_fd;
            if (client_socket > 0) {
                FD_SET(client_socket, &set_socket);
                if (client_socket > max_fd) {
                    max_fd = client_socket;
                }
            }
        }
        pthread_mutex_unlock(&connection_mutex);

        int activity = select(max_fd + 1, &set_socket, NULL, NULL, NULL);
        if (activity < 0 && errno != EINTR) {
            perror("select error");
            continue;
        }

        if (FD_ISSET(server_fd, &set_socket)) {
            handle_new_connection();
        }

        pthread_mutex_lock(&connection_mutex);
        for (int i = 0; i < connection_count; i++) {
            int client_socket = connections[i].socket_fd;
            if (FD_ISSET(client_socket, &set_socket)) {
                handle_client_message(client_socket);
            }
        }
        pthread_mutex_unlock(&connection_mutex);
    }

    return 0;
}

void initialize_server() {
    struct sockaddr_in address;
    int opt = 1;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(MY_PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", MY_PORT);
}

State getState(const char *command) {
    if (strcmp(command, "help") == 0) {
        return STATE_HELP;
    } else if (strcmp(command, "myip") == 0) {
        return STATE_MYIP;
    } else if (strcmp(command, "myport") == 0) {
        return STATE_MYPORT;
    } else if (strcmp(command, "connect") == 0) {
        return STATE_CONNECT;
    } else if (strcmp(command, "list") == 0) {
        return STATE_LIST;
    } else if (strcmp(command, "terminate") == 0) {
        return STATE_TERMINATE;
    } else if (strcmp(command, "send") == 0) {
        return STATE_SEND;
    } else if (strcmp(command, "exit") == 0) {
        return STATE_EXIT;
    } else {
        return STATE_INVALID;
    }
}

void processHelp() {
    printf("help\n");
    printf("myip\n");
    printf("myport\n");
    printf("connect <destination> <port no>\n");
    printf("list\n");
    printf("terminate <connection id.>\n");
    printf("send <connection id.> <message>\n");
    printf("exit\n");
}

void processMyIP() {
    struct ifaddrs *ifaddr, *ifa;
    int family, s;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        family = ifa->ifa_addr->sa_family;

        if (family == AF_INET && !(ifa->ifa_flags & IFF_LOOPBACK)) {
            s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                            host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
            if (s != 0) {
                printf("getnameinfo() failed: %s\n", gai_strerror(s));
                return;
            }

            printf("My IP: %s\n", host);
            break;
        }
    }

    freeifaddrs(ifaddr);
}

void processMyPort() {
    printf("My port: %d\n", MY_PORT);
}

int is_duplicate_connection(const char* ip, int port, int is_incoming) {
    for (int i = 0; i < connection_count; i++) {
        if (strcmp(inet_ntoa(connections[i].address.sin_addr), ip) == 0 && ntohs(connections[i].address.sin_port) == port && connections[i].is_incoming == is_incoming) {
            return 1;
        }
    }
    return 0;
}

void processConnect(const char *args) {
    char destination[100];
    int port;
    if (sscanf(args, "%99s %d", destination, &port) == 2) {
        printf("state: connect, destination: %s, port: %d\n", destination, port);

        if (is_duplicate_connection(destination, port, 0)) {
            printf("Connection to %s:%d already exists\n", destination, port);
            return;
        }

        int client_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (client_fd < 0) {
            perror("socket creation error");
            return;
        }

        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);

        if (inet_pton(AF_INET, destination, &serv_addr.sin_addr) <= 0) {
            printf("Invalid address/Address not supported\n");
            return;
        }

        if (connect(client_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            printf("Connection Failed\n");
            return;
        }

        pthread_mutex_lock(&connection_mutex);
        if (connection_count < MAX_CLIENTS) {
            connections[connection_count].socket_fd = client_fd;
            connections[connection_count].address = serv_addr;
            connections[connection_count].connection_id = connection_count + 1;
            connections[connection_count].is_incoming = 0; // Outgoing connection
            connection_count++;
            printf("Connected to %s:%d\n", destination, port);
        } else {
            printf("Connection limit reached\n");
            close(client_fd);
        }
        pthread_mutex_unlock(&connection_mutex);
    } else {
        printf("Invalid connect command format\n");
    }
}

void processList() {
    pthread_mutex_lock(&connection_mutex);
    printf("ID\tIP Address\tPort\n");
    for (int i = 0; i < connection_count; i++) {
        printf("%d\t%s\t%d\n", connections[i].connection_id, inet_ntoa(connections[i].address.sin_addr), ntohs(connections[i].address.sin_port));
    }
    pthread_mutex_unlock(&connection_mutex);
}

void processTerminate(const char *args) {
    int connection_id;
    if (sscanf(args, "%d", &connection_id) == 1) {
        pthread_mutex_lock(&connection_mutex);
        if (connection_id > 0 && connection_id <= connection_count) {
            int index = connection_id - 1;
            close(connections[index].socket_fd);
            for (int i = index; i < connection_count - 1; i++) {
                connections[i] = connections[i + 1];
            }
            connection_count--;
            printf("Connection ID %d terminated\n", connection_id);
        } else {
            printf("Invalid connection ID\n");
        }
        pthread_mutex_unlock(&connection_mutex);
    } else {
        printf("Invalid terminate command format\n");
    }
}

void processSend(const char *args) {
    int connection_id;
    char message[BUFFER_SIZE];
    if (sscanf(args, "%d %[^\n]", &connection_id, message) == 2) {
        pthread_mutex_lock(&connection_mutex);
        if (connection_id > 0 && connection_id <= connection_count) {
            send(connections[connection_id - 1].socket_fd, message, strlen(message), 0);
            printf("Message sent to connection ID: %d\n", connection_id);
        } else {
            printf("Invalid connection ID\n");
        }
        pthread_mutex_unlock(&connection_mutex);
    } else {
        printf("Invalid send command format\n");
    }
}

void processExit() {
    pthread_mutex_lock(&connection_mutex);
    for (int i = 0; i < connection_count; i++) {
        close(connections[i].socket_fd);
    }
    connection_count = 0;
    pthread_mutex_unlock(&connection_mutex);
    printf("Server exiting\n");
    exit(0);
}

void handle_new_connection() {
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);
    int new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
    if (new_socket < 0) {
        perror("accept failed");
        return;
    }

    printf("New connection from %s:%d\n", inet_ntoa(address.sin_addr), ntohs(address.sin_port));

    pthread_mutex_lock(&connection_mutex);
    if (connection_count < MAX_CLIENTS) {
        if (is_duplicate_connection(inet_ntoa(address.sin_addr), ntohs(address.sin_port), 1)) {
            printf("Incoming connection from %s:%d already exists\n", inet_ntoa(address.sin_addr), ntohs(address.sin_port));
            close(new_socket);
        } else {
            connections[connection_count].socket_fd = new_socket;
            connections[connection_count].address = address;
            connections[connection_count].connection_id = connection_count + 1;
            connections[connection_count].is_incoming = 1; // Incoming connection
            connection_count++;
        }
    } else {
        printf("Maximum clients reached, connection rejected\n");
        close(new_socket);
    }
    pthread_mutex_unlock(&connection_mutex);
}

void handle_client_message(int client_socket) {
    char buffer[BUFFER_SIZE];
    int valread = read(client_socket, buffer, BUFFER_SIZE - 1);
    if (valread == 0) {
        struct sockaddr_in address;
        socklen_t addrlen = sizeof(address);
        getpeername(client_socket, (struct sockaddr *)&address, &addrlen);
        printf("Client disconnected, ip %s, port %d\n", inet_ntoa(address.sin_addr), ntohs(address.sin_port));
        close(client_socket);

        pthread_mutex_lock(&connection_mutex);
        for (int i = 0; i < connection_count; i++) {
            if (connections[i].socket_fd == client_socket) {
                for (int j = i; j < connection_count - 1; j++) {
                    connections[j] = connections[j + 1];
                }
                connection_count--;
                break;
            }
        }
        pthread_mutex_unlock(&connection_mutex);
    } else if (valread < 0) {
        perror("read error");
    } else {
        buffer[valread] = '\0';
        printf("Received message: %s\n", buffer);
        char response[] = "Hello from Server\n";
        send(client_socket, response, strlen(response), 0);
    }
}

void* command_listener(void* arg) {
    char input[200];
    char command[10];
    char args[190];

    while (1) {
        printf("---------------------------------------------------------------------------------------------\n");
        printf("Enter command: ");
        if (!fgets(input, sizeof(input), stdin)) {
            break; // Exit on input error
        }

        // Remove trailing newline character
        input[strcspn(input, "\n")] = '\0';

        if (sscanf(input, "%9s %189[^\n]", command, args) < 1) {
            printf("Invalid input\n");
            continue;
        }

        State state = getState(command);

        switch (state) {
            case STATE_HELP:
                processHelp();
                break;
            case STATE_MYIP:
                processMyIP();
                break;
            case STATE_MYPORT:
                processMyPort();
                break;
            case STATE_CONNECT:
                processConnect(args);
                break;
            case STATE_LIST:
                processList();
                break;
            case STATE_TERMINATE:
                processTerminate(args);
                break;
            case STATE_SEND:
                processSend(args);
                break;
            case STATE_EXIT:
                processExit();
                return NULL;
            case STATE_INVALID:
            default:
                printf("Invalid command\n");
                break;
        }
        printf("---------------------------------------------------------------------------------------------\n");
    }

    return NULL;
}
