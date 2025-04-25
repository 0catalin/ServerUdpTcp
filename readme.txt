                                            Assignment 2 PCom -> UDP + TCP server




    The Application protocol

    Since the messages can't be longer than 2000 characters, I chose to send keep 2 bytes of the length of the message before
the actual message (uint16_t). In the sendTcp function I simply convert the length into network byte order and send the 
whole string. When I send I send until the bytes sent are equal to the length of the string. When I receive, I receive 
2 bytes, convert back to host order, then receive until the amount received is the initial calculated length (in 
applicationProtocol function).




    The polling mechanism

    Polling is used because we do not need blocking function calls when needing to read from more sockets, since we do not know which
socket is going to send us the packet.


        - the client can either read from : stdin, we remove the newline :
            ; if the command is exit we free memory and return;
            ; if the command is subscribe or unsubscribe we check the string to be valid and send if valid
                                          : the server socket, and if the notification is "shutdown" we close the server,
            if the notification is something else different from Null, we print it
        - the server can read from : stdin, we check if the command is exit, and if it is we free memory and notify the clients
                                   : the listenfd tcp socket, if we get a message we know that it is the username and add the necessary info
                                to the data structures, unless we are trying to connect with the same id, where we shut him down
                                   : the udp socket, where we compute the string, iterate through all the user structures and
                                   use the match function to only send them where they match (along with the 2 bytes of length)
                                   : one of the clients subscribing or unsubscribing, if the buffer sent is "", then they deconnected
                                so we close their connection, while removing the corresponding fields from the data structures




    I have treated the following error cases (and more not mentioned):

        Cases treated by DIE macro:
    - not enough command line arguments (for both the client and the server)
    - error in initial poll structure allocation (malloc), since the server/client is just starting
    - in socket creation, since they are just starting
    - in setsockopt error
    - in binding sockets
    - in listening on tcp socket
    - in poll

        Cases treated by ERROR macro:
    - when we need to realloc the poll structure for another fd
    - for all the other times when we need to allocate memory with malloc, realloc or strdup
    - when we need to alocate to insert new subscriptions
    - when we receive a new connection and connect fails us

        In functions
    - in applicationProtocol when we receive EOF or error we can't close the server, we return an empty string and evaluate it in 
the caller of the function
    - manageUdp which checks if the udp packet was successfully transmitted (WE ONLY DROP IT IF WE REALLY CAN'T KNOW THE 
PACKET CONSTRUCTION), otherwise we want the client to receive the data, even if it is a bit modified
    - in validateMessage where we check if a client sent something worth sending to the server (ALSO CHECK AGAINST SERVER BUFFER OVERFLOW)




    Data Structures

    - I used an unordered map with all the usernames for O(1) lookup and insertion
    - I used a fds to usernames map for O(1) lookup 
    - I used a map with username -> (subscriptions, online/offline (boolean), fd)

    I needed to use a strong fd <-> user relationship because the user 
can always change fd and I either had one or the other, needing both.



    Global variable

        I needed to use a global variable because I needed all my clients to shutdown gracefully when the server stopped because of
    other actions (not a simple 'exit'). So I used signal handlers. They can't take parameters and they needed the structure to call 
    its function, and that is the justification of using 1 global variable. I did the same in both the server and client.



    Variable user number

    I made a personalized structure for my users to be able to be "infinite" -> not really (maybe just how many fd's it can hold).
    It holds the array needed for polling, the number of elements and the maxSize (allocated memory). Its afferent functions are:
        - initPoll which initializes the poll structure
        - addFd which adds a new socket file descriptor
        - removeLast which removes the last element
        - remove which removes the element from the middle and moves the others back for the poll function to work
        - freeMemory which sends a "shutdown" message to all the client sockets which are connected to the server and frees memory



    Matching algorithm structure and algorithm

    Each user has a structure allocated to it, with subscriptions and other elements. The subscription can be a RegExSubscription or
a nonRegEx, and they have different methods of matching. When we introduce a subscription, we either introduce it as a subscribe,
or an unsubscribe. If it is regex we create a RegEx object and otherwise we create a non RegEx object. They have different matching
functions, a non regex can simply strcmp to check, and a regex object has to do a more complicated algorithm, we will get to that later.

    The match function tells us if we are subscribed or we are not subscribed. There are many useCases where we could subscribe to 
a regEx pattern which matches another unsubscribed RegEx pattern and we can do that for many subscriptions. Using a trie or any
other data structure would probably confuse the client and probably myself too, and so I decided to make a rule called 
"The subscription is decided by the last action on the path". So the algorithm is simply matching through each subscription from 
last to first, and if there is a match, we return the status of the subscription (subscribed or unsubscribed). Otherwise, if we 
reached the end, the user never 'touched' that subject.

    The regEx matching algorithm is recursive and it uses backtracking. we split the 2 matching strings into 2 others, by the character
'/', if there is no star and there is a match, we continue to compare the second part, otherwise we end the search. If we have a 
'*', there are 2 cases -> the case where the full starred string is matched with the second part of the other string
                       -> the case where we compare, like before, the second part of the first with the second part of the second

        


    Macros

    I used the macro DIE which exits the program and prints a message if a condition is met.
    I used the macro ERROR which prints the given message at stderr.



    Memory

    The memory cleanup will always be done in both the client and server, along with the file descriptors.



    Coding Style

    The project follows the Google C++ Style Guide for consistency, readability, and maintainability.






                                                                                    Oprea Andrei Cătălin 324CD 2025
