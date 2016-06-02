#include "http.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include "dbg.h"

#define MAXPENDING 20


int http_open_connection(const char *url, int port) {
    int sock;
    struct sockaddr_in dest;

    if ( (sock = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
        log_err("ERROR: could not create a socket.");
        return -1;
    }

    //---Initialize server address/port struct
    bzero(&dest, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = inet_addr(url);
    dest.sin_port = htons(port);

    //---Connect to server
    if ( connect(sock, (struct sockaddr*)&dest, sizeof(dest)) != 0 ) {
        log_err("ERROR: could not connect to host.");
        return -1;
    }

    return sock;
}


int http_create_inet_socket(const char *port) {
    int sock_fd, s;

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((s = getaddrinfo(NULL, port, &hints, &res)) != 0) {
        log_err("Error getaddrinfo: %s", gai_strerror(s));
        exit(-1);
    }

    if ((sock_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0) {
        log_err("Error: Can't create socket.");
        exit(-1);
    }

    if (bind(sock_fd, res->ai_addr, res->ai_addrlen) < 0) {
        close(sock_fd);
        log_err("Error: can't bind to socket.");
        exit(-1);
    }

    if (listen(sock_fd, MAXPENDING) < 0) {
        log_err("Error: can't listen to socket.");
        exit(-1);
    }
    freeaddrinfo(res);
    return sock_fd;
}


int http_read_line(int sockfd, char **line) {
    *line  = NULL;
    char *buf = (char *)malloc(sizeof(char) * BUFFERSIZE);
    check_mem(buf);
    size_t buf_offset = 0;  // total received bytes
    int got_cr = 0;

    size_t recv_size = 0;  // total received bytes
    while ((recv_size = read(sockfd, buf+buf_offset, 1)) == 1) {
        if (buf[buf_offset] == '\r') {
            got_cr = 1;
        }
        else {
            if (buf[buf_offset] == '\n' && got_cr) {
                // reached end of line
                *line = strndup(buf, buf_offset - 1);
                break;
            }
            else {
                got_cr = 0;
            }
        }
        buf_offset += recv_size;
    }//TODO
    free(buf);
    if (*line != NULL) {
        debug("Receive line: %s", *line);
        return 0;
    }
    log_err("Received no crlf indicating end of line.");
    return -1;
}


int http_parse_request_line(char *line, char **method, char **resource) {
    *method = NULL;
    *resource = NULL;
    size_t method_end = 0;
    size_t resource_end = 0;

    size_t i;
    for (i = 0; i < strlen(line); ++i) {
        if (line[i] == ' ') {
            if (method_end == 0) {
                // end of http method
                method_end = i;
                continue;
            }
            if (resource_end == 0) {
                resource_end = i;
                break;
            }
        }
    }
    if (resource_end == 0) {
        log_err("Invalid Request line: %s", line);
        return -1;
    }
    *method = strndup(line, method_end);
    *resource = strndup(line + method_end + 1, resource_end - (method_end + 1));
    return 0;
}


int http_parse_response_line(char *line, int *status) {
    *status = 0;
    size_t status_start = 0;
    size_t status_end = 0;

    size_t i;
    for (i = 0; i < strlen(line); ++i) {
        if (line[i] == ' ') {
            if (status_start == 0) {
                // start of http status
                status_start = i + 1;
                continue;
            }
            if (status_end == 0) {
                status_end = i;
                break;
            }
        }
    }
    if (status_end == 0) {
        log_err("Invalid response status line: %s", line);
        return -1;
    }
    if (sscanf(line + status_start, "%d ", status) != 1) {
        return -1;
    }
    return 0;
}


int http_parse_header_line(char *line, char **field_name, char **field_value) {
    *field_name = NULL;
    *field_value = NULL;
    size_t field_name_end = 0;

    size_t i;
    for (i = 0; i < strlen(line); ++i) {
        if (line[i] == ':') {
            field_name_end = i;
            break;
        }
    }
    if (field_name_end == 0) {
        log_err("Invalid header line: %s", line);
        return -1;
    }
    *field_name = strndup(line, field_name_end);
    *field_value = strndup(line + field_name_end + 1, strlen(line) - (field_name_end + 1));
    return 0;
}

struct HttpRequest *http_receive_request(int sockfd) {
    char *line = NULL;
    char *method = NULL;
    char *resource = NULL;
    int content_length = -1;
    char *payload = NULL;
    struct HttpRequest *request = NULL;

    if (http_read_line(sockfd, &line) == -1) {
        goto error;
    }
    if (http_parse_request_line(line, &method, &resource) == -1) {
        goto error;
    }
    free(line);

    while (TRUE) {
        char *field_name;
        char *field_value;
        if (http_read_line(sockfd, &line) == -1) {
            goto error;
        }
        if (strcmp(line, "") == 0) {
            debug("End of Http header");
            free(line);
            break;
        }
        if (http_parse_header_line(line, &field_name, &field_value) == -1) {
            goto error;
        }
        free(line);
        if (strcasecmp(field_name, "Content-Length") == 0) {
            content_length = atoi(field_value);
        }
    }

    if (content_length == -1 || content_length == 0) {
        debug("No or 0 content-length.");
        content_length = 0;
    }
    else {
        ssize_t recv_size = 0;
        size_t payload_offset = 0;
        payload = (char *)calloc(sizeof(char), content_length + 1);
        check_mem(payload);
        while ((recv_size = read(sockfd, payload + payload_offset, content_length-payload_offset)) > 0) {
            payload_offset += recv_size;
        }
        if (recv_size == 0) {
            debug("End of TCP stream.");
        } else if (recv_size == -1) {
            log_err("Error while reading TCP stream.");
            goto error;
        }
    }

    // create and fill return object
    request = (struct HttpRequest *)malloc(sizeof(struct HttpRequest));
    check_mem(request);
    request->method = method;
    request->resource = resource;
    request->content_length = content_length;
    request->payload = payload;

    return request;

error:
    free(method);
    free(resource);
    free(payload);
    free(line);
    return NULL;
}


struct HttpResponse *HttpResponseFromEndpoint(int sockfd) {
    debug("HttpResponseFromEndpoint");
    char *line = NULL;
    int status = 0;
    int content_length = -1;
    char *payload = NULL;
    struct HttpResponse *response = NULL;

    if (http_read_line(sockfd, &line) == -1) {
        goto error;
    }
    if (http_parse_response_line(line, &status) == -1) {
        goto error;
    }
    free(line);

    while (TRUE) {
        char *field_name;
        char *field_value;
        if (http_read_line(sockfd, &line) == -1) {
            goto error;
        }
        if (strcmp(line, "") == 0) {
            free(line);
            break;
        }
        if (http_parse_header_line(line, &field_name, &field_value) == -1) {
            goto error;
        }
        free(line);
        if (strcasecmp(field_name, "Content-Length") == 0) {
            content_length = atoi(field_value);
        }
    }

    if (content_length == -1 || content_length == 0) {
        debug("No or 0 content-length.");
        content_length = 0;
    }
    else {
        ssize_t recv_size = 0;
        size_t payload_offset = 0;
        payload = (char *)calloc(sizeof(char), content_length + 1);
        check_mem(payload);
        while ((recv_size = read(sockfd, payload + payload_offset, content_length-payload_offset)) > 0) {
            payload_offset += recv_size;
        }
        if (recv_size == 0) {
            debug("End of TCP stream.");
            if (payload_offset < content_length) {
                log_err("Connection Error before receiving full message.");
                goto error;
            }
        }
        else if (recv_size == -1) {
            log_err("Error while reading TCP stream.");
            goto error;
        }
    }

    // create and fill return object
    response = (struct HttpResponse *)malloc(sizeof(struct HttpResponse));
    check_mem(response);
    response->status = status;
    response->content_length = content_length;
    response->payload = payload;

    return response;

error:
    free(payload);
    free(line);
    return NULL;
}


struct HttpResponse *executeRequest(struct Host *host, struct HttpRequest *request) {
    debug("execute request %s:%d", host->url, host->port);
    int sockfd = http_open_connection(host->url, host->port);
    if (sockfd == -1) {
        return NULL;
    }

    if (http_send_request(sockfd, request) != 0) {
        return NULL;
    }

    struct HttpResponse *response = HttpResponseFromEndpoint(sockfd);
    debug("Close socket.");
    close(sockfd);

    return response;
}

const char *http_reason_phrase(int response_status) {
    switch (response_status) {
        case 200: return "OK";
        case 404: return "Not Found";
        case 500: return "Server Error";
        default: log_err("Unknown response status %d", response_status); return "";
    }
}


int http_send_request(int sockfd, struct HttpRequest *request) {
    char http_post[] = "POST %s HTTP/1.1\r\n\
Connection: close\r\n\
Content-Length: %d\r\n\r\n\
%s";

    char *buf;
    int allocatedBytes = asprintf(&buf, http_post, "/query", request->content_length, request->payload);
    if (allocatedBytes == -1) {
       log_err("An error occurred while creating response.");
        return -1;
    }
    if (send(sockfd, buf, strlen(buf), 0) == -1) {
        log_err("Send response.");
        free(buf);
        return -1;
    };
    debug("SEND\n%s\n", buf);
    free(buf);
    return 0;
}


int http_send_response(int sockfd, struct HttpResponse *response) {
    debug("http_send_response");
    int status = (response != NULL) ? response->status : 200;
    int content_length = (response != NULL) ? response->content_length : 0;
    const char *payload = (response != NULL) ? response->payload : "";

    char *buf;
    char http_response[] = "HTTP/1.1 %d %s\r\nContent-Length: %d\r\n\r\n%s";
    if (asprintf(&buf, http_response, status, http_reason_phrase(status),
                 content_length, payload) == -1) {
        log_err("An error occurred while creating response.");
        const char* error_response = "HTTP/1.1 500 ERROR\r\n\r\n";
        if (send(sockfd, error_response, strlen(error_response), 0) == -1) {
            log_err("Send response.");
            return -1;
        };
    }
    else {
        if (send(sockfd, buf, strlen(buf), 0) == -1) {
            log_err("Send response.");
            free(buf);
            return -1;
        };
        free(buf);
    }
    return 0;
}


void HttpRequest_free(struct HttpRequest *request) {
    if (request == NULL) {
        return;
    }
    free(request->method);
    free(request->resource);
    free(request->payload);
    free(request);
}


void HttpResponse_free(struct HttpResponse *response) {
    if (response == NULL) {
        return;
    }
    free(response->payload);
    free(response);
}
