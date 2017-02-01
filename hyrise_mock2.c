#include "http.h"
#include "dbg.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>


int db_s1;


struct connection {
    char buffer[BUFFERSIZE];
    int buffer_offset;
};



int get_DB_connection() {
    return db_s1;
}


void start_hyrise_mock(const char *port) {

    // create socket
    int dispatcher_socket = http_create_inet_socket(port);
    debug("Hyrise Dispatcher: Listening on port %s", port);

    fd_set active_fd_set, read_fd_set;
    /* Initialize the set of active sockets. */
    FD_ZERO (&active_fd_set);
    FD_SET (dispatcher_socket, &active_fd_set);


    struct connection *conns[FD_SIZE] = {NULL};

    int DB_connections[FD_SIZE];
    int j;
    for (j = 0; j < FD_SIZE; j++) {
        DB_connections[j] = -1;
    }

    int db_s1 = http_open_connection("192.168.31.38", 5000);
    FD_SET (db_s1, &active_fd_set);

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
                if (i == dispatcher_socket) {
                    /* Connection request on original socket. */
                    int new;
                    size = sizeof (clientname);
                    new = accept (dispatcher_socket,
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
                } else {
                    /* Data arriving on an already-connected socket. */
                    struct connection *con = conns[i];
                    if (con == NULL) {
                        struct connection *new_conn = malloc(sizeof(struct connection));
                        new_conn->buffer_offset = 0;
                        conns[i] = new_conn;
                    }

                    ssize_t data_size = recv(i, conns[i]->buffer + conns[i]->buffer_offset, BUFFERSIZE - buffer_offset, MSG_DONTWAIT);
                    if (data_size == 0) {
                        debug("Connection closed by client");
                        close(i);
                        free(conns[i]);
                        conns[i] = NULL;
                        DB_connections[i] == -1;
                        FD_CLR (i, &active_fd_set);
                        continue;
                    }
                    if (data_size < 0) {
                        if (errno == EAGAIN) {
                            // ignor
                        } else {
                            log_err("Error recv");
                            continue;
                        }
                    }
                    conns[i]->buffer_offset += data_size;

                    if (DB_connections[i] == -1) {
                        // client connection
                        struct HttpRequest *request = NULL;
                        status = connection_http_request_parse(con, &request);
                        if (status == 0) {
                            int db_con = get_DB_connection();
                            DB_connections[db_con] = i;
                            http_send_request(db_conn, request)
                            HttpRequest_free(request);
                        }
                    } else {
                        // database connection
                        assert(i == db_s1);
                        struct HttpResponse *response = NULL;
                        status = connection_http_response_parse(con, &response);
                        if (status == 0) {
                            int client_con = DB_connections[i];
                            http_send_response(client_conn, request)
                            HttpResponse_free(request);
                            DB_connections[-1];
                        }
                    }

                    // debug("Client request: %d", i);
                    // struct HttpRequest *request;
                    // int error = http_receive_request(i, &request);

                    // if (error == ERR_EOF) {
                    //     debug("Connection closed");
                    //     close(i);
                    //     FD_CLR (i, &active_fd_set);
                    //     continue;
                    // } else {
                    //     //printf("%s\n", request->payload);
                    //     HttpRequest_free(request);
                    // }


                    ssize_t data_size;
                    data_size = recv(i, buffer, BUFFERSIZE, MSG_DONTWAIT);

                    //while ((data_size = recv(i, buffer, 1, MSG_DONTWAIT)) == 1) {};

                    if (data_size == 0) {
                        debug("Connection closed");
                        close(i);
                        FD_CLR (i, &active_fd_set);
                        continue;
                    }
                    if (data_size < 0) {
                        if (errno == EAGAIN) {
                            // ignor
                        } else {
                            log_err("Error recv");
                            continue;
                        }
                    }
                    buffer[data_size] = '\0';
                    debug("%s\n", buffer);

                    char http_response[] = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
                    ssize_t send_size = send_all(i, http_response, strlen(http_response), 0);
                    if (send_size != strlen(http_response)) {
                        log_err("send_size != data_size");
                    }
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

    char *port = argv[1];

    start_hyrise_mock(port);
    return 0;
}

