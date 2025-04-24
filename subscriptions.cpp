#include "subscriptions.h"

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <list>
#include <vector>

#include "utils.h"

#define MAXTOPICSIZE 50

/* adds fd to structure, increases size if it doesn't have enough space */
int pollAdjustable::addFd(int fd) {
  if (nfds == maxSize) {
    struct pollfd* temp =
        (struct pollfd*)realloc(pollfds, maxSize * 2 * sizeof(struct pollfd));
    if (temp == nullptr) {
      ERROR("error in realloc in pollAdjustable\n");
      return -1;
    }
    pollfds = temp;
    maxSize = maxSize * 2;
  }
  pollfds[nfds].fd = fd;
  pollfds[nfds].events = POLLIN;
  nfds++;
  return 1;
}

/* removes the last element */
void pollAdjustable::removeLast(int fd) {
  close(fd);
  nfds--;
}

/* removes user and copies data back 1 space for the poll function to still work
 * properly */
void pollAdjustable::remove(int fd) {
  for (int i = 0; i < nfds; ++i) {
    if (pollfds[i].fd == fd) {
      nfds--;
      close(fd);
      memmove(&pollfds[i], &pollfds[i + 1], (nfds - i) * sizeof(struct pollfd));
      return;
    }
  }
}

/* frees memory and sends shutdown message to all the clients */
void pollAdjustable::freeMemory() {
  for (int i = 3; i < nfds; i++) {
    sendTcp(pollfds[i].fd, "shutdown");
  }
  for (int i = 1; i < nfds; i++) {
    close(pollfds[i].fd);
  }
  free(pollfds);
  pollfds = nullptr;
  nfds = 0;
  maxSize = 0;
}

/* allocates 4 spaces for the first fd's and fills in data */
struct pollAdjustable initPoll() {
  struct pollAdjustable res;
  res.pollfds = (struct pollfd*)malloc(sizeof(struct pollfd) * 4);
  DIE(res.pollfds == nullptr, "error in pollfds allocation, try again\n");

  res.nfds = 0;
  res.maxSize = 4;
  return res;
}

/* manages the cases where the tcp receives too much data at once or doesn't
 * receive it all in 1 go */
std::string applicationProtocol(int fd) {
  uint16_t payload_len;
  int rc, res = 999, recv_len = 0;
  char some_buffer[1700];
  memset(some_buffer, 0, 1700);

  // receives the number of bytes
  rc = recv(fd, &payload_len, 2, MSG_WAITALL);

  if (rc <= 0) return "";

  payload_len = ntohs(payload_len);

  // while we don't get all the bytes, we receive in loop
  while (res > 0 && recv_len != payload_len) {
    res = recv(fd, some_buffer + recv_len, payload_len - recv_len, 0);
    recv_len += res;
    if (res == 0) return "";
  }
  // creating a string object out of the received buffer
  std::string buff = std::string(some_buffer);
  return buff;
}

/* function which sends data in loop */
void sendTcp(int socketFd, const char* str) {
  uint16_t len = strlen(str);
  // allocate for the number of bytes sent
  char* finalStr = (char*)malloc((len + 3) * sizeof(char));

  if (finalStr == nullptr) {
    ERROR("error in malloc in sendTcp\n");
    return;
  }

  // copy the '\0' as well
  strncpy(finalStr + 2, str, len + 1);
  // place the length in there
  *((uint16_t*)finalStr) = htons(len);

  int res = 999;
  uint16_t sendLen = 0;
  // send while the size sent reaches the length
  while (res > 0 && sendLen < len + 2) {
    res = send(socketFd, finalStr + sendLen, len + 2 - sendLen, 0);
    sendLen += res;
  }
  free(finalStr);
}

/* function which splits at the first '/' and places the first and second part
 * in the given parametres*/
void split_at_first_separator(const char* str, char* first_part,
                              char* rest_part) {
  const char* sep = strchr(str, '/');

  if (sep != nullptr) {
    int len = sep - str;
    strncpy(first_part, str, len);
    first_part[len] = '\0';

    len = strlen(sep + 1);
    memmove(rest_part, sep + 1, len);
    rest_part[len] = '\0';
  } else {
    // if there is no separator we put '\0' in the rest_part string
    strcpy(first_part, str);
    rest_part[0] = '\0';
  }
}

/* function which checks if a string is a regExp*/
bool isItRegEx(const char* exp) {
  char* expressionCopy = strdup(exp);
  if (expressionCopy == nullptr) {
    ERROR("error in strdup in isItRegEx\n");
    return false;
  }

  char* first_part = (char*)malloc(sizeof(char) * 51);
  if (first_part == nullptr) {
    ERROR("error in malloc in isItRegEx\n");
    free(expressionCopy);
    return false;
  }

  split_at_first_separator(expressionCopy, first_part, expressionCopy);
  if ((first_part[0] == '*' || first_part[0] == '+') && first_part[1] == '\0') {
    free(expressionCopy);
    free(first_part);
    return true;
  }

  while (expressionCopy[0] != '\0') {
    split_at_first_separator(expressionCopy, first_part, expressionCopy);
    if ((first_part[0] == '*' || first_part[0] == '+') &&
        first_part[1] == '\0') {
      free(expressionCopy);
      free(first_part);
      return true;
    }
  }

  free(expressionCopy);
  free(first_part);

  return false;
}

class nonRegExSubscription : public Subscription {
 private:
  char* expression;  // element
  bool subscribed;   // subscription or unsubscription

 public:
  nonRegExSubscription(char* expr, bool sub) {
    expression = expr;
    subscribed = sub;
  }

  ~nonRegExSubscription() { free(expression); }

  bool match(char* expression, char* target) const override {
    return (strcmp(expression, target) == 0);
  }

  bool isSubscribed() const override { return subscribed; }

  char* getExpression() const override { return expression; }
};

class regExSubscription : public Subscription {
 private:
  char* expression;
  bool subscribed;

 public:
  regExSubscription(char* expr, bool sub) {
    expression = expr;
    subscribed = sub;
  }

  ~regExSubscription() { free(expression); }

  bool match(char* expression, char* target) const override {
    // base cases
    if ((expression[0] == '\0' && target[0] != '\0') ||
        (expression[0] != '\0' && target[0] == '\0'))
      return false;
    if (expression[0] == '\0' && target[0] == '\0') return true;

    char* expressionCopy = strdup(expression);
    if (expressionCopy == nullptr) {
      ERROR("error in strdup in match\n");
      return false;
    }

    char* first_part1 = (char*)malloc(sizeof(char) * 51);
    if (first_part1 == nullptr) {
      ERROR("error in malloc in match\n");
      free(expressionCopy);
      return false;
    }

    char* first_part2 = (char*)malloc(sizeof(char) * 51);
    if (first_part2 == nullptr) {
      ERROR("error in malloc in match\n");
      free(expressionCopy);
      free(first_part1);
      return false;
    }

    char* targetCopy;
    bool res;

    split_at_first_separator(expression, first_part1, expression);
    split_at_first_separator(target, first_part2, target);

    targetCopy = strdup(target);
    if (targetCopy == nullptr) {
      ERROR("error in strdup in match\n");
      free(expressionCopy);
      free(first_part1);
      free(first_part2);
      return false;
    }
    // we continue looking by matching the rest
    if (first_part1[0] == '+' && first_part1[1] == '\0') {
      res = match(expression, target);
      // we continue matching on both cases
    } else if (first_part1[0] == '*' && first_part1[1] == '\0') {
      res = match(expression, target) || match(expressionCopy, targetCopy);
      // we continue looking by matching the rest
    } else if (strcmp(first_part1, first_part2) == 0) {
      res = match(expression, target);
      // the current was not a match, return false
    } else
      res = false;

    free(targetCopy);
    free(expressionCopy);
    free(first_part1);
    free(first_part2);
    return res;
  }

  bool isSubscribed() const override { return subscribed; }

  char* getExpression() const override { return expression; }
};

userIdSubscriptions::~userIdSubscriptions() {
  for (auto sub : subscriptions) {
    delete sub;
  }
}

userIdSubscriptions::userIdSubscriptions(bool isActive, int userFd) {
  active = isActive;
  fd = userFd;
}

bool userIdSubscriptions::getMatch(char* target) const {
  if (!active)  // if the user is inactive
    return false;

  bool match;
  char expressionCopy[51] = {0};
  char targetCopy[51] = {0};

  // we iterate from end to beginning
  for (auto it = subscriptions.rbegin(); it != subscriptions.rend(); ++it) {
    strncpy(expressionCopy, (*it)->getExpression(),
            strlen((*it)->getExpression()) + 1);
    strncpy(targetCopy, target, strlen(target) + 1);
    match = (*it)->match(expressionCopy, targetCopy);

    // if there is a match and the match is a subscription
    if (match && (*it)->isSubscribed()) {
      return true;
      // if there is a match and the match is not a subscription
    } else if (match && !(*it)->isSubscribed())
      return false;
  }

  // there was no match
  return false;
}

void userIdSubscriptions::addSubscribe(char* topic) {
  if (!topic || topic[0] == '\0') {
    ERROR("the topic is invalid\n");
    return;
  }

  try {
    if (isItRegEx(topic)) {
      subscriptions.push_back(new regExSubscription(topic, true));
    } else {
      subscriptions.push_back(new nonRegExSubscription(topic, true));
    }

  } catch (const std::bad_alloc& e) {
    ERROR("could not allocate memory for subscription\n");
  } catch (const std::exception& e) {
    ERROR("could not add subscription to vector\n");
  }
}

void userIdSubscriptions::addUnsubscribe(char* topic) {
  if (!topic || topic[0] == '\0') {
    ERROR("the topic is invalid\n");
    return;
  }

  try {
    if (isItRegEx(topic))
      subscriptions.push_back(new regExSubscription(topic, false));
    else
      subscriptions.push_back(new nonRegExSubscription(topic, false));
  } catch (const std::bad_alloc& e) {
    ERROR("could not allocate memory for subscription\n");
  } catch (const std::exception& e) {
    ERROR("could not add subscription to vector\n");
  }
}
