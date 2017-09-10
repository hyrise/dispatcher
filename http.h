#ifndef HTTP_H_
#define HTTP_H_

#include <stdlib.h>
#include <sys/types.h>
#include <sys/uio.h>
#include "dict.h"


#define TRUE 1
#define FALSE 0

#define HTTP_SUCCESS 0
#define ERR_EOF -2
#define ERR_BROKEN_PIPE -3
#define ERR_CONNECTION_RESET -4



struct Host {
    char *url;
    int port;
    unsigned int total_queries;
    unsigned int total_time;
};


struct HttpRequest {
    char *method;
    char *resource;
    char *version;
    struct dict *headers;
    size_t content_length;
    char *payload;
};

struct HttpResponse {
    int status;
    struct dict *headers;
    size_t content_length;
    char *payload;
};

ssize_t send_all(int socket, const void *buffer, size_t length, int flags);
ssize_t read_all(int socket, void *buffer, size_t length);

int http_create_inet_socket(const char *port);
int http_open_connection(const char *url, int port);
int http_receive_request(int sockfd, struct HttpRequest **request_ref);
int http_receive_response(int sockfd, struct HttpResponse **response_ref);
struct HttpResponse *http_execute_request(struct Host *host, struct HttpRequest *request);
int http_send_request(int sockfd, struct HttpRequest *request);
int http_send_response(int sockfd, struct HttpResponse *response);
void HttpRequest_free(struct HttpRequest *request);
void HttpRequest_print(struct HttpRequest *request);
int HttpRequest_persistent_connection(struct HttpRequest *request);
void HttpResponse_free(struct HttpResponse *response);
void HttpResponse_print(struct HttpResponse *response);


#endif
