#include "Dispatcher.h"
#include "dbg.h"
#include "jsoncpp/json.h"
#include "RoundRobinDistributor.h"
#include "StreamDistributor.h"
#include "http.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>

#define MAXPENDING 10


void Request_free(struct Request *request) {
    free(request);
}

void dispatch_requests_wrapper(Dispatcher *dispatcher, int id) {
    dispatcher->dispatch_requests(id);
}


std::string urlDecode(std::string &SRC) {
    std::string ret;
    char ch;
    unsigned int i, ii;
    for (i=0; i<SRC.length(); i++) {
        if (int(SRC[i])==37) {
            sscanf(SRC.substr(i+1,2).c_str(), "%x", &ii);
            ch=static_cast<char>(ii);
            ret+=ch;
            i=i+2;
        } else {
            ret+=SRC[i];
        }
    }
    return (ret);
}


int queryType(char *http_payload) {
    debug("Find out query type");
    if (http_payload == NULL) {
        return -1;
    }
    std::string http_payload_str(http_payload);
    Json::Reader reader = Json::Reader();
    size_t pLastKey, pValue, pNextKey;
    std::map<std::string, std::string> content;

    pLastKey = 0;
    while (true) {
        pValue = http_payload_str.find('=', pLastKey);
        pNextKey = http_payload_str.find('&', pValue);
        std::string key = http_payload_str.substr(pLastKey, pValue - pLastKey);
        std::string value = http_payload_str.substr(pValue + 1, pNextKey == std::string::npos ? pNextKey : pNextKey - pValue -1);
        content.emplace(key, urlDecode(value));
        if (pNextKey == std::string::npos) break;
        pLastKey = pNextKey + 1;
    }
    std::string &query_str = content.at("query");

    std::unique_ptr<Json::Value> root (new Json::Value);
    if (reader.parse(query_str, (*root)) == false) {
        log_err("Error parsing json: %s", reader.getFormattedErrorMessages().c_str());
        debug("HTTP payload was %s", http_payload);
        return -1;
    }
    std::unique_ptr<Json::Value> query = std::move(root);


    Json::Value operators;
    Json::Value obj_value(Json::objectValue);

    if (!query->isObject() || query->isMember("operators") == false) {
        log_err("query content does not contain any operators");
        throw "query content does not contain any operators";
    }
    operators = query->get("operators", obj_value);

    for (auto op: operators) {
        auto type = op.get("type", "").asString();
        if (type == "InsertScan" or
            type == "Delete" or
            type == "PosUpdateIncrementScan" or
            type == "PosUpdateScan") {
                return WRITE;
        }
        if (type == "TableLoad") {
            return LOAD;
        }
    }
    return READ;
}


void Dispatcher::dispatch_requests(int id) {
    debug("Thread %i started", id);

    while (1) {
        // Get an request out of the request queue
        struct Request *tcp_request;
        debug("Try to lock %d", id);
        {
            std::unique_lock<std::mutex> lck(request_queue_mutex);
            while (request_queue.empty()) {
                debug("Wait %d", id);
                request_queue_empty.wait(lck);
            };
            tcp_request = request_queue.front();
            request_queue.pop();
            debug("Unlock %d", id);
        }

        debug("New request: Handled by thread %i", id);

        // Allocates memory for the request
        struct HttpRequest *request = http_receive_request(tcp_request->socket);
        if (request == NULL) {
            debug("Invalid Http request.");
            // TODO send error msg to client
            close(tcp_request->socket);
            Request_free(tcp_request);
            continue;
        }
        debug("Request payload: %s", request->payload);

        if (strncmp(request->resource, "/add_node/", 10) == 0) {
            if (tcp_request->addr.sa_family == AF_INET || tcp_request->addr.sa_family == AF_UNSPEC) {
                struct sockaddr_in *addr = (struct sockaddr_in *)&(tcp_request->addr);
                char ip[INET_ADDRSTRLEN];
                if (inet_ntop(AF_INET, &(addr->sin_addr), ip, INET_ADDRSTRLEN) == NULL) {
                    log_err("/add_node/ - Converting network address to string");
                }
                int port = (int)strtol(request->resource+10, (char **)NULL, 10);
                if (port == 0) {
                    log_err("/add_node/ - Detecting port");
                }
                debug("Add host:  %s:%i", ip, port);
                add_host(ip, port);
            } else {
                debug("Cannot add host: Unsupported Address family %d", tcp_request->addr.sa_family);
            }
        } else if (strcmp(request->resource, "/node_info") == 0) {
            sendNodeInfo(request, tcp_request->socket);
        } else if (strcmp(request->resource, "/query") == 0) {
            int query_t = queryType(request->payload);
            switch(query_t) {
                case READ:
                    distributor->distribute(request, tcp_request->socket);
                    break;
                case LOAD:
                    sendToAll(request, tcp_request->socket);
                    break;
                case WRITE:
                    distributor->sendToMaster(request, tcp_request->socket);
                    break;
                default:
                    log_err("Invalid query: %s", request->payload);
                    throw "Invalid query.";
            }
        } else if (strcmp(request->resource, "/procedure") == 0) {
            distributor->sendToMaster(request, tcp_request->socket);
        } else {
            log_err("Invalid HTTP resource: %s", request->resource);
        }
        // cleanup
        Request_free(tcp_request);
    }
}


void Dispatcher::sendNodeInfo(struct HttpRequest *request, int sock) {
    debug("Send node info.");
    char *node_info = (char *)malloc(sizeof(char) * 256 * (cluster_nodes->size()+1));
    char *current_position = node_info;
    char head[] = "{\"hosts\": [ ";     // important whitespace to have valid json for empty list
    strncpy(current_position, head, strlen(head));
    current_position += strlen(head);

    char tmp_node_info_entry[256];
    for (struct Host *host : *cluster_nodes) {
        int i = snprintf(tmp_node_info_entry, 256, "{\"ip\": \"%s\", \"total_queries\": %d, \"total_time\": %d},", host->url, host->total_queries, host->total_time);
        if (i > 256) {
            log_err("node info to large for buffer");
        }
        strncpy(current_position, tmp_node_info_entry, strlen(tmp_node_info_entry));
        current_position += strlen(tmp_node_info_entry);
    }
    strcpy(current_position-1 , "]}"); // -1 to overwrite the comma or whitespace
    printf("%s\n", node_info);
    struct HttpResponse *response = new HttpResponse;
    response->status = 200;
    response->content_length = strlen(node_info);
    response->payload = node_info;

    http_send_response(sock, response);
    free(node_info);
    free(response);
    close(sock);
}


void Dispatcher::sendToAll(struct HttpRequest *request, int sock) {
    debug("Load table.");
    char entry_template[] = "{\"host\": \"%s:%d\", \"status\": %d, \"answer\": %s},";
    char *answer = strdup("[ ");    // important whitespace to have valid json for empty list

    char *entry;
    for (struct Host *host : *cluster_nodes) {
        struct HttpResponse *response = executeRequest(host, request);
        if (response) {
            check_mem(asprintf(&entry, entry_template, host->url, host->port, response->status, response->payload));
            free(response->payload);
            free(response);
        } else {
            check_mem(asprintf(&entry, entry_template, host->url, host->port, 0, NULL));
        }
        answer = (char *)realloc(answer, (strlen(answer) + strlen(entry) + 1) * sizeof(char));   // +1 for terminating \0
        if (answer == NULL) {
            log_err("Realloc failed.");
            exit(EXIT_FAILURE);
        }
        strcpy(answer + strlen(answer), entry);
        free(entry);
    }

    strcpy(answer + strlen(answer)-1 , "]"); // -1 to overwrite the comma  or whitespace
    struct HttpResponse client_response;
    client_response.status = 200;
    client_response.content_length = strlen(answer);
    client_response.payload = answer;
    http_send_response(sock, &client_response);

    free(answer);
    close(sock);
}


Dispatcher::Dispatcher(char *port, char *settings_file) {
    this->port = port;
    cluster_nodes = new std::vector<struct Host*>;

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

    for (auto host: jsonHosts) {
        std::string url = host.get("url", "").asString();
        int port = host.get("port", "0").asInt();
        if (url != "" and port != 0) {
            const char *url_c = url.c_str();
            add_host(url_c, port);
        }
    }

    if (cluster_nodes->size() == 0) {
        debug("Settings file does not contain any valid hosts.");
    }

    thread_pool_size = v.get("threads", 1).asInt();

    std::string dispatch_algorithm = v.get("algorithm", "RoundRobin").asString();

    if (dispatch_algorithm == "Stream") {
        distributor = new StreamDistributor(cluster_nodes);
        debug("Used dispatching algorithm: Stream");
    } else {
        //SimpleRoundRobinDipatcher is the standard algorithm
        distributor = new RoundRobinDistributor(cluster_nodes);
        debug("Used dispatching algorithm: RoundRobin");
    }
}


void Dispatcher::start() {
    debug("Start dispatcher");
    // Start parser threads
    for (int i = 0; i < thread_pool_size; ++i) {
        parser_thread_pool.emplace_back(dispatch_requests_wrapper, this, i);
    }

    // create dispatcher socket
    int socket = http_create_inet_socket(port);
    debug("Dispatcher: Listening on port %s", port);

    // Disptach requests
    while(1) {
        // Allocates memory for request
        struct Request *request = new struct Request();
        request->socket = accept(socket, &(request->addr), &(request->addrlen));
        if (request->socket < 0) {
            log_err("Error: on accept.");
            throw "Error: on accept.";
        }
        debug("Main: new request.");
        {
            std::unique_lock<std::mutex> lck(request_queue_mutex);
            debug("Main: push to request_queue.");
            request_queue.push(request);
            request_queue_empty.notify_one();
        }
    }
}


void Dispatcher::shut_down() {
    debug("Shut down dispatcher");
    for (auto& th : parser_thread_pool) {
        th.join();
    }
}


void Dispatcher::add_host(const char *url, int port) {
    struct Host *h = new struct Host;
    h->port = port;
    h->url = strdup(url);
    h->total_queries = 0;
    h->total_time = 0;
    cluster_nodes_mutex.lock();
    cluster_nodes->push_back(h);
    cluster_nodes_mutex.unlock();
    debug("Adds host: %zu", cluster_nodes->size());
}
