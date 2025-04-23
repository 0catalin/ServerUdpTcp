CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17

all: server subscriber

server: server.cpp subscriptions.cpp
	$(CXX) $(CXXFLAGS) server.cpp subscriptions.cpp -o server

subscriber: client.cpp subscriptions.cpp
	$(CXX) $(CXXFLAGS) client.cpp subscriptions.cpp -o subscriber

clean:
	rm -f server subscriber