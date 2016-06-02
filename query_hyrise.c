#include "http.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>

#define THREADS 20
#define QUERIES 100

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
    int error_counter = 0;
    struct query_hyrise_arg *qha = (struct query_hyrise_arg *)arg;
    for (i = 0; i < QUERIES/THREADS; ++i) {
        struct HttpResponse *response = executeRequest(qha->host, qha->request);
        if (response != NULL) {
            printf("%d ---------------------------------------\n %s\n", i, response->payload);
            HttpResponse_free(response);
        }
        else {
            error_counter += 1;
            continue;
        }
    }
    printf("Errors: %d\n", error_counter);
    return NULL;
}


int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("USAGE: ./start_dispatcher PORT HOST FILE\n");
        return -1;
    }

    char *port = argv[1];
    char *url = argv[2];
    char *file_name = argv[3];

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
    pthread_t threads[THREADS];
    struct timeval query_start, query_end;
    gettimeofday(&query_start, NULL);
    for (i = 0; i < THREADS; ++i) {
        pthread_create(&threads[i], NULL, &query_hyrise, (void *)&arg);
    }

    for (i = 0; i < THREADS; ++i) {
        pthread_join(threads[i], NULL);
    }
    gettimeofday(&query_end, NULL);
    printf("%f queries/s\n", (QUERIES * 1000 * 1000.0)/timediff(query_start, query_end));

    return 0;
}
