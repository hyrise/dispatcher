#include "Dispatcher.h"
#include "dbg.h"
#include "jsoncpp/json.h"
#include "RoundRobinDistributor.h"
#include "StreamDistributor.h"
#include "http.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define MAXPENDING 5
#define BUFFERSIZE 65535


void dispatch_requests_wrapper(Dispatcher *dispatcher, int id) {
    dispatcher->dispatch_requests(id);
}


void Dispatcher::dispatch_requests(int id) {
    Json::Reader m_reader = Json::Reader();

    debug("Thread %i started", id);

    while (1) {
        // Get an request out of the request queue
        while (1) {
            // TODO: Condition variables
            request_queue_mutex.lock();
            if (!request_queue.empty()) break;
            request_queue_mutex.unlock();
        }
        int sock = request_queue.front();
        request_queue.pop();
        request_queue_mutex.unlock();

        debug("New request: Handled by thread %i", id);
        struct HttpRequest *request = HttpRequestFromEndpoint(sock);

        distributor->dispatch(request, sock);
    }
}


Dispatcher::Dispatcher(char *port, char *settings_file) {
    this->port = port;

    std::ifstream settingsFile(settings_file);
    if (!settingsFile.is_open()) {
        log_err("Could not find settings file.");
        throw "Could not find settings file.";
    }
    
    Json::Reader r = Json::Reader();
    Json::Value v;
    debug("Parse settings");
    if (!r.parse(settingsFile, v, false)) {
        log_err("Could not parse settings file! No valid JSON.");
        throw "Could not parse settings file! No valid JSON.";
    }

    Json::Value jsonHosts = v.get("hosts", "");
    if (jsonHosts == "" || jsonHosts.isArray() == false || jsonHosts.size() == 0) {
        log_err("Settings file does not contain any host.");
        throw "Settings file does not contain any host.";
    }
    
    std::vector<struct Host*> *hosts = new std::vector<struct Host*>;
    for (auto host: jsonHosts) {
        std::string url = host.get("url", "").asString();
        int port = host.get("port", "0").asInt();
        if (url != "" and port != 0) {
            debug("Found host with address %s:%i", url.c_str(), port);
            struct Host *h = new struct Host;
            h->port = port;
            h->url = strdup(url.c_str());
            hosts->push_back(h);
        }
    }

    if (hosts->size() == 0) {
        log_err("Settings file does not contain any valid hosts.");
        throw "Settings file does not contain any valid hosts.";
    }

    thread_pool_size = v.get("threads", 7).asInt();

    std::string dispatch_algorithm = v.get("algorithm", "RoundRobin").asString();

    if (dispatch_algorithm == "Stream") {
        distributor = new StreamDistributor(hosts);
        debug("Used dispatching algorithm: Stream");
    } else {
        //SimpleRoundRobinDipatcher is the standard algorithm
        distributor = new RoundRobinDistributor(hosts);
        debug("Used dispatching algorithm: RoundRobin");
    }
}


int Dispatcher::create_socket() {
    int sock_fd, s;

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((s = getaddrinfo(NULL, port, &hints, &res)) != 0) {
        log_err("Error getaddrinfo: %s", gai_strerror(s));
        throw("Error getaddrinfo.");
    }

    if ((sock_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0) {
        log_err("Error: Can't create socket.");
        throw "Error: Can't create socket.";
    }

    if (::bind(sock_fd, res->ai_addr, res->ai_addrlen) < 0) {
        close(sock_fd);
        log_err("Error: can't bind to socket.");
        throw "Error: can't bind to socket.";
    }

    if (listen(sock_fd, MAXPENDING) < 0) {
        log_err("Error: can't listen to socket.");
        throw "Error: can't listen to socket.";
    }

    return sock_fd;
}

void Dispatcher::start() {
    debug("Start dispatcher");

    // Start parser threads
    for (int i = 0; i < thread_pool_size; ++i) {
        parser_thread_pool.emplace_back(dispatch_requests_wrapper, this, i);
    }

    // create dispatcher socket
    int socket = create_socket();
    debug("Dispatcher: Listening on port %s", port);

    socklen_t client_addrlen;
    struct sockaddr client_addr;
    int request_socket;

    // Disptach requests
    while(1) {
        request_socket = accept(socket, &client_addr, &client_addrlen);
        if (request_socket < 0) {
            log_err("Error: on accept.");
            throw "Error: on accept.";
        }
        
        request_queue_mutex.lock();
        request_queue.push(request_socket);
        request_queue_mutex.unlock();
    }
}


void Dispatcher::shut_down() {
    debug("Shut down dispatcher");
    for (auto& th : parser_thread_pool) {
        th.join();
    }
}
