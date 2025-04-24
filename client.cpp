#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <netinet/tcp.h>
#include "utils.h"
#include "subscriptions.h"


bool validateMessage(char* str) {
    if (str[0] != ' ')
        return false;
    for (size_t i = 1; i < strlen(str); i++) {
        if (str[i] == ' ' || (str[i] == str[i + 1] && str[i] == '/'))
            return false;
    }
    if (str[strlen(str) - 1] == ' ')
        return false;
    return true;
}


int main(int argc, char* argv[]) {
    DIE(argc != 4, "we must give the 3 arguments");
    char stdin_buffer[100];
    int rc, socket_cli;

    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    socket_cli = socket(AF_INET, SOCK_STREAM, 0);
    DIE(socket_cli < 0, "error on creation of client socket");

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[3]));
    server_addr.sin_addr.s_addr = inet_addr(argv[2]);

    rc = connect(socket_cli, (struct sockaddr*)&server_addr, sizeof(server_addr));
    DIE(rc < 0, "error in connect");

    sendTcp(socket_cli, argv[1]);

    struct pollAdjustable pollStruct = initPoll();
    pollStruct.addFd(STDIN_FILENO);

    pollStruct.addFd(socket_cli);

    int val = 1;

    // deactivation of nagle's algorithm
    rc = setsockopt(socket_cli, IPPROTO_TCP, TCP_NODELAY, (char *)&val, sizeof(int));
    DIE(rc < 0, "error in deactivation of nagle");

    while (1) {
        rc = poll(pollStruct.pollfds, pollStruct.nfds, -1);
        DIE(rc < 0, "error in poll");

        if ((pollStruct.pollfds[0].revents & POLLIN) != 0) { // STDIN
            fgets(stdin_buffer, 99, stdin);
            stdin_buffer[strlen(stdin_buffer) - 1] = '\0';
            if (strncmp(stdin_buffer, "exit", 4) == 0) {
                pollStruct.freeMemory();
                return 0;
            } else if (strncmp(stdin_buffer, "subscribe", 9) == 0) {
                if (validateMessage(stdin_buffer + 9)) {
                    std::string noNewline = std::string(stdin_buffer).substr(0, strlen(stdin_buffer) - 1);
                    sendTcp(socket_cli, stdin_buffer);
                    std::cout << "Subscribed to topic " << (stdin_buffer + 10) << std::endl; 
                }
            } else if (strncmp(stdin_buffer, "unsubscribe", 11) == 0) {
                if (validateMessage(stdin_buffer + 11)) {
                    std::string noNewline = std::string(stdin_buffer).substr(0, strlen(stdin_buffer) - 1);
                    sendTcp(socket_cli, stdin_buffer);
                    std::cout << "Unsubscribed from topic " << (stdin_buffer + 12) << std::endl; 
                }
            }
        } else if ((pollStruct.pollfds[1].revents & POLLIN) != 0) {
            std::string notification = applicationProtocol(socket_cli);
            if (notification == "shutdown") {
                pollStruct.freeMemory();
                return 0;
            }
            std::cout << notification << std::endl;
        }
    }
}