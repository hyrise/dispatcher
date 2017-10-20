#include "http.h"
#include "dbg.h"
#include "http-parser/http_parser.h"

#include <assert.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>


#define BUFFERSIZE 1024
#define MAX_MESSAGE_SIZE (1024 * 1024)


char message[MAX_MESSAGE_SIZE];


struct parser_data {
    int socket;
    ssize_t buffer_offset;
    char buffer[1024];
};


// http-parser call backs
int on_url_callback(http_parser *parser, const char *at, size_t length) {
    debug("url_callback");

    struct parser_data *data = (struct parser_data *)parser->data;

    if (data->buffer_offset + length > 1023) {
        log_err("URL too long");
        return 0;
    }
    strncpy(data->buffer + data->buffer_offset, at, length);
    data->buffer_offset += length;

    return 0;
}

int on_headers_complete_callback(http_parser *parser) {
    debug("header_complete_callback");

    struct parser_data *data = (struct parser_data *)parser->data;
    assert(data->buffer_offset < 1024);
    data->buffer[data->buffer_offset] = '\0';
    debug("%s\n", data->buffer);

    return 0;
}



int on_message_complete_callback(http_parser *parser) {
    debug("message_complete_callback");
    long payload_size = 0;
    struct parser_data *data = (struct parser_data *)parser->data;

    if (strncmp(data->buffer, "/query/data/", strlen("/query/data/")) == 0) {
        payload_size = strtol(&data->buffer[strlen("/query/data/")], (char **)NULL, 10);
        assert(payload_size >= 0);
        if (payload_size > MAX_MESSAGE_SIZE) {
            payload_size = MAX_MESSAGE_SIZE;
        }
    }
    char write_buffer[1024];

    data->buffer_offset = 0;

    char http_response[] = "HTTP/1.1 200 OK\r\nContent-Length: %lu\r\n\r\n";
    sprintf(write_buffer, http_response, payload_size);
    // ssize_t send_size = send_all(data->socket , http_response, strlen(http_response), 0);
    // if (send_size != strlen(write_buffer)) {
    //     log_err("send_size != data_size");
    // }

    ssize_t send_size = send_all(data->socket , write_buffer, strlen(write_buffer), 0);
    if (send_size != (ssize_t)strlen(write_buffer)) {
        log_err("send_size != data_size");
    }
    debug("SEND: '''%s'''", write_buffer);
    send_size = send_all(data->socket, message, payload_size, 0);
    if (send_size != payload_size) {
        log_err("send_size != data_size");
    }
    debug("SEND: %lu Bytes", payload_size);

    if (http_should_keep_alive(parser) == 0) {
        close((int)parser->data);
    }
    return 0;
}


void start_hyrise_mock(const char *port) {
    http_parser *parsers[FD_SETSIZE] = {NULL};

    // create socket
    int db_socket = http_create_inet_socket(port);
    debug("Hyrise mock: Listening on port %s", port);

    fd_set active_fd_set, read_fd_set;
    /* Initialize the set of active sockets. */
    FD_ZERO (&active_fd_set);
    FD_SET (db_socket, &active_fd_set);

    char buffer[BUFFERSIZE];

    // http-parser callback settings
    http_parser_settings settings;
    settings.on_url = on_url_callback;
    settings.on_headers_complete = on_headers_complete_callback;
    settings.on_message_complete = on_message_complete_callback;

    // Disptach requests
    while(1) {
        read_fd_set = active_fd_set;

        debug("select");
        if (select (FD_SETSIZE, &read_fd_set, NULL, NULL, NULL) < 0) {
            perror ("select");
            exit (EXIT_FAILURE);
        }

        /* Service all the sockets with input pending. */
        int i;
        struct sockaddr_in clientname;
        socklen_t size;


        for (i = 0; i < FD_SETSIZE; ++i) {
            if (FD_ISSET (i, &read_fd_set)) {
                if (i == db_socket) {
                    /* Connection request on DB socket. */
                    int new;
                    size = sizeof (clientname);
                    new = accept (db_socket,
                                  (struct sockaddr *) &clientname,
                                  &size);
                    printf("size: %d instead of %lu\n", size, sizeof (clientname));
                    if (new < 0) {
                        perror ("accept");
                        exit (EXIT_FAILURE);
                    }
                    fprintf (stderr,
                             "Server: connect from host %s, port %hu. \n",
                             inet_ntoa (clientname.sin_addr),
                             ntohs (clientname.sin_port));
                    FD_SET (new, &active_fd_set);

                    http_parser *new_parser = malloc(sizeof(http_parser));
                    http_parser_init(new_parser, HTTP_REQUEST);

                    struct parser_data *new_data = malloc(sizeof(struct parser_data));
                    new_data->socket = new;
                    new_data->buffer_offset = 0;

                    new_parser->data = new_data;
                    parsers[new] = new_parser;

                } else {
                    ssize_t data_size;
                    data_size = recv(i, buffer, BUFFERSIZE - 1, MSG_DONTWAIT);

                    //while ((data_size = recv(i, buffer, 1, MSG_DONTWAIT)) == 1) {};

                    if (data_size == 0) {
                        log_err("Connection closed");
                        close(i);
                        FD_CLR (i, &active_fd_set);
                        free(parsers[i]->data);
                        free(parsers[i]);
                        parsers[i] = NULL;
                        continue;
                    }
                    if (data_size < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // ignor
                        } else {
                            log_err("Error recv");
                            continue;
                        }
                    }
                    debug("Received '''%.*s'''(%lu)", (int)data_size, buffer, data_size);
                    size_t nparsed = http_parser_execute(parsers[i], &settings, buffer, data_size);
                    if ((ssize_t)nparsed != data_size) {
                        log_err("%s\n", http_errno_name(parsers[i]->http_errno));
                        log_err("%s\n", http_errno_description(parsers[i]->http_errno));
                        exit(EXIT_FAILURE);
                    }


                    // struct HttpRequest *r;
                    // int error = http_receive_request(i, &r);
                    // if (error != HTTP_SUCCESS) {
                    //     if (error == ERR_EOF || errno == ERR_CONNECTION_RESET) {
                    //         debug("Connection closed");
                    //         close(i);
                    //         FD_CLR (i, &active_fd_set);
                    //         continue;
                    //     }
                    // }
                    // HttpRequest_print(r);
                    // HttpRequest_free(r);


                    // struct HttpResponse resp;
                    // resp.status = 200;
                    // resp.payload = "Supi";
                    // resp.content_length = strlen(resp.payload);

                    // http_send_response(i, &resp);
                }
            }
        }
    }
}




int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("USAGE: ./start_hyrise_mock PORT\n");
        return -1;
    }

    memset(message, '0', MAX_MESSAGE_SIZE - 1);
    char *port = argv[1];

    start_hyrise_mock(port);
    return 0;
}

