all: server subscriber

server:
	g++ -Wall -Wextra -std=c++17 server.cpp subscriptions.cpp -o server

subscriber:
	g++ -Wall -Wextra -std=c++17 client.cpp subscriptions.cpp -o subscriber

clean:
	rm -f server subscriber