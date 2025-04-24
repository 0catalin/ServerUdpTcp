#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "subscriptions.h"
#include "utils.h"

/* function used to validate a message by a user */
bool validateMessage(char* str) {
  if (str[0] != ' ') return false;
  for (size_t i = 1; i < strlen(str); i++) {
    if (str[i] == ' ' || (str[i] == str[i + 1] && str[i] == '/')) return false;
  }
  if (str[strlen(str) - 1] == ' ') return false;
  return true;
}

int main(int argc, char* argv[]) {
  // treating command line argument error
  DIE(argc != 4, "we must give the 3 arguments");

  char stdin_buffer[100];
  int rc, socket_cli;

  // deactivating print buffering
  setvbuf(stdout, NULL, _IONBF, BUFSIZ);

  // the client socket used to communicate with the server
  socket_cli = socket(AF_INET, SOCK_STREAM, 0);
  DIE(socket_cli < 0, "error on creation of client socket");

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(atoi(argv[3]));
  server_addr.sin_addr.s_addr = inet_addr(argv[2]);

  // connecting to the server
  rc = connect(socket_cli, (struct sockaddr*)&server_addr, sizeof(server_addr));
  DIE(rc < 0, "error in connect");

  // send the server the username
  sendTcp(socket_cli, argv[1]);

  // initialize poll
  struct pollAdjustable pollStruct = initPoll();

  // add monitored fd's to the poll
  pollStruct.addFd(STDIN_FILENO);
  pollStruct.addFd(socket_cli);

  int val = 1;

  // deactivation of nagle's algorithm
  rc = setsockopt(socket_cli, IPPROTO_TCP, TCP_NODELAY, (char*)&val,
                  sizeof(int));
  DIE(rc < 0, "error in deactivation of nagle");

  while (1) {
    rc = poll(pollStruct.pollfds, pollStruct.nfds, -1);
    DIE(rc < 0, "error in poll");

    // we have input from stdin
    if ((pollStruct.pollfds[0].revents & POLLIN) != 0) {
      fgets(stdin_buffer, 99, stdin);
      // remove newline
      stdin_buffer[strlen(stdin_buffer) - 1] = '\0';
      // if the client wants to exit
      if (strcmp(stdin_buffer, "exit") == 0) {
        pollStruct.freeMemory();
        return 0;
        // if the client wants to subscribe
      } else if (strncmp(stdin_buffer, "subscribe", 9) == 0) {
        if (validateMessage(stdin_buffer + 9)) {
          sendTcp(socket_cli, stdin_buffer);
          std::cout << "Subscribed to topic " << (stdin_buffer + 10)
                    << std::endl;
        }
        // if the client wants to unsubscribe
      } else if (strncmp(stdin_buffer, "unsubscribe", 11) == 0) {
        if (validateMessage(stdin_buffer + 11)) {
          sendTcp(socket_cli, stdin_buffer);
          std::cout << "Unsubscribed from topic " << (stdin_buffer + 12)
                    << std::endl;
        }
      }
      // there was a notification from the server
    } else if ((pollStruct.pollfds[1].revents & POLLIN) != 0) {
      std::string notification = applicationProtocol(socket_cli);
      // if the server told us to shutdown
      if (notification == "shutdown") {
        pollStruct.freeMemory();
        return 0;
      }
      // if the message is valid print it
      if (notification != "") std::cout << notification << std::endl;
    }
  }
}
