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


int get_ip_from_socket(int sockfd, char* ipstr) {
    debug("get_ip_from_socket");
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


std::string url_decode(std::string &SRC) {
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


int query_type(char *http_payload) {
    debug("Find out query type");
    if (http_payload == NULL) {
        log_err("No payload. Expected JSON query.");
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
        content.emplace(key, url_decode(value));
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


// Parameter for pthread_create()
struct thread_arg {
    Dispatcher *dispatcher;
    int new_socket;
};

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


void Dispatcher::handle_connection(int client_socket) {
    struct HttpRequest *request;
    struct HttpResponse *response;
    std::map<std::string, int> connections;
    static int node_offset;

    while (TRUE) {
        // Allocates memory for the request
        int http_error = http_receive_request(client_socket, &request);
        if (http_error != HTTP_SUCCESS) {
            assert(request == NULL);
            // TODO send error msg to client in Case of wrong request
            for (auto it = connections.begin(); it != connections.end(); it++ ) {
                close(it->second);
            }

            close(client_socket);
            return;
        }

        if (strncmp(request->resource, "/add_node/", 10) == 0) {
            char ip[INET_ADDRSTRLEN];
            int port;
            response = new HttpResponse;
            if (get_ip_from_socket(client_socket, ip) == 0) {
                port = (int)strtol(request->resource+10, (char **)NULL, 10);
                if (port == 0) {
                    log_err("/add_node/ - Detecting port");
                    response->status = 400;
                    response->payload = strdup("Error while setting adding db_node: unsupported format.");
                } else {
                    debug("Add host:  %s:%i", ip, port);
                    add_host(ip, port);
                    response->status = 200;
                    response->payload = strdup("New host added");
                }
            } else {
                response->status = 500;
                response->payload = strdup("Error while adding new node: Server cannot detect node IP.");
            }
            response->headers = NULL;
            response->content_length = strlen(response->payload);

        } else if (strncmp(request->resource, "/remove_node/", 13) == 0) {
            char *delimiter = strchr(request->resource, ':');
            response = new HttpResponse;
            if (delimiter != NULL) {
                char *ip = strndup(request->resource + 13, delimiter - (request->resource + 13));
                int port = 0;
                remove_host(ip, port);
                free(ip);
                response->status = 200;
                response->payload = strdup("Host removed");
            } else {
                response->status = 400;
                response->payload = strdup("Error while setting new master: unsupported format.");
            }
            response->headers = NULL;
            response->content_length = strlen(response->payload);

        } else if (strcmp(request->resource, "/node_info") == 0) {
            get_node_info(request, &response);

        } else if (strncmp(request->resource, "/new_master/", 12) == 0) {
            char ip[INET_ADDRSTRLEN];
            int port;
            response = new HttpResponse;
            if (get_ip_from_socket(client_socket, ip) == 0) {
                port = (int)strtol(request->resource+12, (char **)NULL, 10);
                if (port == 0) {
                    log_err("/new_master/ - Detecting port");
                    response->status = 400;
                    response->payload = strdup("Error while setting new master: unsupported format.");
                } else {
                    debug("New master:  %s:%i", ip, port);
                    set_master(ip, port);
                    response->status = 200;
                    response->payload = strdup("New master set");
                }
            } else {
                response->status = 500;
                response->payload = strdup("Error while setting new master: Server cannot detect master IP.");
            }
            response->headers = NULL;
            response->content_length = strlen(response->payload);

        } else if (strncmp(request->resource, "/devzero/", sizeof("/devzero/") - 1) == 0) {
            node_offset = (node_offset + 1) % cluster_nodes->size();
            send_to_db_node(request, &connections, node_offset, &response);

        } else if (strncmp(request->resource, "/query", 6) == 0) {
            int query_t = query_type(request->payload);
            switch(query_t) {
                case READ:
                    node_offset = (node_offset + 1) % cluster_nodes->size();
                    send_to_db_node(request, &connections, node_offset, &response);
                    break;
                case LOAD:
                    send_to_all(request, &response);
                    break;
                case WRITE:
                    // send to master node (node_offset=0)
                    send_to_db_node(request, &connections, 0, &response);
                    break;
                default:
                    log_err("Invalid query: %s", request->payload);

                    response = new HttpResponse;
                    response->status = 500;
                    response->payload = strdup("Invalid query");
                    response->content_length = strlen(response->payload);
            }
        } else if (strcmp(request->resource, "/procedure") == 0) {
            // send to master node (node_offset=0)
            send_to_db_node(request, &connections, 0, &response);
        } else {
            log_err("Invalid HTTP resource: %s", request->resource);

            response = new HttpResponse;
            response->status = 404;
            if (asprintf(&response->payload, "Invalid HTTP resource: %s", request->resource) == -1) {
                response->payload = strdup("Invalid HTTP resource");
            }
            response->headers = NULL;
            response->content_length = strlen(response->payload);
        }

        debug("Send response to client.");
        http_send_response(client_socket, response);
        HttpResponse_free(response);
        response = NULL;

        if (HttpRequest_persistent_connection(request) == TRUE) {
            HttpRequest_free(request);
            continue;
        }

        HttpRequest_free(request);
        debug("Close client socket with fd %d", client_socket);
        close(client_socket);
        break;
    }
    // TODO: Close DB sockets in case clients wants to close the connection
}


void Dispatcher::get_node_info(struct HttpRequest *request, struct HttpResponse **response_ref) {
    debug("Get node info.");
    char *node_info = (char *)malloc(sizeof(char) * 256 * (cluster_nodes->size()+1));
    check_mem(node_info);
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
    response->headers = NULL;
    response->payload = node_info;
    *response_ref = response;
}


void Dispatcher::send_to_db_node(struct HttpRequest *request, std::map<std::string, int> *connections,
                                 int node_offset, struct HttpResponse **response_ref) {
    int db_socket;
    Host *h = (*cluster_nodes)[node_offset];

    std::string db_node = std::string(h->url) + ":" + std::to_string(h->port);

    auto iter = connections->find(db_node);
    if (iter == connections->end()) {
        db_socket = (*connections)[db_node] = http_open_connection(h->url, h->port);
    } else {
        db_socket = iter->second;
    }

    // TODO: proper error handling (try to open new connection ...)
    if (http_send_request(db_socket, request) != 0) {
        log_err("Error while sending request to database.");
        return;
    }

    int http_error = http_receive_response(db_socket, response_ref);
    if (http_error != HTTP_SUCCESS) {
        log_err("http error: http_receive_response");
        assert(*response_ref == NULL);

        // Error on db_request -> We have to build the response object
        HttpResponse *response = (struct HttpResponse *)malloc(sizeof(struct HttpResponse));
        check_mem(response);
        response->status = 500;
        response->headers = NULL;
        response->payload = strdup("Database request was not successful.");
        response->content_length = strlen(response->payload);
        *response_ref = response;
    }
}


void Dispatcher::send_to_all(struct HttpRequest *request, struct HttpResponse **response_ref) {
    debug("Load table.");
    char entry_template[] = "{\"host\": \"%s:%d\", \"status\": %d, \"answer\": %s},";
    char *answer = strdup("[ ");    // important whitespace to have valid json for empty list

    char *entry;
    for (struct Host *host : *cluster_nodes) {
        struct HttpResponse *response = http_execute_request(host, request);
        if (response) {
            // TODO: FIX: asprintf returns -1 on error;
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
    struct HttpResponse *client_response = new HttpResponse;
    client_response->status = 200;
    client_response->content_length = strlen(answer);
    client_response->headers = NULL;
    client_response->payload = answer;
    *response_ref = client_response;
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
        debug("Main: new connection.");
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
    // TODO: Why is the port not used??
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

