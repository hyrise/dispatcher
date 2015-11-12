#include "Dispatcher.h"
#include "dbg.h"
#include "jsoncpp/json.h"
#include "RoundRobinDispatcher.h"
#include "StreamDispatcher.h"

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
                    std::cerr << "Error parsing json:" << m_reader.getFormattedErrorMessages() << std::endl;
                    std::cout << r.getContent() << std::endl;
                    std::cout << r.getDecodedContent("query") << std::endl;
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

    std::vector<Host> *hosts = new std::vector<Host>;

    debug("Parse settings");

    std::ifstream settingsFile(settings_file);
    if (!settingsFile.is_open()) {        
        throw "Could not find settings file.";
    }
    
    Json::Reader r = Json::Reader();
    Json::Value v;
    debug("Parse settings");
    if (!r.parse(settingsFile, v, false)) {
        throw "Could not parse settings file! No valid JSON.";
    }

    Json::Value jsonHosts = v.get("hosts", "");
    if (jsonHosts == "" || jsonHosts.isArray() == false || jsonHosts.size() == 0) {
        throw "Settings file does not contain any host.";
    }
    
    for (auto host: jsonHosts) {
        std::string url = host.get("url", "").asString();
        int port = host.get("port", "0").asInt();
        if (url != "" and port != 0) {
            debug("Found host with address %s:%i", url.c_str(), port);
            hosts->emplace_back(url, port);
        }
    }

    if (hosts->size() == 0) {
        throw "Settings file does not contain any valid hosts.";
    }

    thread_pool_size = v.get("threads", 7).asInt();

    std::string dispatchAlgorithm = v.get("algorithm", "SimpleRoundRobin").asString();

    if (dispatchAlgorithm == "Stream") {
        distributor = new StreamDispatcher(hosts);
        debug("Used dispatching algorithm: Stream");
    } else {
        //SimpleRoundRobinDipatcher is the standard algorithm
        distributor = new RoundRobinDispatcher(hosts);
        debug("Used dispatching algorithm: SimpleRoundRobin");
    }
}


int Dispatcher::create_socket() {
    int n, errno;
    int sock_fd;
    std::stringstream error_stream;

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((n = getaddrinfo(NULL, port, &hints, &res)) != 0) {
        error_stream << "Error getaddrinfo: " << strerror(n);
        throw error_stream;
    }

    if ((sock_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0) {
        error_stream << "Error: Can't create socket: " << strerror(n);
        throw error_stream;
    }

    if (::bind(sock_fd, res->ai_addr, res->ai_addrlen) < 0) {
        close(sock_fd);
        error_stream << "Error: can't bind to socket: " << strerror(n);
        throw error_stream;
    }

    if (listen(sock_fd, MAXPENDING) < 0) {
        error_stream << "Error: can't listen to socket: " << strerror(n);
        throw error_stream;
    }

    return sock_fd;
}

void Dispatcher::start() {
    debug("Start dispatcher");

    for (int i = 1; i <= thread_pool_size; ++i) {
        parser_thread_pool.emplace_back(dispatch_requests_wrapper, this, i);
    }

    int socket = create_socket();
    debug("Dispatcher: Listening on port %s", port);

    socklen_t client_addrlen;
    struct sockaddr client_addr;
    int client_socket;

    while(1) {
        client_socket = accept(socket, &client_addr, &client_addrlen);
        
        if (client_socket < 0) {
            throw "Error: on accept";
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


