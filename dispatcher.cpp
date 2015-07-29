//C libraries
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netdb.h>

//C++ libraries
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <chrono>
#include <condition_variable>

#include "jsoncpp/json.h"

#include "Host.h"
#include "HttpRequest.h"
#include "AbstractDispatcher.h"
#include "SimpleRoundRobinDispatcher.h"

#define MAXPENDING 5
#define BUFFERSIZE 65535

using namespace std;

AbstractDispatcher* dispatcher;

int dispatcherSocket;
std::vector<std::thread> threads;
std::vector<Host> hosts;
std::queue<int> requests;

std::mutex request_mtx;
std::condition_variable request_cv;

int createDispatcherSocket (const char* port) {
    int n, errno;

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((n = getaddrinfo(NULL, port, &hints, &res)) != 0) {
        std::cerr << "Error getaddrinfo: " << strerror(n) << std::endl;
        return -1;
    }

    if ((dispatcherSocket = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0) {
        std::cerr << "Error: Can't create socket: " << strerror(errno) << std::endl;
        return -1;
    }

    if (bind(dispatcherSocket, res->ai_addr, res->ai_addrlen) < 0) {
        close(dispatcherSocket);
        std::cerr << "Error: can't bind to socket: " << strerror(errno) << std::endl;
        return -1;
    }

    if (listen(dispatcherSocket, MAXPENDING) < 0) {
        std::cerr << "Error: can't listen to socket: " << strerror(errno) << std::endl;
        return -1;
    }

    return 0;
}

char *strnstr_(const char *haystack, const char *needle, size_t len_haystack, size_t len_needle) {
    if (len_haystack == 0) return (char *)haystack; /* degenerate edge case */
    if (len_needle == 0) return (char *)haystack; /* degenerate edge case */
    while ((haystack = strchr(haystack, needle[0]))) {
        if (!strncmp(haystack, needle, len_needle)) return (char *)haystack;
        haystack++; }
    return NULL;
}

int get_content_lenght1(const char *buf, const int size, const char *lengthname) {
    const char *hit_ptr;
    int content_length;
    hit_ptr = strnstr_(buf, lengthname, size, 15);
    if (hit_ptr == NULL) {
        return -1;
    }
    char format [50];
    strcpy(format,lengthname);
    strcat(format," %d");
    fflush(stdout);
    if (sscanf(hit_ptr, format, &content_length) != 1) {
        return -1;
    }
    return content_length;
}

int get_content_lenght(const char *buf, const int size) {
    int res = get_content_lenght1(buf, size, "Content-Length:");
    if (res == -1)
        res = get_content_lenght1(buf, size, "Content-length:");
        if (res == -1)
            res = get_content_lenght1(buf, size, "content-length:");
    return res;
}

void poll_requests(int id) {
#ifdef DEBUG
    std::cout << "Thread " << id << " started" << std::endl;
#endif
    while (1) {
        std::unique_lock<std::mutex> lck(request_mtx);
        while (requests.empty()) request_cv.wait(lck);
        int sock = requests.front();
        requests.pop();
        lck.unlock();

        HttpRequest r = HttpRequest();

        char *buf = new char[BUFFERSIZE];
        memset(buf, 0, BUFFERSIZE);
        int offset = 0;
        int recv_size = 0;
        int first_line_received = 0;
        int header_received = 0;
        char *http_body_start = NULL;
        char method[16], recource[32];
        char *content = NULL;
        int length = 0;
#ifdef DEBUG
        std::cout << "new request handled by " << id << std::endl;
#endif
        while ((recv_size = read(sock, buf+offset, BUFFERSIZE-offset)) > 0) {
#ifdef DEBUG
            std::cout << "received " << recv_size << " bytes" << std::endl;
            std::cout <<  buf << std::endl;
#endif
            offset += recv_size;

            if (!first_line_received) {
                char *hit_ptr;
                hit_ptr = strnstr_(buf, "\n", offset, 1);
                if (hit_ptr == NULL) {
                    continue;
                }
                first_line_received = 1;
                // first line received
                // it can be parsed for http method and recource
                int n;
                if ((n = sscanf(buf, "%15s %31s HTTP/1.1", (char *)&method, (char *)&recource)) == 2) {
                    r.setMethod(method);
                    //r.setResource(resource);
#ifdef DEBUG
                    std::cout << "HTTP Request: Method " << method << " Recource: " << recource << std::endl;
#endif
                } else {
                    std::cerr << "ERROR scanf " << n << std::endl;
                    break;
                }
            }

            if (!header_received) {
                char *hit_ptr;
                hit_ptr = strnstr_(buf, "\r\n\r\n", offset, 4);
                http_body_start = hit_ptr + 4;
                if (hit_ptr == NULL) {
                    std::cerr << "ERROR: not FOUND" << std::endl;
                    continue;
                }
                header_received = 1;
                // header delimiter reached
                length = get_content_lenght(buf, offset);
                r.setContentLength(length);
                if (length == -1)
                {
                    std::cerr << "ERROR: Could not read content length!" << std::endl;
                    break;
                } else {
#ifdef DEBUG
                   std::cout << "Header Received #### Content-Length: " << length << std::endl;
#endif
                }
            }

            // complete header was received
            // check whether message is complete
            if (http_body_start != NULL) {
                if (((http_body_start - buf) + length) == offset) {
#ifdef DEBUG
                    std::cout << "complete message received\n header: " << http_body_start-buf << std::endl;
#endif
                    content = http_body_start;
                    r.setContent(content);
                    break;
                }
            }

        }
        dispatcher->dispatch(r, sock);
    }
}

int readSettings(char const* path) {
    std::ifstream settingsFile (path);
    if (!settingsFile.is_open()) {
        std::cerr << "Error: Could not find settings file" << std::endl;        
        return -1;
    }
    
    Json::Reader r = Json::Reader();
    Json::Value v;
    if (!r.parse(settingsFile, v, false)) {
        std::cerr << "Error: Could not parse settings file! No valid JSON" << std::endl;
        return -1;
    }

    Json::Value jsonHosts = v.get("hosts", "");
    if (jsonHosts == "" || jsonHosts.isArray() == false || jsonHosts.size() == 0) {
        std::cerr << "Error: Settings file does not contain any host" << std::endl;
        return -1;
    }
    
    for (auto host: jsonHosts) {
        std::string url = host.get("url", "").asString();
        int port = host.get("port", "0").asInt();
        if (url != "" and port != 0) {
#ifdef DEBUG
            std::cout << "Found host with address " << url << ":" << port << std::endl;
#endif
            hosts.emplace_back(url, port);
        }
    }

    if (hosts.size() == 0) {
        std::cerr << "Error: Settings file does not contain any valid hosts" << std::endl;
        return -1;
    }

    int thread_count = v.get("threads", 7).asInt();
    for (int i = 1; i <= thread_count; ++i) {
        threads.emplace_back(poll_requests, i);
    }

    return 0;
}

int main(int argc, char const *argv[]) {
    if (argc != 3) {
        std::cout << "USAGE: ./a.out PORT HOSTS_SETTINGS" << std::endl;
        return 1;
    }

    if (readSettings(argv[2]) == -1)
        return -1;

    if (createDispatcherSocket(argv[1]) == -1)
        return -1;

#ifdef DEBUG
    std::cout << "Dispatcher listening on port " << argv[1] << "..." << std::endl;
#endif

    dispatcher = new SimpleRoundRobinDispatcher(&hosts);

    socklen_t client_addrlen = sizeof(client_addrlen);
    struct sockaddr client_addr;

    int clientSocket;

    while(1) {
        clientSocket = accept(dispatcherSocket, &client_addr, &client_addrlen);
        
        if (clientSocket < 0) {
            std::cerr << "Error: on accept" << std::endl;
            return -1;
        }
        
        std::unique_lock<std::mutex> lck(request_mtx);
        requests.push(clientSocket);
        request_cv.notify_one();

        if (client_addr.sa_family == AF_INET){
            struct sockaddr_in *client_addr_ip4 = (struct sockaddr_in *) &client_addr;
#ifdef DEBUG          
            std::cout << "client " << client_addr_ip4->sin_addr.s_addr << std::endl;
#endif

        } else {
            /* not an IPv4 address */
        }
    }

    for (auto& th : threads) th.join();

    close(dispatcherSocket);
    
    return 0;
}
