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
#include <thread>
#include <vector>
#include <chrono>
#include <string>
#include <unordered_set>
#include <algorithm>
#include <stk/Stk.h>
#include <json.h>

#define DELAY_MS 1000
#define RATE 44100.
#define BUF_SIZE 1024

struct client {
    int clientFd;
    struct std::thread receive;
    struct std::thread send;
    bool end = false;
};
// server socket
int servFd;

// client sockets
std::unordered_set<int> clientFds;
std::vector<std::thread> r;
std::unordered_set<std::string> items;

// handles SIGINT
void ctrl_c(int);

// sends data to clientFds excluding fd
void sendToAllBut(int fd, char * buffer, int count);

// converts cstring to port
uint16_t readPort(char * txt);

// sets SO_REUSEADDR
void setReuseAddr(int sock);

// thread function
void sendMessage(int clientFd);
void receiveMessage(int clientFd);

long getCurrentTimeMilis();
void disconnectClient(int clientFd);
void sendCurrentItems(int clientFd);

constexpr unsigned int str2int(const char* str, int h = 0) {
    return !str[h] ? 5381 : (str2int(str, h+1) * 33) ^ str[h];
}

int main(int argc, char ** argv){
//    printf("libsoxr version %s\n", soxr_version());
    //printf("%f\n", stk::PI);
	if(argc != 2) error(1, 0, "Need 1 arg (port)");
	auto port = readPort(argv[1]);

	// create socket
	servFd = socket(AF_INET, SOCK_STREAM, 0);
	// get and validate port number
	int efd = epoll_create(0);
	epoll_event *events, event;
	events = nullptr;
	event.data.fd = efd;
	event.events = EPOLLIN | EPOLLET;
	epoll_ctl(efd, EPOLL_CTL_ADD, servFd, &event);

	if(servFd == -1) error(1, errno, "socket failed");

	// graceful ctrl+c exit
	signal(SIGINT, ctrl_c);
	// prevent dead sockets from throwing pipŝe errors on write
	signal(SIGPIPE, SIG_IGN);

	setReuseAddr(servFd);

	// bind to any address and port provided in arguments
	sockaddr_in serverAddr{.sin_family=AF_INET, .sin_port=htons((short)port), .sin_addr={INADDR_ANY}};
	int res = bind(servFd, (sockaddr*) &serverAddr, sizeof(serverAddr));
	if(res) error(1, errno, "bind failed");

	// enter listening mode
	res = listen(servFd, 1);
	if(res) error(1, errno, "listen failed");

/****************************/

	while(true){
		// prepare placeholders for client address
		epoll_wait(efd, events ,25, -1);
		sockaddr_in clientAddr{0};
		socklen_t clientAddrSize = sizeof(clientAddr);

		// accept new connection
		auto clientFd = accept(servFd, (sockaddr*) &clientAddr, &clientAddrSize);
		if(clientFd == -1)
            error(1, errno, "accept failed");
        else {
            clientFds.insert(clientFd);
            //create thread for new client
            r.push_back(std::thread(receiveMessage, clientFd));

            // tell who has connected
            printf("new connection from: %s:%hu (fd: %d)\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), clientFd);
            sendCurrentItems(clientFd);
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

void sendToAllBut(int fd, char * buffer, int count){
	int res;
	decltype(clientFds) bad;
	for(int clientFd : clientFds){
		if(clientFd == fd) continue;
		res = write(clientFd, buffer, count);
		if(res!=count)
			bad.insert(clientFd);
	}
	for(int clientFd : bad){
		printf("removing %d\n", clientFd);
		disconnectClient(clientFd);
	}
}

void receiveMessage(int clientFd) {
    printf("receiveMessage: clientFd: %d\n", clientFd);
    char buffer[BUF_SIZE];
    const char *figure;
    while(true) {
        for(int i = 0; i < BUF_SIZE; i++) {
            buffer[i] = 0;
        }
        int count = read(clientFd, buffer, BUF_SIZE);
        if(count > 0) {
            printf("buffer %d: %s\n", clientFd, buffer);
            // broadcast the message
            try {
                printf("Received %s\n", buffer);
                figure = "";
                json_object * jobj = json_tokener_parse(buffer);
                json_object_object_foreach(jobj, key, val) {
                    switch (str2int(key)) {
                        case str2int("figure"):
                            //printf("key: %s; value: %s\n", key, json_object_get_string(val));
                            figure = json_object_get_string(val);
                            break;
/*                        case str2int("x"):
                            //printf("key: %s; value: %s\n", key, json_object_get_double(val));
                            x = json_object_get_double(val);
                            break;
                        case str2int("y"):
                            //printf("key: %s; value: %s\n", key, json_object_get_double(val));
                            y = json_object_get_double(val);
                            break;
                        case str2int("rotation"):
                            //printf("key: %s; value: %s\n", key, json_object_get_double(val));
                            rotation = json_object_get_double(val);
                            break;
                        case str2int("scale"):
                            //printf("key: %s; value: %s\n", key, json_object_get_double(val));
                            scale = json_object_get_double(val);
                            break;
                        case str2int("color") :
                            //printf("key: %s; value: %s\n", key, json_object_get_string(val));
                            color = json_object_get_string(val);
                            break;
                        case str2int("text"):
                            //printf("key: %s; value: %s\n", key, json_object_get_string(val));
                            text = json_object_get_string(val);
                            break;*/
                    }
                }
                printf("Figure: %s\n", figure);
                std::string str(buffer);
                switch(str2int(figure)) {
                    case str2int("square") :
                        //char newItem[BUF_SIZE];
                        //strcpy(newItem, buffer);
                        items.insert(str);
                        sendToAllBut(clientFd, buffer, count);
                        break;
                    default :
                        printf("Nothing\n");
                        break;
                }
            } catch(const char* msg) {
                printf("Unknown data receive errno %d message %s\n", errno, msg);
            }
        }
        else {
            printf("read failed count: %d :: error: %d\n", count, errno);
            int wrote1 = write(clientFd, "{}", strlen("{}"));
            if(wrote1 == -1) {
                printf("Connection lost to %d\n", clientFd);
                disconnectClient(clientFd);
                break;
            }

        }
    }
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
    for(std::string item : items) {
        char buf[BUF_SIZE];
        strcpy(buf, item.c_str());
        strcat(buf, "\n");

        printf("sending %s\n", buf);
        int res = write(clientFd, buf, strlen(buf));
        if(res == -1) {//TODO check if all data were sent
            printf("connection broken client %d\n", clientFd);
            disconnectClient(clientFd);
        }
    }
}
