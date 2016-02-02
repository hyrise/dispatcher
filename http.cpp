#include "http.h"

#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "dbg.h"


char *strnstr_(const char *haystack, const char *needle, size_t len_haystack, size_t len_needle) {
    if (len_haystack == 0) {
        return (char *)haystack; /* degenerate edge case */
    }
    if (len_needle == 0) {
        return (char *)haystack; /* degenerate edge case */
    }
    while ((haystack = strchr(haystack, needle[0]))) {
        if (!strncmp(haystack, needle, len_needle)) {
            return (char *)haystack;
        }
        haystack++;
    }
    return NULL;
}


int get_content_lenght1(const char *buf, const int size, const char *lengthname) {
    const char *hit_ptr;
    int content_length;
    hit_ptr = strnstr_(buf, lengthname, size, 15);
    if (hit_ptr == NULL) {
        return -1;
    }
    char format [50];
    strcpy(format,lengthname);
    strcat(format," %d");
    fflush(stdout);
    if (sscanf(hit_ptr, format, &content_length) != 1) {
        return -1;
    }
    return content_length;
}


int get_content_lenght(const char *buf, const int size) {
    // TODO refactor
    int res = get_content_lenght1(buf, size, "Content-Length:");
    if (res == -1)
        res = get_content_lenght1(buf, size, "Content-length:");
        if (res == -1)
            res = get_content_lenght1(buf, size, "content-length:");
    return res;
}

int openConnection(struct Host *host) {
    int sock;
    struct sockaddr_in dest;

    if ( (sock = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
        log_err("ERROR: could not create a socket.");
        return -1;
    }

    //---Initialize server address/port struct
    bzero(&dest, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = inet_addr(host->url);
    dest.sin_port = htons(host->port);

    //---Connect to server
    if ( connect(sock, (struct sockaddr*)&dest, sizeof(dest)) != 0 ) {
        log_err("ERROR: could not connect to host.");
        return -1;
    }

    return sock;
}



struct HttpRequest *HttpRequestFromEndpoint(int sock) {
    char *buf = new char[BUFFERSIZE];
    size_t buf_offset = 0;  // total received bytes

    int first_line_received = FALSE;
    int header_received = FALSE;
    char *http_body_start = NULL;

    char method[16], resource[32];
    size_t content_length = 0;


    ssize_t read_bytes = 0;
    while ((read_bytes = read(sock, buf+buf_offset, BUFFERSIZE-buf_offset)) > 0) {
        buf_offset += read_bytes;

        if (!first_line_received) {
            char *hit_ptr;
            hit_ptr = strnstr_(buf, "\n", buf_offset, 1);
            if (hit_ptr == NULL) {
                continue;
            }
            first_line_received = TRUE;     // first line received
            // it can be parsed for http method and recource
            int n;
            if ((n = sscanf(buf, "%15s %31s HTTP/1.1", (char *)&method, (char *)&resource)) == 2) {
                debug("HTTP Request: Method %s Recource: %s", method, resource);
            } else {
                log_err("ERROR scanf %d", n);
                break;
            }
        }
        if (!header_received) {
            char *hit_ptr;
            hit_ptr = strnstr_(buf, "\r\n\r\n", buf_offset, 4);
            http_body_start = hit_ptr + 4;
            if (hit_ptr == NULL) {
                log_err("ERROR: not FOUND");
                continue;
            }
            header_received = TRUE;     // header delimiter reached
            
            content_length = get_content_lenght(buf, buf_offset);
            if (content_length == -1) {

                debug("Did not find content length, assume empty body! Message: %s", buf);
                content_length = 0;
                break;
            } else {
                debug("Header Received #### Content-Length: %zu", content_length);
            }
        }
        // complete header was received
        // check whether message is complete
        if (http_body_start != NULL) {
            if (((http_body_start - buf) + content_length) == buf_offset) {
                debug("%s", http_body_start);
                break;
            }
        }
    }

    if (read_bytes == 0) {
        debug("End of TCP stream.");
    } else if (read_bytes == -1) {
        log_err("Error while reading TCP stream.");
        free(buf);
        return NULL;
    }

    // create and fill return object
    struct HttpRequest *request = new struct HttpRequest;
    request->method = strdup(method);
    request->resource = strdup(resource);
    request->content_length = (int)content_length;
    if (content_length == 0) {
        request->payload = NULL;
    } else {
        request->payload = strdup(http_body_start);
    }
    free(buf);
    return request;
}


struct HttpResponse *executeRequest(struct Host *host, struct HttpRequest *request) {
    int sock = openConnection(host);
    if (sock == -1) {
        return NULL;
    }

    struct HttpResponse *response = new HttpResponse;

    char http_post[] = "POST %s HTTP/1.1\r\n\
Content-Length: %d\r\n\
Connection: Keep-Alive\r\n\r\n\
%s";

    char *buf;
    int allocatedBytes = asprintf(&buf, http_post, "/query/" /*request.getResource().c_str()*/, request->content_length, request->payload);
    if (allocatedBytes == -1) {
       log_err("Error during creating request.");
        return NULL;
    }
    send(sock, buf, strlen(buf), 0);
    free(buf);

    int offset = 0;
    int recv_size = 0;
    int first_line_received = 0;
    int header_received = 0;
    char *http_body_start = NULL;
    int status = 0;
    int content_length = 0;
    char *content;

    buf = new char[BUFFERSIZE];
    memset( buf, 0, BUFFERSIZE );

    while ((recv_size = read(sock, buf+offset, BUFFERSIZE-offset)) > 0) {
    debug("received %i bytes\ncontent: %s", recv_size, buf );
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
            if ((n = sscanf(buf, "HTTP/1.1 %d", &status)) == 1) {
                response->status = status;
                debug("HTTP Response status: %i", status);
            } else {
                log_err("ERROR----------------------- scanf %d", n);
                close(sock);
                return NULL;
            }
            if (status != 200) {
                close(sock);
                log_err("Wrong Status");
                return NULL;
            }
        }

        // first line received and checked successfully
        // check for content next
        if (!header_received) {
            char *hit_ptr;
            hit_ptr = strnstr_(buf, "\r\n\r\n", offset, 4);
            http_body_start = hit_ptr + 4;
            if (hit_ptr == NULL) {
                log_err("not FOUND");
                continue;
            }
            header_received = 1;
            // header delimiter reached
            content_length = get_content_lenght(buf, offset);
            response->content_length = content_length;
            debug("Content-Length: %i", content_length);
        }

        // complete header was received
        // check whether message is complete
        if (http_body_start != NULL) {
            if (((http_body_start - buf) + content_length) == offset) {
                //debug("complete message received\n header: %s", http_body_start-buf);
                content = http_body_start;
                response->payload = content;
                free(buf);
                close(sock);
                return response;
            }
        }
        debug("Read...");
    }
    free(buf);
    close(sock);
    return NULL;
}

void HttpRequest_free(struct HttpRequest *request) {
    free(request->method);
    free(request->resource);
    free(request->payload);
    free(request);
}
