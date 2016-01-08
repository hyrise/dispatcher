#ifndef HTTP_H_
#define HTTP_H_

#include <stdlib.h>

#define BUFFERSIZE 65535

#define TRUE 1
#define FALSE 0


// struct Host {
//     char *url;
//     int port;
// };

struct HttpRequest {
    char *method;
    char *resource;
    size_t content_length;
    char *payload;
};

// struct HttpResponse {
//     char *status;
//     size_t content_length;
//     char *payload;
// };

struct HttpRequest *HttpRequestFromEndpoint(int sock);

#endif
