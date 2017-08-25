#include "Dispatcher.h"
#include "dbg.h"
#include "jsoncpp/json.h"

extern "C"
{
#include "http.h"
}

#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <pthread.h>

#define MAXPENDING 10

struct thread_arg {
    Dispatcher *dispatcher;
    int new_socket;
};


int getIpFromSocket(int sockfd, char* ipstr) {
    debug("getIpFromSocket");
    // TODO: IPv6 Support
    struct sockaddr addr;
    socklen_t addrlen = sizeof(struct sockaddr);
    if (getpeername(sockfd, &addr, &addrlen) == 0) {
        if (addr.sa_family == AF_INET || addr.sa_family == AF_UNSPEC) {
            struct sockaddr_in *sa = (struct sockaddr_in *)&addr;
            if (inet_ntop(AF_INET, &(sa->sin_addr), ipstr, INET_ADDRSTRLEN) == NULL) {
                log_err("/add_node/ - Converting network address to string");
                return -1;
            }
        } else {
            log_err("Cannot add host: Unsupported Address family %d", addr.sa_family);
            return -1;
        }
    } else {
        log_err("Error on getpeername");
    }
    return 0;
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
        debug("HTTP payload was %s", http_payload);
        return -1;
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


void *dispatch_handle_connection_wrapper(void *arg) {
    //TODO detatch thread
    struct thread_arg *ta = (struct thread_arg *)arg;
    Dispatcher *dispatcher = ta->dispatcher;
    int client_socket = ta->new_socket;
    free(arg);
    debug("New thread created: for fd %d", client_socket);
    dispatcher->handle_connection(client_socket);

    return NULL;
}

void Dispatcher::send_to_next_node(struct HttpRequest *request, int client_socket) {
    static int i = 0;
    i = (i + 1) % cluster_nodes->size();
    struct HttpResponse *response = NULL;
    response = executeRequest((*cluster_nodes)[i], request);

    if (response == NULL) {
        response = (struct HttpResponse *)malloc(sizeof(struct HttpResponse));
        check_mem(response);
        response->status = 500;
        response->headers = NULL;
        response->payload = strdup("Database request was not successful.");
        response->content_length = strlen(response->payload);
    }

    debug("Response to client.");
    http_send_response(client_socket, response);
    HttpResponse_free(response);
}


void Dispatcher::send_to_master(struct HttpRequest *request, int client_socket) {
    struct HttpResponse *response = NULL;
    response = executeRequest((*cluster_nodes)[0], request);

    if (response == NULL) {
        response = (struct HttpResponse *)malloc(sizeof(struct HttpResponse));
        check_mem(response);
        response->status = 500;
        response->headers = NULL;
        response->payload = strdup("Database request was not successful.");
        response->content_length = strlen(response->payload);
    }

    debug("Response to client.");
    http_send_response(client_socket, response);
    HttpResponse_free(response);
}

void Dispatcher::handle_connection(int client_socket) {
     // Allocates memory for the request
    struct HttpRequest *request;
    int http_error = http_receive_request(client_socket, &request);
    if (http_error != HTTP_SUCCESS) {
        debug("Invalid Http request.");
        assert(request == NULL);
        // TODO send error msg to client in Case of wrong request
        close(client_socket);
        return;
    }
    debug("Request payload: %s", request->payload);

    if (strncmp(request->resource, "/add_node/", 10) == 0) {
        char ip[INET_ADDRSTRLEN];
        int port;
        if (getIpFromSocket(client_socket, ip) == 0) {
            port = (int)strtol(request->resource+10, (char **)NULL, 10);
            if (port == 0) {
                log_err("/add_node/ - Detecting port");
            } else {
                debug("Add host:  %s:%i", ip, port);
                add_host(ip, port);
            }
        }
        HttpRequest_free(request);
        // TODO: Send response

    } else if (strncmp(request->resource, "/remove_node/", 13) == 0) {
        char *delimiter = strchr(request->resource, ':');
        if (delimiter != NULL) {
            char *ip = strndup(request->resource + 13, delimiter - (request->resource + 13));
            int port = 0;
            remove_host(ip, port);
            free(ip);
        }
        HttpRequest_free(request);
        // TODO: Send response

    } else if (strcmp(request->resource, "/node_info") == 0) {
        sendNodeInfo(request, client_socket);
        HttpRequest_free(request);

    } else if (strncmp(request->resource, "/new_master/", 12) == 0) {
        char ip[INET_ADDRSTRLEN];
        int port;
        if (getIpFromSocket(client_socket, ip) == 0) {
            port = (int)strtol(request->resource+12, (char **)NULL, 10);
            if (port == 0) {
                log_err("/new_master/ - Detecting port");
            } else {
                debug("New master:  %s:%i", ip, port);
                set_master(ip, port);
            }
        }
        HttpRequest_free(request);

    } else if (strcmp(request->resource, "/query") == 0) {
        int query_t = queryType(request->payload);
        switch(query_t) {
            case READ:
                // TODO
                send_to_next_node(request, client_socket);
                break;
            case LOAD:
                send_to_all(request, client_socket);
                break;
            case WRITE:
                send_to_master(request, client_socket);
                break;
            default:
                log_err("Invalid query: %s", request->payload);
                HttpRequest_free(request);

                struct HttpResponse *response = new HttpResponse;
                response->status = 500;
                response->payload = strdup("Invalid query");
                response->content_length = strlen(response->payload);
                http_send_response(client_socket, response);
                free(response);
        }
    } else if (strcmp(request->resource, "/procedure") == 0) {
        send_to_master(request, client_socket);
    } else {
        log_err("Invalid HTTP resource: %s", request->resource);

        struct HttpResponse *response = new HttpResponse;
        response->status = 404;
        if (asprintf(&response->payload, "Invalid HTTP resource: %s", request->resource) == -1) {
            response->payload = strdup("Invalid HTTP resource");
        }
        response->content_length = strlen(response->payload);
        http_send_response(client_socket, response);
        HttpRequest_free(request);
        free(response);
    }
    debug("Close client socket with fd %d", client_socket);
    close(client_socket);
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
        if (i > 256) { // TODO
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
}

void Dispatcher::send_to_all(struct HttpRequest *request, int sock) {
    debug("Load table.");
    char entry_template[] = "{\"host\": \"%s:%d\", \"status\": %d, \"answer\": %s},";
    char *answer = strdup("[ ");    // important whitespace to have valid json for empty list

    char *entry;
    for (struct Host *host : *cluster_nodes) {
        struct HttpResponse *response = executeRequest(host, request);
        if (response) {
            check_mem(asprintf(&entry, entry_template, host->url, host->port, response->status, response->payload));
            HttpResponse_free(response);
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

    // if (cluster_nodes->size() == 0) {
    //     debug("Settings file does not contain any valid hosts.");
    // }
}


void Dispatcher::start() {
    signal(SIGPIPE, SIG_IGN);
    debug("Start dispatcher");

    // create dispatcher socket
    int socket = http_create_inet_socket(port);
    debug("Dispatcher: Listening on port %s", port);

    // Disptach requests
    while(1) {
        int new_socket = accept(socket, NULL, NULL);
        if (new_socket < 0) {
            log_err("Error: on accept.");
            throw "Error: on accept.";
        }
        debug("Main: new request.");
        pthread_t thread;
        struct thread_arg *ta = (struct thread_arg *)malloc(sizeof(struct thread_arg));
        ta->dispatcher = this;
        ta->new_socket = new_socket;
        if (pthread_create(&thread, NULL, &dispatch_handle_connection_wrapper, (void *)ta) != 0) {
            log_err("pthread_create failed");
            exit(-1);
        }
    }
}


void Dispatcher::set_master(const char *url, int port) {
    // TODO: Do not reset queries and time
    remove_host(url, port);

    cluster_nodes_mutex.lock();
    struct Host *old_master = cluster_nodes->front();
    free(old_master->url);
    free(old_master);

    struct Host *h = new struct Host;
    h->port = port;
    h->url = strdup(url);
    h->total_queries = 0;
    h->total_time = 0;
    cluster_nodes->at(0) = h;

    cluster_nodes_mutex.unlock();
    debug("New master: %s:%d\n", h->url, h->port);
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


void Dispatcher::remove_host(const char *url, int port) {
    debug("Try to remove host: %s", url);
    size_t i;
    cluster_nodes_mutex.lock();
    for (i=0; i<cluster_nodes->size(); i++) {
        if (strcmp(cluster_nodes->at(i)->url, url) == 0) {
            free(cluster_nodes->at(i)->url);
            free(cluster_nodes->at(i));
            cluster_nodes->erase(cluster_nodes->begin() + i);
            debug("Removed host: %zu", cluster_nodes->size());
            break;
        }
    }
    cluster_nodes_mutex.unlock();
}

