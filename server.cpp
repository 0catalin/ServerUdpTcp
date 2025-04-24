#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "subscriptions.h"
#include "utils.h"

struct pollAdjustable pollStruct;

std::string convertTo2Decimals(uint16_t elem) {
  std::string s = std::to_string(elem * 1.0 / 100);
  size_t dot = s.find('.');
  if (dot != std::string::npos && dot + 3 < s.size()) {
    return s.substr(0, dot + 3);
  }
  return s;
}

/* function which gets called when one of the 3 signals is trying to kill out
 * program */
void shutdown_server(int signum) {
  std::cerr << "\nWe received signal " << signum
            << " the server and its clients will be shut down" << std::endl;
  pollStruct.freeMemory();
  DIE(1, "forced shutdown :) ");
}

/* function which builds the buffer to be sent to all the matching users */
std::string manageUdp(void* buf, struct sockaddr_in* from, int size) {
  // buffer clearily invalid, must drop
  if (size < 51) return "";

  char topic[51];
  uint8_t datatype;
  char ip_str[16] = {0};

  inet_ntop(AF_INET, &(from->sin_addr), ip_str, 16);
  memcpy(topic, buf, 50);
  topic[50] = '\0';

  // we start creating the string
  std::string result = std::string(ip_str) + ":" +
                       std::to_string(ntohs(from->sin_port)) + " - " +
                       std::string(topic) + " - ";

  // the 50th byte -> datatype
  datatype = *((uint8_t*)((char*)buf + 50));

  // the rest of the buffer
  char* content = &((char*)buf)[51];
  // the sign byte for the cases where it exists
  uint8_t sign_byte = *((uint8_t*)content);

  // Integer
  if (datatype == 0) {
    result = result + "INT - ";
    uint32_t elem = ntohl(*((uint32_t*)(&content[1])));
    if (sign_byte == 0) {
      result = result + std::to_string(elem);
    } else if (sign_byte == 1) {
      if (elem != 0)
        result = result + "-" + std::to_string(elem);
      else
        result = result + std::to_string(elem);
      // the byte sign is invalid, we must drop
    } else {
      return "";
    }
    // short real number
  } else if (datatype == 1) {
    result = result + "SHORT_REAL - ";
    uint16_t elem = ntohs(*(uint16_t*)content);
    result = result + std::to_string(elem * 1.0 / 100);

  } else if (datatype == 2) {
    result = result + "FLOAT - ";
    // computes the float number parts
    uint32_t number = ntohl(*(uint32_t*)(&content[1]));
    uint8_t power = *(uint8_t*)(&content[5]);
    double finish = number * 1.0 / pow(10, power);

    // creates the string by setting precision to the "finish number"
    std::stringstream ss;
    ss << std::fixed << std::setprecision(power) << finish;

    if (sign_byte == 0)
      result = result + ss.str();
    else if (sign_byte == 1)
      result = result + "-" + ss.str();
    // the byte sign is invalid, we must drop
    else
      return "";
  } else if (datatype == 3) {
    result = result + "STRING - " + std::string(content);
    // we must drop the packet if the datatype is invalid
  } else {
    return "";
  }

  return result;
}

int main(int argc, char* argv[]) {
  DIE(argc != 2, "we must give the port number as argument");
  uint16_t port = atoi(argv[1]);
  // treating command line argument error

  setvbuf(stdout, NULL, _IONBF, BUFSIZ);
  // deactivating print buffering

  int rc, connfd;
  struct sockaddr_in cli;
  socklen_t len = sizeof(cli);
  struct sockaddr from;
  socklen_t addrlen = sizeof(from);

  char stdin_buffer[11];
  char udp_recv[1700];
  char topic[51];

  // set with all the logged in users -> O(1) average complexity for insertion
  // and search
  std::unordered_set<std::string> userIdSet;

  // map of user -> structure having user fd, a boolean indicating whether the
  // user is active or not and a vector of the user's subscriptions
  std::unordered_map<std::string, userIdSubscriptions> userSubscriptions;

  // a map of the user's file descriptor -> his/her username
  std::unordered_map<int, std::string> fdsToUsers;

  // initialization of structure with the array of struct pollfd needed for
  // polling
  pollStruct = initPoll();
  rc = pollStruct.addFd(
      STDIN_FILENO);  // no need to check error, no realloc happening

  // creating socket for tcp connexions
  int listenfd = socket(AF_INET, SOCK_STREAM, 0);
  DIE(listenfd < 0, "error on listenfd socket creation");

  // adding the tcp socketfd to the poll structure
  rc = pollStruct.addFd(
      listenfd);  // no need to check error, no realloc happening

  // creating socket for datagrams (udp)
  int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
  DIE(udp_socket < 0, "error on udp socket creation");

  // adding the udp socketfd to the poll structure
  pollStruct.addFd(udp_socket);  // no need to check error, no realloc happening

  // setting sockets to be reused if the server stops randomly
  int val = 1;
  rc = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int));
  DIE(rc < 0, "set socket to restart server quickly after failure");
  rc = setsockopt(udp_socket, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int));
  DIE(rc < 0, "set socket to restart server quickly after failure");

  // signal handlers to gracefully shut down the server
  // when receiving Ctrl + C or other signals
  std::signal(SIGINT, shutdown_server);
  std::signal(SIGTERM, shutdown_server);
  std::signal(SIGQUIT, shutdown_server);

  // creating a sockaddr_in structure to be used for bind
  struct sockaddr_in server_address;
  memset(&server_address, 0, sizeof(server_address));

  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(port);

  // binding the udp and tcp socket to the same ip and port
  rc =
      bind(listenfd, (struct sockaddr*)&server_address, sizeof(server_address));
  DIE(rc < 0, "bind listenfd socket failed");
  rc = bind(udp_socket, (struct sockaddr*)&server_address,
            sizeof(server_address));
  DIE(rc < 0, "bind udp socket failed");

  // listening for new connexions on the tcp socket
  rc = listen(listenfd, 1);
  DIE(rc < 0, "error in listen in listenfd");

  while (1) {
    rc = poll(pollStruct.pollfds, pollStruct.nfds, -1);
    // freeing the memory and alerting the customers before using DIE
    if (rc < 0) pollStruct.freeMemory();
    DIE(rc < 0, "error in poll");

    // we have input from stdin
    if ((pollStruct.pollfds[0].revents & POLLIN) != 0) {
      fgets(stdin_buffer, 8, stdin);
      // remove newline
      stdin_buffer[strlen(stdin_buffer) - 1] = '\0';
      if (strcmp(stdin_buffer, "exit") == 0) {
        // free memory and file descriptors and return
        pollStruct.freeMemory();
        return 0;
      }
      // we received input from TCP listen file descriptor
    } else if ((pollStruct.pollfds[1].revents & POLLIN) != 0) {
      // accepting connection and treating error case
      connfd = accept(pollStruct.pollfds[1].fd, (struct sockaddr*)&cli, &len);
      if (connfd < 0) {
        ERROR("error in accepting connection\n");
        continue;
      }

      // deactivation of NAGLE'S ALGORITHM
      rc = setsockopt(connfd, IPPROTO_TCP, TCP_NODELAY, (char*)&val,
                      sizeof(int));
      if (rc < 0) {
        ERROR("error in deactivation of nagle\n");
        close(connfd);
        continue;
      }

      // Adds the fd to the poll structure
      rc = pollStruct.addFd(connfd);
      if (rc == -1) {
        // we notify the client to shutdown
        sendTcp(connfd, "shutdown");
        close(connfd);
        continue;
      }

      // we look for the username from the connfd
      std::string buff = applicationProtocol(connfd);
      // if the username was correctly transmitted (no spaces and not an empty
      // string)
      if (buff != "" &&
          std::find(buff.begin(), buff.end(), ' ') == buff.end()) {
        // if it is the first time a user connected
        if (userIdSet.count(buff) == 0) {
          char ip_str[16] = {0};
          inet_ntop(AF_INET, &(cli.sin_addr), ip_str, 16);
          std::cout << "New client " << buff << " connected from " << ip_str
                    << ":" << ntohs(cli.sin_port) << std::endl;

          // we add him to our data structures
          userIdSet.insert(buff);
          userSubscriptions[buff] = userIdSubscriptions(true, connfd);
          fdsToUsers[connfd] = buff;
          // if the user was off and this is when he logs back on
        } else if ((userIdSet.count(buff) != 0) &&
                   !userSubscriptions[buff].active) {
          char ip_str[16] = {0};
          inet_ntop(AF_INET, &(cli.sin_addr), ip_str, 16);
          std::cout << "New client " << buff << " connected from " << ip_str
                    << ":" << ntohs(cli.sin_port) << std::endl;

          // we update the data structures / add him back
          userSubscriptions[buff].active = true;
          userSubscriptions[buff].fd = connfd;
          fdsToUsers[connfd] = buff;
          // the user is active already and is trying to connect from another
          // device
        } else {
          std::cout << "Client " << buff << " already connected." << std::endl;
          sendTcp(connfd, "shutdown");
          pollStruct.removeLast(connfd);
        }
        // if there was an error in transmitting the username
      } else {
        sendTcp(connfd, "shutdown");
        pollStruct.removeLast(connfd);
      }
      // we receive a message from the UDP socket
    } else if ((pollStruct.pollfds[2].revents & POLLIN) != 0) {
      // we compute the toSend string that we would have to send to the clients
      memset(udp_recv, 0, 1700);
      rc = recvfrom(pollStruct.pollfds[2].fd, udp_recv, 1700, 0, &from,
                    &addrlen);
      std::string toSend =
          manageUdp((void*)udp_recv, (struct sockaddr_in*)&from, rc);

      // the udp packet was successfully transmitted
      if (toSend != "") {
        const char* toSendChar = toSend.c_str();
        memcpy(topic, udp_recv, 50);
        topic[50] = '\0';

        // we iterate through the users and if they match we send the string
        for (const auto& entry : userSubscriptions) {
          const userIdSubscriptions& value = entry.second;
          if (value.getMatch(topic)) {
            sendTcp(value.fd, toSendChar);
          }
        }
      } else {
        // the udp packet is invalid
      }
    } else {
      // the users sent data through our connect sockets
      for (int i = 3; i < pollStruct.nfds; i++) {
        if ((pollStruct.pollfds[i].revents & POLLIN) != 0) {
          // we receive data and get the id
          std::string buff = applicationProtocol(pollStruct.pollfds[i].fd);
          std::string userId = fdsToUsers[pollStruct.pollfds[i].fd];
          // if the data is EOF (connection closed)
          if (buff == "") {
            // we erase the user from our data structures
            fdsToUsers.erase(pollStruct.pollfds[i].fd);
            pollStruct.remove(pollStruct.pollfds[i].fd);
            userSubscriptions[userId].active = false;
            std::cout << "Client " << userId << " disconnected." << std::endl;
          } else {
            // we split the string into 2 strings
            std::istringstream iss(buff);
            std::string command, topic;
            iss >> command >> topic;

            char* str = strdup(topic.c_str());
            if (str == nullptr) {
              ERROR("error in strdup in main server loop\n");
              continue;
            }
            // the check has been done in client, we just add the topic to the
            // client's structure
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
