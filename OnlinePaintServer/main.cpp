#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <error.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <poll.h>
#include <signal.h>
#include <fcntl.h>
#include <vector>
#include <chrono>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <algorithm>
#include <cstring>
#include <json.h>

#define DEFAULT_PORT 10101
#define BUFFER_SIZE 1024
#define MAX_EVENTS 4

// server socket
int servFd;

// client sockets
std::unordered_set<int> clientFds;
std::map<int, std::string> items;
std::string background;
int nextId = 0;

// handles SIGINT
void ctrl_c(int);

// converts cstring to port
uint16_t readPort(char * txt);

// sets SO_REUSEADDR
void setReuseAddr(int sock);

// thread function
void receiveMessage(int clientFd);

long getCurrentTimeMilis();
void disconnectClient(int clientFd);
void sendCurrentItems(int clientFd);
int sendToSingle(int clientFd, char* buf, int size);

constexpr unsigned int str2int(const char* str, int h = 0) {
    return !str[h] ? 5381 : (str2int(str, h+1) * 33) ^ str[h];
}

int main(int argc, char ** argv){
    unsigned short port;
	if(argc != 2) {
        printf("arg1: port\nSetting default port %d\n", DEFAULT_PORT);
        port = DEFAULT_PORT;
    }
    else {
        port = readPort(argv[1]);
        printf("Setting port %d\n", port);
    }

	// create socket
	servFd = socket(AF_INET, SOCK_STREAM, 0);
	// get and validate port number
	int efd = epoll_create(1);
	if(efd == -1) {
        perror("epoll_create");
        exit(EXIT_FAILURE);
    }

	// graceful ctrl+c exit
	signal(SIGINT, ctrl_c);
	// prevent dead sockets from throwing pipe errors on write
	signal(SIGPIPE, SIG_IGN);

	setReuseAddr(servFd);

	// bind to any address and port provided in arguments
	sockaddr_in serverAddr{.sin_family=AF_INET, .sin_port=htons((short)port), .sin_addr={INADDR_ANY}};
	int res = bind(servFd, (sockaddr*) &serverAddr, sizeof(serverAddr));
	if(res) error(1, errno, "bind failed");

	// enter listening mode
	res = listen(servFd, SOMAXCONN);
	if(res) error(1, errno, "listen failed");

	epoll_event events[MAX_EVENTS], event;
	event.data.fd = servFd;
	event.events = EPOLLIN;
	epoll_ctl(efd, EPOLL_CTL_ADD, servFd, &event);

	if(servFd == -1) error(1, errno, "socket failed");

/****************************/

	while(true) {
		// prepare placeholders for client address
		int nfds = epoll_wait(efd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }
        for(int i = 0; i < nfds; i++) {
            sockaddr_in clientAddr{0};
            socklen_t clientAddrSize = sizeof(clientAddr);

            if(events[i].data.fd == servFd) {
                // accept new connection
                int clientFd = accept(servFd, (sockaddr*) &clientAddr, &clientAddrSize);
                if(clientFd == -1)
                    error(1, errno, "accept failed");
                else {
                    event.events = EPOLLIN | EPOLLET;
                    event.data.fd = clientFd;
                    if (epoll_ctl(efd, EPOLL_CTL_ADD, clientFd, &event) == -1) {
                        perror("epoll_ctl: clientFd");
                        exit(EXIT_FAILURE);
                    }
                    clientFds.insert(clientFd);
                    printf("new connection from: %s:%hu (fd: %d)\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), clientFd);
                    sendCurrentItems(clientFd);
                }
            } else {
                receiveMessage(events[i].data.fd);
            }
        }
	}
}

uint16_t readPort(char * txt){
	char * ptr;
	auto port = strtol(txt, &ptr, 10);
	if(*ptr!=0 || port<1 || (port>((1<<16)-1))) error(1,0,"illegal argument %s", txt);
	return port;
}

void setReuseAddr(int sock){
	const int one = 1;
	int res = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if(res) error(1,errno, "setsockopt failed");
}

void ctrl_c(int){
	for(int clientFd : clientFds)
		close(clientFd);
	close(servFd);
	printf("Closing server\n");
	exit(0);
}

void sendToAll(const char * buffer, int count){
	decltype(clientFds) bad;
	for(int clientFd : clientFds){
        if(sendToSingle(clientFd, (char*) buffer, count) == -1) {
			bad.insert(clientFd);
        }

	}
	for(int clientFd : bad){
		printf("removing %d\n", clientFd);
		disconnectClient(clientFd);
	}
}

void receiveMessage(int clientFd) {
	ssize_t bufsize = BUFFER_SIZE;
    const char *buf;
    ssize_t received, receivedTotal;
    char *buffer, *temp;
    receivedTotal = 0;
    temp = (char*) malloc(bufsize * sizeof(char));
    buffer = (char*) malloc(bufsize * sizeof(char));
    fcntl(clientFd, F_SETFL, fcntl(clientFd, F_GETFL) & ~O_NONBLOCK);
    received = read(clientFd, temp, bufsize);
    if(received > 0) {
        buffer = strncpy(buffer, temp, received);
        fcntl(clientFd, F_SETFL, fcntl(clientFd, F_GETFL) | O_NONBLOCK);
        while(received > 0) {
            buffer = (char*) realloc(buffer, (receivedTotal + received + 1) * sizeof(char));
            memcpy(buffer + receivedTotal, temp, received);
            for(int i = 0; i < bufsize; i++) {
                temp[i] = 0;
            }
            receivedTotal += received;
            received = read(clientFd, (char*) temp, bufsize);
        }

        char *pch = strtok (buffer, "\n");
        while(pch != NULL) {
            try {
                printf("Received %s\n", pch);
                json_object * jObj = json_tokener_parse(pch);
                if(jObj != NULL) {
                    int id = -1;
                    const char *figure = "";
                    json_object_object_foreach(jObj, key, val) {
                        switch (str2int(key)) {
                            case str2int("figure"):
                                figure = json_object_get_string(val);
                                break;
                            case str2int("id"):
                                id = json_object_get_int(val);
                                break;
                        }
                    }
                    if(id < 0) {
                        id = nextId;
                    } else if(id > nextId) {
                        nextId = id;
                    }
                    json_object *jId = json_object_new_int(id);
                    json_object_object_add(jObj,"id", jId);
                    buf = json_object_to_json_string(jObj);
                    std::string str(buf);

                    switch(str2int(figure)) {
                        case str2int("square") : case str2int("circle") : case str2int("text") : case str2int("triangle") :
                            items[id] = str;
                            nextId++;
                            sendToAll(buf, strlen(buf));
                            break;
                        case str2int("background") :
                            background = buf;
                            sendToAll(buf, strlen(buf));
                            break;
                        case str2int("delete") :
                            items.erase(id);
                            sendToAll(buf, strlen(buf));
                            break;
                        default :
                            break;
                    }
                }
            } catch(const char* msg) {
                printf("Unknown data receive errno %d message %s\n", errno, msg);
            }
            pch = strtok (NULL,"\n");
        }
    }
    else {
        printf("Connection lost to %d\n", clientFd);
        disconnectClient(clientFd);
        //break;
    }
    free(temp);
    free(buffer);
}

long getCurrentTimeMilis() {
    return std::chrono::duration_cast< std::chrono::milliseconds >(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

void disconnectClient(int clientFd) {
    close(clientFd);
    printf("disconnected client\n");
}

void sendCurrentItems(int clientFd) {
    char *buf;
    if(!background.empty()) {
        buf = (char*) malloc(strlen(background.c_str()) + 3);
        strcpy(buf, background.c_str());
        strcat(buf, "\n");
        sendToSingle(clientFd, buf, -1);
    }

    for(std::pair<int, std::string> i : items) {
        buf = (char*) malloc(strlen(i.second.c_str()) + 3);
        strcpy(buf, i.second.c_str());
        strcat(buf, "\n");

        sendToSingle(clientFd, buf, -1);
        free(buf);
    }
}

int sendToSingle(int clientFd, char* buf, int size) {
    if(size < 0)
        size = strlen(buf);
    int sent = 0;
    while(sent < size) {
        int res = write(clientFd, buf + sent, size);
        if(res == -1) {
            disconnectClient(clientFd);
            return -1;
        } else {
            sent += res;
        }
    }
    return 0;
}
