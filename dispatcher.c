//C libraries
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <jansson.h>
#include <pthread.h>

#include "datastructures/linked_list.h"

#include "dbg.h"
#define MAXPENDING 5
#define BUFFERSIZE 65535



typedef struct host_s{
    char *ip;
    int port;
} host_s;

typedef host_s *host;

void host_free(host host) {
    if (host != NULL) {
        free(host->ip);
        free(host);
    }
};

void socket_free(int *socket_fd_r) {
    if (socket_fd_r != NULL) {
        free(socket_fd_r);
    }
};

pthread_mutex_t request_queue_lock;


int create_dispatcher_socket (const char* port) {
    int sock_fd;

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    check(getaddrinfo(NULL, port, &hints, &res) == 0, "Error getaddrinfo.");

    sock_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    check(sock_fd >= 0, "Error: Can't create socket.");

    check(bind(sock_fd, res->ai_addr, res->ai_addrlen) == 0, "Error: can't bind to socket");

    check(listen(sock_fd, MAXPENDING) == 0, "Error: can't listen to socket");

    return sock_fd;
error:
    //TODO clean up
    return -1;
}

char *strnstr_(const char *haystack, const char *needle, size_t len_haystack, size_t len_needle) {
    if (len_haystack == 0) return (char *)haystack; /* degenerate edge case */
    if (len_needle == 0) return (char *)haystack; /* degenerate edge case */
    while ((haystack = strchr(haystack, needle[0]))) {
        if (!strncmp(haystack, needle, len_needle)) return (char *)haystack;
        haystack++; }
    return NULL;
}


int get_content_lenght(const char *buf, const int size) {
    const char *hit_ptr;
    int content_length;
    hit_ptr = strcasestr(buf, "content-length:");
    if (hit_ptr == NULL) {
        return -1;
    }
    char format [50];
    strncpy(format, hit_ptr, 15);
    strcat(format," %d");
    fflush(stdout);     // TODO: why
    if (sscanf(hit_ptr, format, &content_length) != 1) {
        return -1;
    }
    return content_length;
}


void poll_requests(list request_queue) {
    debug("Parser thread started");

    while (1) {

        int not_empty_flag = 0;
        int sock;

        // get socket with request
        pthread_mutex_lock(&request_queue_lock);
        if (list_size(request_queue) != 0) {
            sock = *((int *)list_get(request_queue, 0));
            check(sock != 0,  "Error on list get.");
            check(list_del(request_queue, 0) == SUCCESS, "Error on list delete entry.");
            not_empty_flag = 1;
        }
        pthread_mutex_unlock(&request_queue_lock);
        if (!not_empty_flag) {
            continue;
        }

        char buf[BUFFERSIZE];
        memset(buf, 0, BUFFERSIZE);
        int offset = 0;
        int recv_size = 0;
        int first_line_received = 0;
        int header_received = 0;
        char *http_body_start = NULL;
        char method[16], recource[64];
        char *content = NULL;
        int length = 0;

        debug("new request handled by");

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
                if ((n = sscanf(buf, "%15s %63s HTTP/1.1", (char *)&method, (char *)&recource)) == 2) {
                    debug("HTTP Request: Method %s Recource: %s", method, recource);
                } else {
                    // TODO Error message
                    printf("ERROR scanf \n");
                    break;
                }
            }

            if (!header_received) {
                char *hit_ptr;
                hit_ptr = strnstr_(buf, "\r\n\r\n", offset, 4);
                http_body_start = hit_ptr + 4;
                if (hit_ptr == NULL) {
                    debug("Waiting for header to be completed");
                    continue;
                }
                header_received = 1;
                // header delimiter reached
                length = get_content_lenght(buf, offset);
                if (length == -1)
                {
                    printf("ERROR: Could not read content length!\n");
                    break;
                } else {
                    debug("Header Received #### Content-Length: %i", length);
                }
            }

            // complete header was received
            // check whether message is complete
            if (http_body_start != NULL) {
                if (((http_body_start - buf) + length) == offset) {
                    debug("complete message received\n header:  %ld", http_body_start-buf);
                    content = http_body_start;
                    break;
                }
            }
        }


        if (strncmp(recource, "/query/") == 0) {

        }
        else {
            if (strncmp(recource, "/procedure/") == 0) {
            }
            else {
                
            }
        }
        // if (r.getResource() == "/query/") {
        //     if (r.hasDecodedContent("query")) {
        //         std::unique_ptr<Json::Value> root (new Json::Value);
        //         if (m_reader.parse(r.getDecodedContent("query"), (*root)) == false) {
        //             std::cerr << "Error parsing json:" << m_reader.getFormattedErrorMessages() << std::endl;
        //             std::cout << r.getContent() << std::endl;
        //             std::cout << r.getDecodedContent("query") << std::endl;
        //             close(sock);
        //             return;
        //         }
        //         dispatcher->dispatchQuery(r, sock, std::move(root));
        //     } else {
        //         dispatcher->dispatch(r, sock);
        //     }
        // } else if (r.getResource() == "/procedure/") {
        //     dispatcher->dispatchProcedure(r, sock);
        // } else {
        //     dispatcher->dispatch(r, sock);
        // }
    }
    return ;
error :
    printf("error\n");
}

int read_settings(char const* settings_file, list hosts) {

    json_t *root;
    json_error_t error;

    root = json_load_file(settings_file, 0, &error);
    check(root, "JSON_error");
    
    json_t *hosts_j = json_object_get(root, "hosts");
    check(json_is_array(hosts_j) && json_array_size(hosts_j) != 0, "No or empty 'hosts' array in json.");

    size_t index;
    json_t *host_j;
    host new_host;
    json_array_foreach(hosts_j, index, host_j) {
        json_t *ip_j = json_object_get(host_j, "url");
        check(ip_j, "No url for host");
        json_t *port_j = json_object_get(host_j, "port");
        check(port_j, "No port for host");
        char *ip;

        new_host = malloc(sizeof(host_s));
        json_unpack(ip_j, "s", &ip);
        new_host->ip = strdup(ip);
        json_unpack(port_j, "i", &(new_host->port));
        printf("Add host %s:%d\n", new_host->ip, new_host->port);
        list_add(hosts, new_host);
    }

    //OPTIONAL: read other Settings (Threads)

    return 0;
error:
    return -1;
}




int main(int argc, char const *argv[]) {
    if (argc != 3) {
        printf("USAGE: ./a.out PORT HOSTS_SETTINGS\n");
        return -1;
    }

    check(pthread_mutex_init(&request_queue_lock, NULL) == 0, "Mutex init failed.");
    list request_queue = list_create((list_entryfree)&socket_free);

    list hosts = list_create((list_entryfree)&host_free);
    check(read_settings(argv[2], hosts) == 0, "while reading settings");

    int dispatcher_socket;
    check((dispatcher_socket = create_dispatcher_socket(argv[1])) != -1, "while creating dispatcher socket");


    debug("Dispatcher listening on port %s ...", argv[1]);

    socklen_t client_addrlen;
    struct sockaddr client_addr;
    int client_socket;

    client_socket = sizeof(socklen_t);

    pthread_t thread_id;
    pthread_create(&thread_id, NULL, (void *(*)(void *))poll_requests, request_queue);

    while(1) {
        client_socket = accept(dispatcher_socket, &client_addr, &client_addrlen);
        
        if (client_socket < 0) {
            printf("Error: on accept\n");
            return -1;
        }
        int *socket_fd_p = malloc(sizeof(int));
        *socket_fd_p = client_socket;
        pthread_mutex_lock(&request_queue_lock);
        list_add(request_queue, socket_fd_p);
        pthread_mutex_unlock(&request_queue_lock);
    }
 
    close(dispatcher_socket);
    
    return 0;

error:
    return -1;
}
