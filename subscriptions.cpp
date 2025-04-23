#include <iostream>
#include <array>
#include <list>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <chrono>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "utils.h"
#include "subscriptions.h"


#define MAXTOPICSIZE 50




void pollAdjustable::addFd(int fd) {
    if (nfds == maxSize) {
        pollfds = (struct pollfd*)realloc(pollfds, maxSize * 2 * sizeof(struct pollfd));
        maxSize = maxSize * 2;
    }
    pollfds[nfds].fd = fd;
    pollfds[nfds].events = POLLIN;
    nfds++;
}

void pollAdjustable::freePoll() {
    free(pollfds);
    nfds = 0;
    maxSize = 0;
}

void pollAdjustable::removeLast(int fd) {
    close(fd);
    nfds--;
}

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



struct pollAdjustable initPoll() {
    struct pollAdjustable res;
    res.pollfds = (struct pollfd*)malloc(sizeof(struct pollfd) * 4);
    res.nfds = 0;
    res.maxSize = 4;
    return res;
}


std::string applicationProtocol(int fd) {
    uint16_t payload_len;
    int rc, res = 999, recv_len = 0;
    char some_buffer[1700];
    memset(some_buffer, 0, 1700);

    rc = recv(fd, &payload_len, 2, MSG_WAITALL);

    if (rc == 0) { // can't happen when we connect the first time
        return "";
    }


    DIE(rc < 0, "error in receiving"); // maybe not die here???
    payload_len = ntohs(payload_len);


    while (res > 0 && recv_len != payload_len) {
        res = recv(fd, some_buffer + recv_len, payload_len - recv_len, 0);
        recv_len += res;
        if (res == 0) { // can't happen when we connect the first time
            return "";
        }
    }
    // if (payload_len == 0) { // can't happen when we connect the first time, only if the user inputs us that
    //     return "";
    // }
    std::string buff = std::string(some_buffer);
    return buff;
}


void sendTcp(int socketFd, const char* str) {
    short len = strlen(str);
    char* finalStr = (char*)malloc((len + 3) * sizeof(char));
    strncpy(finalStr + 2, str, len + 1);
    *((short*) finalStr) = htons(len);



	int res = 999;
    short sendLen = 0;
    while (res > 0 && sendLen < len + 2) {
		res = send(socketFd, finalStr + sendLen, len + 2 - sendLen, 0);
		sendLen += res;
	}
    free(finalStr);
}


// THE STRINGS MUST BE STRINGS WITH \0 AT THE END (51 CHARACTER STRINGS) -> put this in utils
void split_at_first_separator(const char* str, char* first_part, char* rest_part) {
    // char str2[51];
    // strncpy(str2, str, MAXTOPICSIZE);
    // str2[MAXTOPICSIZE] = '\0';

    const char* sep = strchr(str, '/');

    if (sep != nullptr) {
        int len = sep - str;
        strncpy(first_part, str, len);
        first_part[len] = '\0';

        len = strlen(sep + 1);
        memmove(rest_part, sep + 1, len);
        rest_part[len] = '\0';
    } else {
        strcpy(first_part, str);
        rest_part[0] = '\0';
    }
}

bool isItRegEx(const char* exp) {
    char* expressionCopy = strdup(exp);
    char* first_part = (char*)malloc(sizeof(char) * 51);

    split_at_first_separator(expressionCopy, first_part, expressionCopy);
    if ((first_part[0] == '*' || first_part[0] == '+') && first_part[1] == '\0') {
        free(expressionCopy);
        free(first_part);
        return true;
    }

        
    while(expressionCopy[0] != '\0') {
        split_at_first_separator(expressionCopy, first_part, expressionCopy);
        if ((first_part[0] == '*' || first_part[0] == '+') && first_part[1] == '\0') {
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
        char* expression; // element
        bool subscribed; // subscription or unsubscription

    public:

        nonRegExSubscription(char* expr, bool sub) {
            expression = expr;
            subscribed = sub;
        }

        ~nonRegExSubscription() {
            free(expression);
        }

        bool match(char* expression, char* target) const override {
            return (strcmp(expression, target) == 0);
        }

        bool isSubscribed() const override {
            return subscribed;
        }

        char* getExpression() const override {
            return expression;
        }

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

        ~regExSubscription() {
            free(expression);
        }

        bool match(char* expression, char* target) const override {
            if ((expression[0] == '\0' && target[0] != '\0') || (expression[0] != '\0' && target[0] == '\0')) {
                return false;
            }
            if (expression[0] == '\0' && target[0] == '\0')
                return true;
        
            char* expressionCopy = strdup(expression);
            char* first_part1 = (char*)malloc(sizeof(char) * 51);
            char* first_part2 = (char*)malloc(sizeof(char) * 51);
            char* targetCopy;
            bool res;
            
            //std::cout << "The strings are: before " << expression << " " << target << std::endl;
            split_at_first_separator(expression, first_part1, expression);
            split_at_first_separator(target, first_part2, target);
            targetCopy = strdup(target);
            // std::cout << "The strings are: " << expression << " " << target << std::endl;
            // std::cout << "The parts are: " << first_part1 << " " << first_part2 << std::endl;
            if (first_part1[0] == '+' && first_part1[1] == '\0') {
                res = match(expression, target);
            } else if (first_part1[0] == '*' && first_part1[1] == '\0') {
                res = match(expression, target) || match(expressionCopy, targetCopy);
            } else if (strcmp(first_part1, first_part2) == 0) {
                res = match(expression, target);
            } else
                res = false;
            free(targetCopy);
            free(expressionCopy);
            free(first_part1);
            free(first_part2);
            return res;
        }

        bool isSubscribed() const override {
            return subscribed;
        }
        
        char* getExpression() const override {
            return expression;
        }
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
    if (!active) // if the user is inactive
        return false;

    bool match;
    char expressionCopy[51] = {0};
    char targetCopy[51] = {0};

    for (auto it = subscriptions.rbegin(); it != subscriptions.rend(); ++it) {
        strncpy(expressionCopy, (*it)->getExpression(), strlen((*it)->getExpression()) + 1);
        strncpy(targetCopy, target, strlen(target) + 1);
        match = (*it)->match(expressionCopy, targetCopy);

        if (match && (*it)->isSubscribed()) {
            return true;
        } else if (match && !(*it)->isSubscribed())
            return false;
    }
        
    return false;
}


void userIdSubscriptions::addSubscribe(char* topic) {
    if (!topic || topic[0] == '\0') return;

    if (isItRegEx(topic))
        subscriptions.push_back(new regExSubscription(topic, true));
    else
        subscriptions.push_back(new nonRegExSubscription(topic, true));
}


void userIdSubscriptions::addUnsubscribe(char* topic) {
    if (!topic || topic[0] == '\0') return;

    if (isItRegEx(topic))
        subscriptions.push_back(new regExSubscription(topic, false));
    else
        subscriptions.push_back(new nonRegExSubscription(topic, false));
}





// int main() {
//     // Crearea unui obiect de tip userIdSubscriptions
//     userIdSubscriptions userSubscriptions;

//     const char* expr1 = "*";
//     const char* expr2 = "+";
//     const char* expr3 = "topic/reeg/g/gg/*hj/";
//     const char* expr4 = "topic/+/*";
//     const char* expr5 = "topic/one/two/*";
//     const char* expr6 = "normal/topic";


//     std::cout << "Testing expr1: " << expr1 << " -> " << (isItRegEx(expr1) ? "true" : "false") << std::endl;
//     std::cout << "Testing expr2: " << expr2 << " -> " << (isItRegEx(expr2) ? "true" : "false") << std::endl;
//     std::cout << "Testing expr3: " << expr3 << " -> " << (isItRegEx(expr3) ? "true" : "false") << std::endl;
//     std::cout << "Testing expr4: " << expr4 << " -> " << (isItRegEx(expr4) ? "true" : "false") << std::endl;
//     std::cout << "Testing expr5: " << expr5 << " -> " << (isItRegEx(expr5) ? "true" : "false") << std::endl;
//     std::cout << "Testing expr6: " << expr6 << " -> " << (isItRegEx(expr6) ? "true" : "false") << std::endl;


//     userSubscriptions.addUnsubscribe("topic/abc");
//     userSubscriptions.addSubscribe("topic/*");
//     userSubscriptions.addSubscribe("topic/xyz");
//     userSubscriptions.addSubscribe("home/*/files/*/pictures/*");


//     auto start = std::chrono::high_resolution_clock::now();

//     const char* target1 = "home/user123/files/2022/pictures/summer";
//     const char* target2 = "topic/123";
//     const char* target3 = "topic/abc";

   
//     std::cout << "Testing match for target1 (" << target1 << "): " << userSubscriptions.getMatch(target1) << std::endl;
//     std::cout << "Testing match for target2 (" << target2 << "): " << userSubscriptions.getMatch(target2) << std::endl;
//     std::cout << "Testing match for target3 (" << target3 << "): " << userSubscriptions.getMatch(target3) << std::endl;

//     auto end = std::chrono::high_resolution_clock::now();
//     std::chrono::duration<double, std::milli> elapsed = end - start;

//     std::cout << "Time taken: " << elapsed.count() << " ms" << std::endl;

//     return 0;
// }



// struct userIdSubscriptions {
//     std::vector<char*> sortednoRegExSubscriptions;
//     std::list<char*> regExSubscriptions;

//     bool binarySearch(const char* target) {
//         int first = 0;
//         int last = sortednoRegExSubscriptions.size() - 1;
    
//         while (first <= last) {
//             int mid = (first + last) / 2;
//             int cmp = strcmp(sortednoRegExSubscriptions[mid], target);
    
//             if (cmp == 0)
//                 return true;
//             else if (cmp < 0)
//                 first = mid + 1;
//             else
//                 last = mid - 1;
//         }
//         return false;
//     }

//     bool match(char* expression, char* target) {
//         if ((expression[0] == '\0' && target[0] != '\0') || (expression[0] != '\0' && target[0] == '\0')) {
//             return false;
//         }
//         if (expression[0] == '\0' && target[0] == '\0')
//             return true;

//         char* expressionCopy = strdup(expression);
//         char* first_part1 = (char*)malloc(sizeof(char) * 51);
//         char* first_part2 = (char*)malloc(sizeof(char) * 51);
//         char* targetCopy;
//         bool res;

//         //std::cout << "The strings are: before " << expression << " " << target << std::endl;
//         split_at_first_separator(expression, first_part1, expression);
//         split_at_first_separator(target, first_part2, target);
//         targetCopy = strdup(target);
//         // std::cout << "The strings are: " << expression << " " << target << std::endl;
//         // std::cout << "The parts are: " << first_part1 << " " << first_part2 << std::endl;
//         if (first_part1[0] == '+' && first_part1[1] == '\0') {
//             res = match(expression, target);
//         } else if (first_part1[0] == '*' && first_part1[1] == '\0') {
//             res = match(expression, target) || match(expressionCopy, targetCopy);
//         } else if (strcmp(first_part1, first_part2) == 0) {
//             res = match(expression, target);
//         } else
//             res = false;
//         free(targetCopy);
//         free(expressionCopy);
//         free(first_part1);
//         free(first_part2);
//         return res;
//     }

//     bool getMatch(const char* target) {
//         char expressionCopy[51] = {0};
//         char targetCopy[51] = {0};

//         if (binarySearch(target))
//             return true;
//         else {
//             for (char* expression : regExSubscriptions) {
//                 strncpy(expressionCopy, expression, strlen(expression));
//                 strncpy(targetCopy, target, strlen(target));
//                 if (match(expressionCopy, targetCopy)) {
//                     return true;
//                 }
//             }
//         }
//         return false;
//     }


//     // would normally use string for heap usage but the message is guaranteed to be less than 51 characters

// };


// int main() {
//     userIdSubscriptions user;

//     // Add some test subscriptions
//     user.sortednoRegExSubscriptions.push_back(strdup("topic/one"));
//     user.sortednoRegExSubscriptions.push_back(strdup("topic/two"));
//     user.sortednoRegExSubscriptions.push_back(strdup("topic/three"));

//     user.regExSubscriptions.push_back(strdup("home/+/*/pictures/*/f/*/f/*"));
//     //user.regExSubscriptions.push_back(strdup("*/carne/*"));

//     const char* target3 = "home/user123/files/2022/pictures/g/f/a/b/f/g";

//     auto start = std::chrono::high_resolution_clock::now();

//     if (user.getMatch(target3)) {
//         std::cout << "Match found for target: " << target3 << std::endl;
//     } else {
//         std::cout << "No match for target: " << target3 << std::endl;
//     }

//     auto end = std::chrono::high_resolution_clock::now();
//     std::chrono::duration<double, std::milli> elapsed = end - start;

//     std::cout << "Time taken: " << elapsed.count() << " ms" << std::endl;

//     // Freeing allocated memory
//     for (char* subscription : user.sortednoRegExSubscriptions) {
//         free(subscription);
//     }

//     for (char* subscription : user.regExSubscriptions) {
//         free(subscription);
//     }

//     return 0;
// }