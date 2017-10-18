#include "http.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include "dbg.h"
#include "dict.h"

#define MAXPENDING 20



const char *dict_get_case(const struct dict *d, const char *key) {
    if (d == NULL)
        return NULL;

    struct dict_item *current_item = d->head;
    while (current_item != NULL) {
        if (!strcasecmp(current_item->key, key))
            return current_item->value;     // found -> return
        current_item = current_item->next;
    }
    return NULL;
}



ssize_t send_all(int socket, const void *buffer, size_t length, int flags) {
    ssize_t offset = 0;
    ssize_t sent_bytes = 0;

    while (offset != length) {
        sent_bytes = send(socket, buffer + offset, length - offset, flags);
        if (sent_bytes < 0) {
            switch (errno) {
            case EINTR:
                continue;
            default:
                return -1;
            }
        }
        offset += sent_bytes;
    }
    return offset;
}


ssize_t read_all(int socket, void *buffer, size_t length) {
    ssize_t offset = 0;
    ssize_t read_bytes = 0;

    while (offset != length) {
        read_bytes = read(socket, buffer + offset, length - offset);
        if (read_bytes < 0) {
            switch (errno) {
            case EINTR:
                continue;
            default:
                return -1;
            }
        }
        offset += read_bytes;
    }
    return offset;
}




int http_open_connection(const char *url, int port) {
    int sock;
    struct sockaddr_in dest;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        log_err("ERROR: could not create a socket.");
        return -1;
    }

    //---Initialize server address/port struct
    bzero(&dest, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = inet_addr(url);
    dest.sin_port = htons(port);

    //---Connect to server
    if (connect(sock, (struct sockaddr*)&dest, sizeof(dest)) != 0) {
        log_err("ERROR: could not connect to host.");
        return -1;
    }

    return sock;
}


int http_create_inet_socket(const char *port) {
    static const u_int one = 1;
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

    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
        log_err("Error: Can't set SO_REUSEADDR.");
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
    const size_t BUFFERSIZE = 1024;
    char buf[BUFFERSIZE];

    check_mem(buf);
    size_t buf_offset = 0;  // total received bytes
    size_t recv_size;
    int got_cr = 0;

    while (TRUE) {
        recv_size = read(sockfd, buf+buf_offset, 1);
        if (recv_size < 0) {
            switch (errno) {
            case EINTR:
                continue;
            case ECONNRESET:
                free(buf);
                log_info("Connection reset.");
                return ERR_CONNECTION_RESET;
            default:
                log_err("Error on read.");
                exit(1);
            }
        } else if (recv_size == 0) {
            if (buf_offset == 0)
		continue;
            free(buf);
            log_err("Read EOF.");
            return ERR_EOF;
        }
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
        if (buf_offset == BUFFERSIZE) {
            buf[BUFFERSIZE - 1] = '\0';
            log_err("Receive buffer is full:\n\tReceived: %s", buf);
            return -1;
        }
    }
    if (*line != NULL) {
        debug("Receive line: %s", *line);
        return 0;
    }
    debug("Allocting line failed.");
    return -1;
}


int http_parse_request_line(char *line, char **method, char **resource, char **version) {
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
    *version = strndup(line + resource_end + 6, 3);
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


int http_receive_headers(int sockfd, struct dict *headers) {
    int http_error = HTTP_SUCCESS;
    char *line = NULL;

    while (TRUE) {
        char *field_name;
        char *field_value;
        if ((http_error = http_read_line(sockfd, &line)) != HTTP_SUCCESS) {
            break;
        }
        if (strcmp(line, "") == 0) {
            debug("End of Http header");
            break;
        }
        if ((http_error = http_parse_header_line(line, &field_name, &field_value)) != HTTP_SUCCESS) {
            break;
        }
        free(line);
        dict_set(headers, field_name, field_value);
    }
    free(line);
    return http_error;
}


int http_receive_payload(int sockfd, char **payload, int content_length) {
    assert(content_length > 0);
    *payload = NULL;
    ssize_t recv_size = 0;
    *payload = (char *)calloc(sizeof(char), content_length + 1);
    check_mem(payload);

    recv_size = read_all(sockfd, *payload, content_length);
    debug("%s", *payload);

    if (recv_size == content_length) {
        debug("Read complete Http payload.");
    } else if (recv_size == 0) {
        log_err("End of TCP stream.");
            return ERR_EOF;
    } else {
        log_err("Error while reading TCP stream.");
         // TODO handle
        exit(-1);
    }
    return HTTP_SUCCESS;
}


int http_receive_request(int sockfd, struct HttpRequest **request_ref) {
    debug("http_receive_request");
    *request_ref = NULL;
    char *method = NULL;
    char *resource = NULL;
    char *version = NULL;
    struct dict *headers = dict_create();
    int content_length = -1;
    char *payload = NULL;

    int http_error = HTTP_SUCCESS;
    char *line = NULL;

    if ((http_error = http_read_line(sockfd, &line)) != HTTP_SUCCESS) {
        goto error;
    }
    if ((http_error = http_parse_request_line(line, &method, &resource, &version)) != HTTP_SUCCESS) {
        goto error;
    }
    free(line);

    if ((http_error = http_receive_headers(sockfd, headers)) != HTTP_SUCCESS) {
        goto error;
    }

    const char *l = dict_get_case(headers, "Content-Length");
    if (l != 0) {
        content_length = atoi(l);
    }

    if (content_length == -1 || content_length == 0) {
        debug("No or 0 content-length.");
        content_length = 0;
    } else {
        if ((http_error = http_receive_payload(sockfd, &payload, content_length)) != HTTP_SUCCESS) {
            goto error;
        }
    }

    // create and fill return object
    struct HttpRequest *request = (struct HttpRequest *)malloc(sizeof(struct HttpRequest));
    check_mem(request);
    request->method = method;
    request->resource = resource;
    request->version = version;
    request->headers = headers;
    request->content_length = content_length;
    request->payload = payload;

    *request_ref = request;
    return HTTP_SUCCESS;

error:
    dict_free(headers);
    free(method);
    free(resource);
    free(version);
    free(payload);
    free(line);
    assert(http_error != HTTP_SUCCESS);
    return http_error;
}


int http_receive_response(int sockfd, struct HttpResponse **response_ref) {
    debug("http_receive_response");
    *response_ref = NULL;
    int status = 0;
    struct dict *headers = dict_create();
    int content_length = -1;
    char *payload = NULL;

    int http_error = HTTP_SUCCESS;
    char *line = NULL;

    if ((http_error = http_read_line(sockfd, &line)) != HTTP_SUCCESS) {
        goto error;
    }
    if ((http_error = http_parse_response_line(line, &status)) != HTTP_SUCCESS) {
        goto error;
    }
    free(line);

    if ((http_error = http_receive_headers(sockfd, headers)) != HTTP_SUCCESS) {
        goto error;
    }

    const char *l = dict_get_case(headers, "Content-Length");
    if (l != 0) {
        content_length = atoi(l);
    }

    if (content_length == -1 || content_length == 0) {
        debug("No or 0 content-length.");
        content_length = 0;
    } else {
        if ((http_error = http_receive_payload(sockfd, &payload, content_length)) != HTTP_SUCCESS) {
            goto error;
        }
    }

    // create and fill return object
    struct HttpResponse *response = (struct HttpResponse *)malloc(sizeof(struct HttpResponse));
    check_mem(response);
    response->status = status;
    response->headers = headers;
    response->content_length = content_length;
    response->payload = payload;

    *response_ref = response;

    return HTTP_SUCCESS;

error:
    dict_free(headers);
    free(payload);
    free(line);
    return http_error;
}


struct HttpResponse *http_execute_request(struct Host *host, struct HttpRequest *request) {
    debug("execute request %s:%d", host->url, host->port);
    int sockfd = http_open_connection(host->url, host->port);
    if (sockfd == -1) {
        return NULL;
    }

    if (http_send_request(sockfd, request) != 0) {
        close(sockfd);
        return NULL;
    }

    struct HttpResponse *response;
    int http_error =  http_receive_response(sockfd, &response);
    if (http_error != HTTP_SUCCESS) {
        debug("http error: http_execute_request");
    }
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
    debug("http_send_request");
    assert(request != NULL);
    assert(request->method != NULL);
    assert(request->resource != NULL);
    assert(request->version != NULL);

    char *payload = NULL;
    if (request->payload == NULL) {
        assert(request->content_length == 0);
        payload = "";
    } else {
        assert(request->content_length == strlen(request->payload));
        payload = request->payload;
    }
    char http_request_template[] = "%s %s HTTP/%s\r\n\
Content-Length: %d\r\n\r\n\
%s";
    char *buf;
    int allocatedBytes = asprintf(&buf, http_request_template, request->method, request->resource,
        request->version, request->content_length, payload);
    if (allocatedBytes == -1) {
        log_err("An error occurred while creating response.");
        exit(-1);
    }
    ssize_t sent_bytes;
    if ((sent_bytes = send_all(sockfd, buf, strlen(buf), 0)) == -1) {
        free(buf);
        if (errno == ECONNRESET) {
            return ERR_CONNECTION_RESET;
        }
        if (errno == EPIPE) {
            return ERR_BROKEN_PIPE;
        }
        log_err("Send request.");
        exit(-1);
    }
    if (sent_bytes != strlen(buf)) {
        log_err("ERROR: send was short. %zd of %lu bytes", sent_bytes, strlen(buf));
    }
    debug("SEND\n%s\n", buf);
    free(buf);
    return HTTP_SUCCESS;
}


int http_send_response(int sockfd, struct HttpResponse *response) {
    debug("http_send_response");
    char *payload = NULL;

    assert(response != NULL);
    if (response->payload == NULL) {
        assert(response->content_length == 0);
        payload = "";
    } else {
        assert(response->content_length == strlen(response->payload));
        payload = response->payload;
    }

    char *buf;
    char http_response[] = "HTTP/1.1 %d %s\r\nContent-Length: %d\r\n\r\n%s";
    if (asprintf(&buf, http_response, response->status, http_reason_phrase(response->status),
                 response->content_length, payload) == -1) {
        log_err("An error occurred while creating response.");
        // TODO
        const char* error_response = "HTTP/1.1 500 ERROR\r\n\r\n";
        if (send_all(sockfd, error_response, strlen(error_response), 0) == -1) {
            if (errno == ECONNRESET) {
                log_info("Connection reset.");
                return ERR_CONNECTION_RESET;
            }
            if (errno == EPIPE) {
                log_info("Broken pipe.");
                return ERR_BROKEN_PIPE;
            }
            log_err("Send response.");
            exit(-1);
        }
    }
    else {
        debug("Send: %s\n", buf);
        if (send_all(sockfd, buf, strlen(buf), 0) == -1) {
            free(buf);
            if (errno == ECONNRESET) {
                log_info("Connection reset.");
                return ERR_CONNECTION_RESET;
            }
            if (errno == EPIPE) {
                log_info("Broken pipe.");
                return ERR_BROKEN_PIPE;
            }
            log_err("Send response.");
            exit(-1);
        }
        free(buf);
    }
    return 0;
}


int HttpRequest_persistent_connection(struct HttpRequest *request) {
    const char *connection_type = dict_get_case(request->headers, "Connection");
    debug("Connection_type: HTTP/%s; Connection: %s", request->version, connection_type);

    if (strncmp(request->version, "1.1", 3) == 0 && connection_type == NULL) {
        return TRUE;
    } else if (strncmp(request->version, "1.1", 3) == 0 && connection_type != NULL && strncasecmp(connection_type, "close", 5) != 0) {
        return TRUE;
    }
    debug("No persistent connection.");
    return FALSE;
}


void HttpRequest_free(struct HttpRequest *request) {
    debug("http request free");
    if (request == NULL) {
        return;
    }
    free(request->method);
    free(request->resource);
    free(request->version);
    dict_free(request->headers);
    free(request->payload);
    free(request);
}


void HttpRequest_print(struct HttpRequest *request) {
    if (request == NULL) {
        printf("NULL(HttpRequest)\n\n");
        return;
    }
    printf("HttpRequest\n\tPersistent Connection:%d\n%s %s HTTP/%s\n%s\n\n",\
        HttpRequest_persistent_connection(request), request->method, request->resource, request->version, request->payload);
}


void HttpResponse_free(struct HttpResponse *response) {
    debug("http response free");
    if (response == NULL) {
        return;
    }
    dict_free(response->headers);
    free(response->payload);
    free(response);
}


void HttpResponse_print(struct HttpResponse *response) {
    if (response == NULL) {
        printf("NULL(HttpResponse)\n\n");
        return;
    }
    printf("HttpResponse\n%d\n%s\n\n", response->status, response->payload);
}
