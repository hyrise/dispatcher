#include "Dispatcher.h"
#include "dbg.h"
#include "jsoncpp/json.h"
#include "http-parser/http_parser.h"

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

#define BUFFER_SIZE 10240


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


int query_type(char *http_payload, size_t n) {
    // http_payload is not NULL terminated!
    debug("Find out query type");
    debug("Query: %.*s", (int)n, http_payload);
    if (http_payload == NULL) {
        log_err("No payload. Expected JSON query.");
        return -1;
    }
    std::string http_payload_str(http_payload, http_payload + n);
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



struct request_parser_data {
    Dispatcher *dispatcher;
    int client_socket;
    std::vector<struct Host *> *cluster_nodes;
    std::map<std::string, int> *connections;
    std::map<int, struct http_parser *> *parsers;
    fd_set *active_fd_set;

    ssize_t buffer_offset;
    char buffer[BUFFER_SIZE];
    char *url_start;
    size_t url_length;
    char *payload_start;
    size_t payload_length;
};

struct response_parser_data {
    int client_socket;
    char buffer[BUFFER_SIZE];
};



// http-parser call backs for client requests
int request_on_url_callback(http_parser *parser, const char *at, size_t length) {
    // (may be called multiple times for a single url)
    // used for client requests to distinguish between management API (/node_info) and queries
    // no data is copied, but pointer  to start and length is stored
    debug("url_callback");

    struct request_parser_data *data = (struct request_parser_data *)parser->data;
    if (data->url_start == NULL) {
        // first call
        data->url_start = (char *)at;
        data->url_length = length;
    } else {
        // further calls
        data->url_length += length;
    }
    return 0;
}


int request_on_body_callback(http_parser *parser, const char *at, size_t length) {
    // (may be called multiple times for a single message body)
    // no data is copied, but pointer  to start and length is stored
    // buffering is needed, because we only decide for a query type for complete messages
    debug("on_body_callback");

    struct request_parser_data *data = (struct request_parser_data *)parser->data;
    if (data->payload_start == NULL) {
        // first call
        data->payload_start = (char *)at;
        data->payload_length = length;
    } else {
        // further calls
        data->payload_length += length;
    }
    return 0;
}


int request_on_message_complete_callback(http_parser *parser) {
    // called for complete client requests
    // react based on url and message body
    debug("message_complete_callback");
    int status = 200;
    const char *payload = NULL;
    char *dynamic_generated_payload = NULL;
    struct request_parser_data *data = (struct request_parser_data *)parser->data;
    Dispatcher *d = data->dispatcher;
    debug("Requested url: %.*s", (int)data->url_length, data->url_start);

    if (strncmp(data->url_start, "/query", 6) == 0) {
        int query_t = query_type(data->payload_start, data->payload_length);
        switch(query_t) {
            case READ:
                static int node_offset = 0;
                node_offset = (node_offset + 1) % data->cluster_nodes->size();
                d->send_to_db_node_async(data, node_offset);
                break;
            case LOAD:
                // Wait for DB answer in case of LOAD requests
                HttpRequest request;
                request.method = (char *)"POST";
                request.resource = (char *)"/query";
                request.version = (char *)"1.1";
                request.headers = NULL;
                request.content_length = data->payload_length;
                request.payload = strndup(data->payload_start, data->payload_length);
                HttpResponse *response;
                d->send_to_all(&request, &response);
                assert(dynamic_generated_payload == NULL);
                dynamic_generated_payload = response->payload;
                payload = dynamic_generated_payload;
                free(response->headers);
                free(response);
                free(request.payload);
                break;
            case WRITE:
                // send to master node (node_offset=0)
                d->send_to_db_node_async(data, 0);
                break;
            default:
                log_err("Invalid query: %.*s", (int)data->payload_length, data->payload_start);
                assert(query_t == -1);
                status = 400;
                payload = "Invalid query.";
        }
        // Reset data
        data->url_start = NULL;
        data->url_length = 0;
        data->payload_start = NULL;
        data->payload_length = 0;
        if (query_t == READ || query_t == WRITE) {
            // Query was sent to DB
            // Return to event loop and wait for DB response.
            return 0;
        }

    } else if (strncmp(data->url_start, "/add_node/", 10) == 0) {
        char ip[INET_ADDRSTRLEN];
        int port;
        if (get_ip_from_socket(data->client_socket, ip) == 0) {
            port = (int)strtol(data->url_start+10, (char **)NULL, 10);
            if (port == 0) {
                log_err("/add_node/ - Detecting port");
                status = 400;
                payload = "Error while setting adding db_node: unsupported format.";
            } else {
                debug("Add host:  %s:%i", ip, port);
                d->add_host(ip, port);
                status = 200;
                payload = "New host added";
            }
        } else {
            status = 500;
            payload = "Error while adding new node: Server cannot detect node IP.";
        }

    } else if (strncmp(data->url_start, "/remove_node/", 13) == 0) {
        char *delimiter = (char *)memchr(data->url_start, ':', data->url_length);
        if (delimiter != NULL) {
            char *ip = strndup(data->url_start + 13, delimiter - (data->url_start + 13));
            int port = (int)strtol(delimiter + 1, (char **)NULL, 10);
            int success = d->remove_host(ip, port);
            free(ip);
            status = 200;
            if (success == 0) {
                payload = "Host removed.";
            } else {
                payload = "Host not found.";
            }
        } else {
            status = 400;
            payload = "Error while removing node: unsupported format. Expected format [IP]:[PORT]";
        }

    } else if (strncmp(data->url_start, "/node_info", 10)  == 0) {
        d->get_node_info(&dynamic_generated_payload);
        payload = dynamic_generated_payload;

    } else if (strncmp(data->url_start, "/new_master/", 12) == 0) {
        char ip[INET_ADDRSTRLEN];
        int port;
        if (get_ip_from_socket(data->client_socket, ip) == 0) {
            port = (int)strtol(data->url_start+12, (char **)NULL, 10);
            if (port == 0) {
                log_err("/new_master/ - Detecting port");
                status = 400;
                payload = strdup("Error while setting new master: unsupported format.");
            } else {
                debug("New master:  %s:%i", ip, port);
                d->set_master(ip, port);
                status = 200;
                payload = "New master set";
            }
        } else {
            status = 500;
            payload = "Error while setting new master: Server cannot detect master IP.";
        }

    } else if (strncmp(data->url_start, "/procedure", data->url_length) == 0) {
        // send to master node (node_offset=0)
        d->send_to_db_node_async(data, 0);
        data->url_start = NULL;
        data->url_length = 0;
        data->payload_start = NULL;
        data->payload_length = 0;
        // Return to event loop and wait for DB response.
        return 0;
    } else {
        log_err("Invalid HTTP resource: %.*s", (int)data->url_length, data->url_start);
        status = 404;
        payload = "Invalid HTTP resource";
    }


    char http_response[] = "HTTP/1.1 %d %s\r\nContent-Length: %lu\r\n\r\n";
    char write_buffer[1024];
    sprintf(write_buffer, http_response, status, http_reason_phrase(status), strlen(payload));

    ssize_t send_size = send_all(data->client_socket , write_buffer, strlen(write_buffer), 0);
    if (send_size != strlen(write_buffer)) {
        log_err("send_size != data_size");
    }
    debug("SEND: '''%s'''", write_buffer);
    send_size = send_all(data->client_socket, payload, strlen(payload), 0);
    if (send_size != strlen(payload)) {
        log_err("send_size != data_size");
    }
    debug("SEND: '''%s'''", payload);

    // Reset data
    data->url_start = NULL;
    data->url_length = 0;
    data->payload_start = NULL;
    data->payload_length = 0;

    if (dynamic_generated_payload != NULL) {
        free(dynamic_generated_payload);
        dynamic_generated_payload = NULL;
    }

    if (http_should_keep_alive(parser) == 0) {
        close(data->client_socket);
    }
    return 0;
}


// http-parser call backs for database responses
int response_on_headers_complete_callback(http_parser *parser) {
    // called for database responses
    // propagate response header to client
    debug("response_on_headers_complete_callback");
    struct response_parser_data *data = (struct response_parser_data *)parser->data;

    char http_response[] = "HTTP/1.1 %d %s\r\nContent-Length: %lu\r\n\r\n";
    char write_buffer[1024];
    sprintf(write_buffer, http_response, parser->status_code, http_reason_phrase(parser->status_code),
            parser->content_length);

    size_t send_size = send_all(data->client_socket, write_buffer, strlen(write_buffer), 0);
    if (send_size != strlen(write_buffer)) {
        log_err("send_size != data_size");
    }
    debug("SEND to fd %d: '''%s'''", data->client_socket, write_buffer);

    return 0;
}


int response_on_body_callback(http_parser *parser, const char *at, size_t length) {
    // (may be called multiple times for a single response body)
    // propagate message back to client
    // TODO: this does not work for pipelined requests, if read() returns incomplete DB responses
    debug("response_on_body_callback");

    struct response_parser_data *data = (struct response_parser_data *)parser->data;

    size_t send_size = send_all(data->client_socket, at, length, 0);
    if (send_size != length) {
        log_err("send_size != data_size");
    }
    debug("SEND to fd %d: '''%.*s'''(%lu)", data->client_socket, (int)length, at, length);
    return 0;
}




// Parameter for pthread_create()
struct thread_arg {
    Dispatcher *dispatcher;
    int new_socket;
};

void *dispatch_handle_connection_wrapper(void *arg) {
    struct thread_arg *ta = (struct thread_arg *)arg;
    Dispatcher *dispatcher = ta->dispatcher;
    int client_socket = ta->new_socket;
    free(arg);
    debug("New thread created: for fd %d", client_socket);
    dispatcher->handle_connection(client_socket);
    debug("Thread terminated: for fd %d", client_socket);
    return NULL;
}


void Dispatcher::handle_connection(int client_socket) {

    fd_set active_fd_set, read_fd_set;
    /* Initialize the set of active sockets. */
    FD_ZERO (&active_fd_set);
    FD_SET (client_socket, &active_fd_set);

    // create new connection and parser containers
    std::map<std::string, int> connections;
    std::map<int, struct http_parser *> parsers;


    // http-parser callback settings
    http_parser_settings settings, response_settings;
    memset(&settings, 0, sizeof(settings));
    memset(&response_settings, 0, sizeof(response_settings));
    settings.on_url = request_on_url_callback;
    settings.on_body = request_on_body_callback;
    settings.on_message_complete = request_on_message_complete_callback;

    response_settings.on_headers_complete = response_on_headers_complete_callback;
    response_settings.on_body = response_on_body_callback;



    http_parser *new_request_parser = (http_parser *)malloc(sizeof(http_parser));
    http_parser_init(new_request_parser, HTTP_REQUEST);

    struct request_parser_data *request_data = (struct request_parser_data *)malloc(sizeof(struct request_parser_data));
    request_data->dispatcher = this;
    request_data->client_socket = client_socket;
    request_data->cluster_nodes = cluster_nodes;
    request_data->connections = &connections;
    request_data->parsers = &parsers;
    request_data->active_fd_set = &active_fd_set;

    request_data->buffer_offset = 0;
    request_data->url_start = NULL;
    request_data->payload_start = NULL;

    new_request_parser->data = request_data;

    while (TRUE) {
        read_fd_set = active_fd_set;
        debug("select");
        if (select (FD_SETSIZE, &read_fd_set, NULL, NULL, NULL) < 0) {
            perror ("select");
            exit(EXIT_FAILURE);
        }

        /* Service all the sockets with input pending. */
        int i;

        for (i = 0; i < FD_SETSIZE; ++i) {
            if (FD_ISSET (i, &read_fd_set)) {
                if (i == client_socket) {
                    /* new data on client socket. */
                    debug("New data on client socket.");

                    ssize_t data_size;
                    data_size = read(client_socket, request_data->buffer + request_data->buffer_offset, BUFFER_SIZE - request_data->buffer_offset);

                    if (request_data->buffer_offset == 0 && data_size == BUFFER_SIZE) {
                        log_err("Buffer to small.");
                        exit(EXIT_FAILURE);
                    }

                    if (data_size == 0) {
                        log_err("Connection closed by client");
                        close(client_socket);
                        FD_CLR (client_socket, &active_fd_set);
                        free(request_data);
                        free(new_request_parser);

                        for (auto it = request_data->parsers->begin(); it != request_data->parsers->end(); it++ ) {
                            debug("Close DB connection fd %d", it->first);
                            close(it->first);
                            free(it->second->data);
                            free(it->second);
                        }

                        // TODO free memory of response parsers, data
                        // TODO: Close DB sockets
                        // TODO: Think about: send outstanding responses.
                        return;
                    }
                    if (data_size < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // ignor
                        } else {
                            log_err("Error recv");
                            // TODO
                            exit(EXIT_FAILURE);
                        }
                    }
                    size_t nparsed = http_parser_execute(new_request_parser, &settings, request_data->buffer + request_data->buffer_offset, data_size);
                    if (nparsed != data_size) {
                        log_err("%s\n", http_errno_name(static_cast<enum http_errno>(new_request_parser->http_errno)));
                        log_err("%s\n", http_errno_description(static_cast<enum http_errno>(new_request_parser->http_errno)));
                        break;
                    }

                    if (data_size == (BUFFER_SIZE - request_data->buffer_offset)) {
                        // end of recv buffer
                        // copy data to begin of buffer
                        if (request_data->url_start != NULL) {
                            strncpy(request_data->buffer, request_data->url_start, request_data->url_length);
                            request_data->url_start = request_data->buffer;
                            if (request_data->payload_start != NULL) {
                                strncpy(request_data->buffer + request_data->url_length, request_data->payload_start, request_data->payload_length);
                                request_data->payload_start = request_data->buffer + request_data->url_length;
                            }
                        }
                        request_data->buffer_offset = request_data->url_length + request_data->payload_length;
                    } else {
                        request_data->buffer_offset += data_size;
                    }

                } else {
                    // new response data on db socket
                    debug("New data on db socket fd %d.", i);
                    struct http_parser *db_response_parser = parsers[i];
                    ssize_t data_size;
                    struct response_parser_data *response_data = (struct response_parser_data *)db_response_parser->data;
                    data_size = read(i, response_data->buffer, BUFFER_SIZE);

                    if (data_size == 0) {
                        log_err("Connection closed by database");
                        close(i);
                        // TODO free memory
                        // TODO: Close DB sockets in case clients wants to close the connection
                        // free(parsers[i]->data);
                        // free(parsers[i]);
                        exit(EXIT_FAILURE);
                    }
                    if (data_size < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // ignor
                        } else {
                            log_err("Error recv");
                            // TODO
                            exit(EXIT_FAILURE);
                        }
                    }
                    debug("Received '''%.*s'''(%lu)", (int)data_size, response_data->buffer, data_size);

                    size_t nparsed = http_parser_execute(db_response_parser, &response_settings, response_data->buffer, data_size);
                    if (nparsed != data_size) {
                        log_err("%s\n", http_errno_name(static_cast<enum http_errno>(db_response_parser->http_errno)));
                        log_err("%s\n", http_errno_description(static_cast<enum http_errno>(db_response_parser->http_errno)));
                        break;
                    }
                }
            }
        }
    }

}


void Dispatcher::get_node_info(char **dynamic_generated_payload) {
    debug("Get node info.");
    assert(*dynamic_generated_payload == NULL);
    char *node_info = (char *)malloc(sizeof(char) * 256 * (cluster_nodes->size()+1));
    check_mem(node_info);
    char *current_position = node_info;
    char head[] = "{\"hosts\": [ ";     // important whitespace to have valid json for empty list
    strncpy(current_position, head, strlen(head));
    current_position += strlen(head);

    char tmp_node_info_entry[256];
    for (struct Host *host : *cluster_nodes) {
        int i = snprintf(tmp_node_info_entry, 256, "{\"host\": \"%s:%d\", \"total_queries\": %d, \"total_time\": %d},", host->url, host->port, host->total_queries, host->total_time);
        if (i > 256) { // TODO
            log_err("node info to large for buffer");
            exit(EXIT_FAILURE);
        }
        strncpy(current_position, tmp_node_info_entry, strlen(tmp_node_info_entry));
        current_position += strlen(tmp_node_info_entry);
    }
    strcpy(current_position-1 , "]}"); // -1 to overwrite the comma or whitespace
    debug("%s\n", node_info);

    *dynamic_generated_payload = node_info;
}


void Dispatcher::send_to_db_node_async(struct request_parser_data *data, int node_offset) {
    int db_socket;
    Host *h = (*data->cluster_nodes)[node_offset];

    std::string db_node = std::string(h->url) + ":" + std::to_string(h->port);

    auto iter = data->connections->find(db_node);
    if (iter == data->connections->end()) {
        db_socket = (*data->connections)[db_node] = http_open_connection(h->url, h->port);
        if (db_socket == -1) {
            exit(EXIT_FAILURE);
        }

        FD_SET (db_socket, data->active_fd_set);


        http_parser *new_response_parser = (http_parser *)malloc(sizeof(http_parser));
        http_parser_init(new_response_parser, HTTP_RESPONSE);

        struct response_parser_data *response_data = (struct response_parser_data *)malloc(sizeof(struct response_parser_data));
        response_data->client_socket = data->client_socket;

        new_response_parser->data = response_data;
        (*data->parsers)[db_socket] = new_response_parser;
        debug("New connection: fd=%d", db_socket);

    } else {
        db_socket = iter->second;
    }

    h->total_queries += 1;


    char http_request[] = "POST %.*s HTTP/1.1\r\nContent-Length: %lu\r\n\r\n";
    char write_buffer[1024];
    sprintf(write_buffer, http_request, data->url_length, data->url_start, data->payload_length);

    ssize_t send_size = send_all(db_socket , write_buffer, strlen(write_buffer), 0);
    if (send_size != strlen(write_buffer)) {
        log_err("send_size != data_size");
    }
    send_size = send_all(db_socket, data->payload_start, data->payload_length, 0);
    if (send_size != data->payload_length) {
        log_err("send_size != data_size");
    }
    debug("SEND: '''%.*s'''(%lu)", (int)send_size, data->payload_start, send_size);

}


void Dispatcher::send_to_all(struct HttpRequest *request, struct HttpResponse **response_ref) {
    debug("Load table.");
    char entry_template[] = "{\"host\": \"%s:%d\", \"status\": %d, \"answer\": %s},";
    char *answer = strdup("[ ");    // important whitespace to have valid json for empty list

    char *entry;
    for (struct Host *host : *cluster_nodes) {
        struct HttpResponse *response = http_execute_request(host, request);
        if (response) {
            if (asprintf(&entry, entry_template, host->url, host->port, response->status, response->payload) == -1) {
                log_err("asprintf() failed");
                exit(EXIT_FAILURE);
            };
            HttpResponse_free(response);
        } else {
            if (asprintf(&entry, entry_template, host->url, host->port, 0, NULL) == -1) {
                log_err("asprintf() failed");
                exit(EXIT_FAILURE);
            };
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
            exit(EXIT_FAILURE);
        }
        if (pthread_detach(thread) != 0) {
            log_err("pthread_detach failed");
            exit(EXIT_FAILURE);
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


int Dispatcher::remove_host(const char *url, int port) {
    debug("Try to remove host: %s:%d", url, port);
    size_t i;
    int success = -1;
    cluster_nodes_mutex.lock();
    for (i=0; i<cluster_nodes->size(); i++) {
        if (strcmp(cluster_nodes->at(i)->url, url) == 0 && cluster_nodes->at(i)->port == port) {
            free(cluster_nodes->at(i)->url);
            free(cluster_nodes->at(i));
            cluster_nodes->erase(cluster_nodes->begin() + i);
            success = 0;
            break;
        }
    }
    cluster_nodes_mutex.unlock();
    return success;
}

