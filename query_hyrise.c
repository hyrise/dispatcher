#include "http.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#include <signal.h>

#include "dbg.h"

int num_threads = 1;
int num_queries = 1;

#define BUFFER_SIZE 16384


unsigned timediff(struct timeval start,  struct timeval stop) {
    return (stop.tv_usec - start.tv_usec) + (stop.tv_sec - start.tv_sec) * 1000 * 1000;
}

struct query_hyrise_arg {
    struct Host *host;
    struct HttpRequest *request;
};


void *query_hyrise(void *arg) {
    int i;
    int success_counter = 0;
    struct query_hyrise_arg *qha = (struct query_hyrise_arg *)arg;

    int socket = http_open_connection(qha->host->url, qha->host->port);
    struct HttpResponse *response;
    int http_error = HTTP_SUCCESS;

    for (i = 0; i < num_queries/num_threads; ++i) {
        if ((http_error = http_send_request(socket, qha->request)) != HTTP_SUCCESS) {
            log_err("Error on send request\n");
            if (http_error == ERR_EOF || http_error == ERR_BROKEN_PIPE || http_error == ERR_CONNECTION_RESET) {
                i -= 1;
                close(socket);
                socket = http_open_connection(qha->host->url, qha->host->port);
                continue;
            }
            exit(-1);
        }

        if ((http_error = http_receive_response(socket, &response)) != HTTP_SUCCESS) {
            log_err("http error on response %d\n", http_error);
            if (http_error == ERR_EOF || http_error == ERR_BROKEN_PIPE || http_error == ERR_CONNECTION_RESET) {
                debug("Unexpected Connection close. Retry..");
                i -= 1;
                close(socket);
                socket = http_open_connection(qha->host->url, qha->host->port);
                continue;
            }
            exit(-1);
        } else {
            debug("Received: %s", response->payload);
            success_counter += 1;
            HttpResponse_free(response);
        }
    }
    close(socket);
    return NULL;
}


int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);
    if (argc != 6) {
        printf("USAGE: ./query_hyrise HOST PORT NUM_QUERIES NUM_THREADS FILE\n");
        return -1;
    }

    char *url = argv[1];
    char *port = argv[2];
    num_queries = atoi(argv[3]);
    num_threads = atoi(argv[4]);
    char *file_name = argv[5];

    char query[BUFFER_SIZE];

    FILE *f = fopen(file_name, "r");
    if (f == NULL) {
        printf("%s\n", strerror(errno));
        exit(-1);
    }
    strncpy(query, "query=", strlen("query="));
    size_t s = fread(&query[6], sizeof(char), BUFFER_SIZE-6, f);

    if (ferror(f)) {
        printf("ERROR reading query file\n");
        return -1;
    } else {
        query[s + 6] = '\0';
    }

    fclose(f);

    struct Host h;
    h.url = url;
    h.port = atoi(port);

    struct HttpRequest r;
    r.method = "POST";
    r.resource = "/qu";
    r.content_length = strlen(query);
    r.payload = query;

    struct query_hyrise_arg arg;
    arg.host = &h;
    arg.request = &r;

    int i;
    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    struct timeval query_start, query_end;
    gettimeofday(&query_start, NULL);
    for (i = 0; i < num_threads; ++i) {
        pthread_create(threads + i, NULL, &query_hyrise, (void *)&arg);
    }

    for (i = 0; i < num_threads; ++i) {
        pthread_join(*(threads + i), NULL);
    }
    free(threads);
    gettimeofday(&query_end, NULL);
    printf("%f queries/s\n", (num_queries * 1000 * 1000.0)/timediff(query_start, query_end));

    return 0;
}
