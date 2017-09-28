#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "dbg.h"
#include "http.h"
#include "http-parser/http_parser.h"

#define BUFFERSIZE 16384

int num_threads = 1;
int num_queries = 1;



unsigned timediff(struct timeval start,  struct timeval stop) {
    return (stop.tv_usec - start.tv_usec) + (stop.tv_sec - start.tv_sec) * 1000 * 1000;
}


struct response_parser_data {
    int db_socket;
    int remaining_queries;
    const char *http_msg;
};


// http-parser call back
int message_complete_callback(http_parser *parser) {
    debug("message_complete_callback");
    struct response_parser_data *data = (struct response_parser_data *)parser->data;

    data->remaining_queries--;

    if (http_should_keep_alive(parser) == 0) {
        close(data->db_socket);
        log_err("Server wants to close connection.");
        exit(EXIT_FAILURE);
    }

    if (data->remaining_queries > 0) {
        send_all(data->db_socket, data->http_msg, strlen(data->http_msg), 0);
    }

    return 0;
}



struct query_hyrise_arg {
    const char *url;
    int port;
    const char *query;
};


void *query_hyrise(void *arg) {
    struct query_hyrise_arg *qha = (struct query_hyrise_arg *)arg;
    const char *url = qha->url;
    int port = qha->port;
    const char *query = qha->query;
    int socket = http_open_connection(url, port);
    if (socket == -1) {
        exit(EXIT_FAILURE);
    }

    char buf[BUFFERSIZE];
    char http_post[] = "POST %s HTTP/1.1\r\n\
Content-Length: %d\r\n\r\n\
%s";
    char *buf_s;
    int allocatedBytes = asprintf(&buf_s, http_post, "/query", strlen(query), query);
    if (allocatedBytes == -1) {
        log_err("An error occurred while creating response.");
        exit(EXIT_FAILURE);
    }

    struct response_parser_data parser_data;
    parser_data.db_socket = socket;
    parser_data.remaining_queries = num_queries/num_threads;
    parser_data.http_msg = buf_s;

    // http-parser
    http_parser_settings settings;
    settings.on_message_complete = message_complete_callback;

    http_parser *parser = malloc(sizeof(http_parser));
    http_parser_init(parser, HTTP_RESPONSE);
    parser->data = (void *)&parser_data;


    send_all(socket, buf_s, strlen(buf_s), 0);

    while (1) {
        ssize_t n = read(socket, buf, BUFFERSIZE);
        if (n < 0) {
            // Handle Error
            log_err("Error on read()");
            exit(EXIT_FAILURE);

        } else if (n == 0) {
            // Connection closed
            log_err("Connection was closed by database.");
            exit(EXIT_FAILURE);
        }

        debug("RECEIVED %zd: '''%.*s'''", n, (int)n, buf);

        size_t nparsed = http_parser_execute(parser, &settings, buf, n);
        if (nparsed != n) {
            log_err("%s\n", http_errno_name(parser->http_errno));
            log_err("%s\n", http_errno_description(parser->http_errno));
            exit(EXIT_FAILURE);
        }
        if (parser_data.remaining_queries < 1) {
            break;
        }
    }

    free(buf_s);
    free(parser);
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
    int port = atoi(argv[2]);
    num_queries = atoi(argv[3]);
    num_threads = atoi(argv[4]);
    char *file_name = argv[5];

    char query[BUFFERSIZE];

    FILE *f = fopen(file_name, "r");
    if (f == NULL) {
        printf("%s\n", strerror(errno));
        exit(-1);
    }
    strncpy(query, "query=", strlen("query="));
    size_t s = fread(&query[6], sizeof(char), BUFFERSIZE-6, f);
    if (s == BUFFERSIZE-6) {
        log_err("Buffer to small.");
        exit(EXIT_FAILURE);
    }

    if (ferror(f)) {
        printf("ERROR reading query file\n");
        exit(EXIT_FAILURE);
    } else {
        query[s + 6] = '\0';
    }
    fclose(f);


    struct query_hyrise_arg arg;
    arg.url = url;
    arg.port = port;
    arg.query = query;

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
