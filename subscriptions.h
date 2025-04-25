#ifndef SUBSCRIPTIONS_H
#define SUBSCRIPTIONS_H

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Subscription {
 public:
  virtual bool match(char* expression, char* target) const = 0;
  virtual bool isSubscribed() const = 0;
  virtual char* getExpression() const = 0;
  virtual ~Subscription() {}
};

struct userIdSubscriptions {
  std::vector<Subscription*> subscriptions;
  bool active;
  int fd;

  ~userIdSubscriptions();
  userIdSubscriptions(bool active = true, int userFd = -1);
  bool getMatch(char* target) const;
  void addSubscribe(char* topic);
  void addUnsubscribe(char* topic);
};

struct pollAdjustable {
  struct pollfd* pollfds;
  int nfds;
  int maxSize;

  int addFd(int fd);
  void removeLast(int fd);
  void remove(int fd);
  void freeMemory();
};

struct pollAdjustable initPoll();
std::string applicationProtocol(int fd);
void sendTcp(int socketFd, const char* str);

#endif
