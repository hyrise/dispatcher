#include "http.h"

#include <string.h>
#include <unistd.h>
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


struct HttpRequest *HttpRequestFromEndpoint(int sock) {
    char *buf = new char[BUFFERSIZE];
    size_t buf_offset = 0;  // total received bytes

    int first_line_received = FALSE;
    int header_received = FALSE;
    char *http_body_start = NULL;

    char method[16], resource[32];
    int content_length = 0;


    size_t read_bytes = 0;
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
                log_err("ERROR: Could not read content length!");
                break;
            } else {
                debug("Header Received #### Content-Length: %i", content_length);
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
    request->content_length = content_length;
    request->payload = strdup(http_body_start);

    free(buf);
    return request;
}
