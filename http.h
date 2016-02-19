#ifndef HTTP_H_
#define HTTP_H_

#include <stdlib.h>

#define BUFFERSIZE 65535

#define TRUE 1
#define FALSE 0


struct Host {
    char *url;
    int port;
    unsigned int total_queries;
    unsigned int total_time;
};


struct HttpRequest {
    char *method;
    char *resource;
    size_t content_length;
    char *payload;
};

struct HttpResponse {
    int status;
    size_t content_length;
    char *payload;
};

struct HttpRequest *http_receive_request(int sock);
struct HttpResponse *executeRequest(struct Host *host, struct HttpRequest *request);
int http_send_request(int sockfd, struct HttpRequest *request);
void http_send_response(int sockfd, struct HttpResponse *response);
void HttpRequest_free(struct HttpRequest *request);

#endif
