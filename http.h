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

struct HttpRequest *HttpRequestFromEndpoint(int sock);
struct HttpResponse *executeRequest(struct Host *host, struct HttpRequest *request);
void sendResponse(struct HttpResponse *response, int sock);
void HttpRequest_free(struct HttpRequest *request);

#endif
