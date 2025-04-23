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
#include <unordered_set>
#include <unordered_map>
#include <netinet/tcp.h>
#include <csignal>
#include "utils.h"
#include "subscriptions.h"




static const char *IP = "127.0.0.1";




std::string convertTo2Decimals(uint16_t elem) {
    std::string s = std::to_string(elem * 1.0 / 100);
    size_t dot = s.find('.');
    if (dot != std::string::npos && dot + 3 < s.size()) {
        return s.substr(0, dot + 3);
    }
    return s;
}


// void sendTcp(int socketFd, const char* str) {
//     int initial = strlen(str);
// 	int res = 999;
//     int sendLen = 0;
//     while (res > 0 && sendLen < initial) {
// 		res = send(socketFd, str + sendLen, initial - sendLen, 0);
// 		sendLen += res;
// 	}
// }


// struct pollAdjustable {
//     struct pollfd* pollfds;
//     int nfds;
//     int maxSize;


//     void addFd(int fd) {
//         if (nfds == maxSize) {
//             pollfds = (struct pollfd*)realloc(pollfds, maxSize * 2 * sizeof(struct pollfd));
//             maxSize = maxSize * 2;
//         }
//         pollfds[nfds].fd = fd;
//         pollfds[nfds].events = POLLIN;
//         nfds++;
//     }

//     void freePoll() {
//         free(pollfds);
//         nfds = 0;
//         maxSize = 0;
//     }

//     void removeLast(int fd) {
//         close(fd);
//         nfds--;
//     }

//     void remove(int fd) {
//         for (int i = 0; i < nfds; ++i) {
//             if (pollfds[i].fd == fd) {
//                 nfds--;
//                 close(fd);
//                 memmove(&pollfds[i], &pollfds[i + 1], (nfds - i) * sizeof(struct pollfd));
//                 return;
//             }
//         }
//     }

//     void freeMemory() {
//         for (int i = 1; i < nfds; i++) {
//             close(pollfds[i].fd);
//         }
//         free(pollfds);
//         pollfds = nullptr;
//         nfds = 0;
//         maxSize = 0;
//     }
// };


// struct pollAdjustable initPoll() {
//     struct pollAdjustable res;
//     res.pollfds = malloc(sizeof(struct pollfd) * 4);
//     res.nfds = 0;
//     res.maxSize = 4;
//     return res;
// }


std::unordered_set<std::string> userIdSet;
std::unordered_map<std::string, userIdSubscriptions> userSubscriptions;
std::unordered_map<int, std::string> fdsToUsers; 
struct pollAdjustable pollStruct;


// std::string applicationProtocol(int fd) {
//     uint16_t payload_len;
//     int rc, res = 999, recv_len = 0;
//     char some_buffer[1600];

//     rc = recv(fd, &payload_len, 2, MSG_WAITALL);

//     if (rc == 0) { // can't happen when we connect the first time
//         pollStruct.remove(fd);
//         return "";
//     }

//     DIE(rc < 0, "error in receiving"); // maybe not die here???
//     payload_len = ntohs(payload_len);


//     while (res > 0 && recv_len != payload_len) {
//         res = recv(fd, some_buffer + recv_len, payload_len - recv_len, 0);
//         recv_len += res;
//         if (res == 0) { // can't happen when we connect the first time
//             pollStruct.remove(fd);
//             return "";
//         }
//     }

//     some_buffer[payload_len] = '\0';
//     // if (payload_len == 0) { // can't happen when we connect the first time, only if the user inputs us that
//     //     return "";
//     // }
//     std::string buff = std::string(some_buffer);
//     return buff;
// }


void shutdown_server(int /*signum*/) {
    pollStruct.freeMemory();
    DIE(1, "forced shutdown :)");
}


std::string manageUdp(void* buf, struct sockaddr_in* from) {
    char topic[51];
    uint8_t datatype;
    char ip_str[16] = {0};

    inet_ntop(AF_INET, &(from->sin_addr), ip_str, 16);
    memcpy(topic, buf, 50);
    topic[50] = '\0';

    std::string result = std::string(ip_str) + ":" + std::to_string(ntohs(from->sin_port)) + " - " + std::string(topic) + " - ";

    datatype = *((uint8_t*)((char*)buf + 50)); // static_cast<uint8_t>(buf[50]);

    char* content = &((char*)buf)[51];
    uint8_t sign_byte = *((uint8_t*)content);
 
    if (datatype == 0) {
        result = result + "INT - ";
        uint32_t elem = ntohl(*((uint32_t*)(&content[1])));
        if (sign_byte == 0) {
            result = result + std::to_string(elem);
        } else if (sign_byte == 1) {
            result = result + "-" + std::to_string(elem);
        } else {
            return "";
            // DROP THE PACKET
        }
    } else if (datatype == 1) {
        result = result + "SHORT_REAL - ";
        uint16_t elem = ntohs(*(uint16_t*)content);
        result = result + std::to_string(elem);
    } else if (datatype == 2) {
        result = result + "FLOAT - ";
        uint32_t number = ntohl(*(uint32_t*)(&content[1]));
        uint8_t power = *(uint8_t*)(&content[5]);
        double finish = number * 1.0 / pow(10, power);

        std::stringstream ss;
        ss << std::fixed << std::setprecision(power) << finish;

        if (sign_byte == 0)
            result = result + ss.str();
        else if (sign_byte == 1)
            result = result + "-" + ss.str();
        else
            return "";
            // DROP THE PACKET
    } else if (datatype == 3) {
        result = result + "STRING - " + std::string(content);
    } else {
        return "";
        // DROP THE PACKET!!!
    }

    return (std::to_string(htons((short)result.size())) + result);

}




int main(int argc, char *argv[]) {

    DIE(argc != 2, "we must give the port number as argument");
    uint16_t port = atoi(argv[1]);

    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    std::signal(SIGINT, shutdown_server);
    std::signal(SIGTERM, shutdown_server);
    std::signal(SIGQUIT, shutdown_server);

    int rc, connfd;
    struct sockaddr_in cli;
    socklen_t len = sizeof(cli);
    struct sockaddr from;
    socklen_t addrlen = sizeof(from);

    char stdin_buffer[11];
    char udp_recv[1700];
    char topic[51];



    pollStruct = initPoll();
    pollStruct.addFd(STDIN_FILENO);


    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    DIE(listenfd < 0, "error on listenfd socket creation");

    pollStruct.addFd(listenfd);

    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    DIE(udp_socket < 0, "error on udp socket creation");

    pollStruct.addFd(udp_socket);

    int val = 1;
    rc = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int));
	DIE(rc < 0, "set socket to restart server quickly after failure");
    rc = setsockopt(udp_socket, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int));
	DIE(rc < 0, "set socket to restart server quickly after failure");


    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = inet_addr(IP);
    server_address.sin_port = htons(port);

    rc = bind(listenfd, (struct sockaddr*)&server_address, sizeof(server_address));
    DIE(rc < 0, "bind listenfd socket failed");
    rc = bind(udp_socket, (struct sockaddr*)&server_address, sizeof(server_address));
    DIE(rc < 0, "bind udp socket failed");

    rc = listen(listenfd, 1);
    DIE(rc < 0, "error in listen in listenfd");
    

    while(1) {
        rc = poll(pollStruct.pollfds, pollStruct.nfds, -1);
        DIE(rc < 0, "error in poll");
        
        if ((pollStruct.pollfds[0].revents & POLLIN) != 0) { // STDIN
            fgets(stdin_buffer, 8, stdin);
            if (strncmp(stdin_buffer, "exit", 4) == 0) {
                // free memory and file descriptors and break
                pollStruct.freeMemory();
                return 0;
            }
        } else if ((pollStruct.pollfds[1].revents & POLLIN) != 0) { // TCP LISTENFD

            connfd = accept(pollStruct.pollfds[1].fd, (struct sockaddr *)&cli, &len);
            DIE(connfd < 0, "error in accept");

            rc = setsockopt(connfd, IPPROTO_TCP, TCP_NODELAY, (char *)&val, sizeof(int));
            DIE(rc < 0, "error in deactivation of nagle");

            pollStruct.addFd(connfd);

            std::string buff = applicationProtocol(connfd);
            if (buff != "") {
                if (userIdSet.find(buff) == userIdSet.end()) { // the first time a user appeared
                    char ip_str[16] = {0};
                    inet_ntop(AF_INET, &(cli.sin_addr), ip_str, 16);
                    std::cout << "New client " << buff << " connected from " << ip_str << ":" << ntohs(cli.sin_port) << std::endl;
                    
                    userIdSet.insert(buff);
                    userSubscriptions[buff] = userIdSubscriptions(true, connfd);
                    fdsToUsers[connfd] = buff;
                } else if ((userIdSet.find(buff) != userIdSet.end()) && !userSubscriptions[buff].active) { // the user was off and wants back
                    // for (Subscription* sub : userSubscriptions[buff].subscriptions) {
                    //     std::cout << sub->getExpression() << std::endl;
                    // }

                    char ip_str[16] = {0};
                    inet_ntop(AF_INET, &(cli.sin_addr), ip_str, 16);
                    std::cout << "Old client " << buff << " connected from " << ip_str << ":" << ntohs(cli.sin_port) << std::endl;
                    userSubscriptions[buff].active = true;
                    userSubscriptions[buff].fd = connfd;
                    fdsToUsers[connfd] = buff;
                } else { // the user is active already and is trying to connect
                    std::cout << "Client " << buff << " already connected." << std::endl;
                    sendTcp(connfd, "shutdown");
                    pollStruct.removeLast(connfd);
                }
            }
        } else if ((pollStruct.pollfds[2].revents & POLLIN) != 0) { // UDP SOCKET
            memset(udp_recv, 0, 1700);
            rc = recvfrom(pollStruct.pollfds[2].fd, udp_recv, 1700, 0, &from, &addrlen);
            std::string toSend = manageUdp((void*)udp_recv, (struct sockaddr_in*)&from);

            if (toSend != "") {
                const char* toSendChar = toSend.c_str();
                memcpy(topic, udp_recv, 50);
                topic[50] = '\0';
 
                for (const auto& entry : userSubscriptions) {
                    const userIdSubscriptions& value = entry.second;
                    if (value.getMatch(topic)) {
                        sendTcp(value.fd, toSendChar);
                    }
                }
            } else {
                // DROP PACKET
            }
        } else {
            for (int i = 3; i < pollStruct.nfds; i++) {
                if ((pollStruct.pollfds[i].revents & POLLIN) != 0) {
                    std::string buff = applicationProtocol(pollStruct.pollfds[i].fd);
                    std::string userId = fdsToUsers[pollStruct.pollfds[i].fd];
                    if (buff == "") {
                        fdsToUsers.erase(pollStruct.pollfds[i].fd);
                        pollStruct.remove(pollStruct.pollfds[i].fd);
                        userSubscriptions[userId].active = false;
                        std::cout << "Client " << userId << " disconnected." << std::endl;
                    } else {
                        std::istringstream iss(buff);
                        std::string command, topic;
                        iss >> command >> topic;
                        char* str = strdup(topic.c_str());
                        DIE (str == NULL, "error in strdup");
                        if (command == "unsubscribe") {
                            userSubscriptions[userId].addUnsubscribe(str);
                        } else if (command == "subscribe") {
                            userSubscriptions[userId].addSubscribe(str);
                        } else {
                            free(str);
                        }
                    }
                }
            }

        }

    }

    return 0;
}
