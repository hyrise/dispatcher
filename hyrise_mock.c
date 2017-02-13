#include "http.h"
#include "dbg.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>



void start_hyrise_mock(const char *port) {

    // create socket
    int db_socket = http_create_inet_socket(port);
    debug("Hyrise mock: Listening on port %s", port);

    fd_set active_fd_set, read_fd_set;
    /* Initialize the set of active sockets. */
    FD_ZERO (&active_fd_set);
    FD_SET (db_socket, &active_fd_set);

    char buffer[BUFFERSIZE];

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
                } else {
                    ssize_t data_size;
                    // data_size = recv(i, buffer, BUFFERSIZE - 1, MSG_DONTWAIT);

                    // //while ((data_size = recv(i, buffer, 1, MSG_DONTWAIT)) == 1) {};

                    // if (data_size == 0) {
                    //     log_err("Connection closed");
                    //     close(i);
                    //     FD_CLR (i, &active_fd_set);
                    //     continue;
                    // }
                    // if (data_size < 0) {
                    //     if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    //         // ignor
                    //     } else {
                    //         log_err("Error recv");
                    //         continue;
                    //     }
                    // }
                    // buffer[data_size] = '\0';
                    // debug("%s\n", buffer);

                    // char http_response[] = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
                    // ssize_t send_size = send_all(i, http_response, strlen(http_response), 0);
                    // if (send_size != strlen(http_response)) {
                    //     log_err("send_size != data_size");
                    // }

                    struct HttpRequest *r;
                    int error = http_receive_request(i, &r);
                    if (error != HTTP_SUCCESS) {
                        if (error == ERR_EOF || errno == ERR_CONNECTION_RESET) {
                            debug("Connection closed");
                            close(i);
                            FD_CLR (i, &active_fd_set);
                            continue;
                        }
                    }
                    HttpRequest_free(r);


                    struct HttpResponse resp;
                    resp.status = 200;
                    resp.payload = "Supi";
                    resp.content_length = strlen(resp.payload);

                    http_send_response(i, &resp);
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

