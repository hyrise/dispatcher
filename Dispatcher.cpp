#include "Dispatcher.h"
#include "dbg.h"
#include "jsoncpp/json.h"
#include "RoundRobinDistributor.h"
#include "StreamDistributor.h"

#include <iostream>
#include <fstream>
#include <sstream>

#define MAXPENDING 5
#define BUFFERSIZE 65535


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
    // TODO refactor
    int res = get_content_lenght1(buf, size, "Content-Length:");
    if (res == -1)
        res = get_content_lenght1(buf, size, "Content-length:");
        if (res == -1)
            res = get_content_lenght1(buf, size, "content-length:");
    return res;
}


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

        debug("new request handled by %i", id);

        while ((recv_size = read(sock, buf+offset, BUFFERSIZE-offset)) > 0) {
            debug("received %i bytes", recv_size);
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
                    r.setResource(recource);
                    debug("HTTP Request: Method %s Recource: %s", method, recource);
                } else {
                    log_err("ERROR scanf %d", n);
                    break;
                }
            }

            if (!header_received) {
                char *hit_ptr;
                hit_ptr = strnstr_(buf, "\r\n\r\n", offset, 4);
                http_body_start = hit_ptr + 4;
                if (hit_ptr == NULL) {
                    log_err("ERROR: not FOUND");
                    continue;
                }
                header_received = 1;
                // header delimiter reached
                length = get_content_lenght(buf, offset);
                r.setContentLength(length);
                if (length == -1)
                {
                    log_err("ERROR: Could not read content length!");
                    break;
                } else {
                    debug("Header Received #### Content-Length: %i", length);
                }
            }

            // complete header was received
            // check whether message is complete
            if (http_body_start != NULL) {
                if (((http_body_start - buf) + length) == offset) {
                    //debug("complete message received\n header:  %s", http_body_start-buf);

                    content = http_body_start;
                    r.setContent(content);
                    break;
                }
            }
        }

        if (r.getResource() == "/query/") {
            if (r.hasDecodedContent("query")) {
                std::unique_ptr<Json::Value> root (new Json::Value);
                if (m_reader.parse(r.getDecodedContent("query"), (*root)) == false) {
                    log_err("Error parsing json: %s", m_reader.getFormattedErrorMessages().c_str());
                    debug("%s", r.getContent());
                    debug("%s", r.getDecodedContent("query").c_str());
                    close(sock);
                    return;
                }
                distributor->dispatchQuery(r, sock, std::move(root));
            } else {
                distributor->dispatch(r, sock);
            }
        } else if (r.getResource() == "/procedure/") {
            distributor->dispatchProcedure(r, sock);
        } else {
            distributor->dispatch(r, sock);
        }
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
    
    std::vector<Host> *hosts = new std::vector<Host>;
    for (auto host: jsonHosts) {
        std::string url = host.get("url", "").asString();
        int port = host.get("port", "0").asInt();
        if (url != "" and port != 0) {
            debug("Found host with address %s:%i", url.c_str(), port);
            hosts->emplace_back(url, port);
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
    int client_socket;

    // Disptach requests
    while(1) {
        client_socket = accept(socket, &client_addr, &client_addrlen);
        if (client_socket < 0) {
            log_err("Error: on accept.");
            throw "Error: on accept.";
        }
        
        request_queue_mutex.lock();
        request_queue.push(client_socket);
        request_queue_mutex.unlock();
    }
}


void Dispatcher::shut_down() {
    debug("Shut down dispatcher");
    for (auto& th : parser_thread_pool) {
        th.join();
    }
}
