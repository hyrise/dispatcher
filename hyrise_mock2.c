#include "http.h"
#include "dbg.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>


#define CLIENT_CONNECTION 0
#define DB_CONNECTION 1

#define BUFFERSIZE 1024


struct connection {
    int type;
    int corresponding_socket;
    char buffer[BUFFERSIZE];
    int buffer_offset;
};





void start_dispatcher(const char *port) {

    // create socket
    int dispatcher_socket = http_create_inet_socket(port);
    debug("Hyrise Dispatcher: Listening on port %s", port);

    fd_set active_fd_set, read_fd_set;
    /* Initialize the set of active sockets. */
    FD_ZERO (&active_fd_set);
    FD_SET (dispatcher_socket, &active_fd_set);


    struct connection *conns[FD_SETSIZE] = {NULL};

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
		    // Client connection request on dispatcher socket.
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

                    // create connection structure
                    struct connection *new_connection = malloc(sizeof(struct connection));
                    new_connection->type = CLIENT_CONNECTION;
                    new_connection->buffer_offset = 0;
                    conns[i] = new_connection;

		    // open corresponding DB connection
                    int db = http_open_connection("192.168.31.38", 5000);
                    FD_SET (db, &active_fd_set);
                    struct connection *new_db_connection = malloc(sizeof(struct connection));
                    new_db_connection->type = DB_CONNECTION;
                    new_db_connection->buffer_offset = 0;
                    new_db_connection->corresponding_socket = i;
                    conns[db] = new_db_connection;

                    new_connection->corresponding_socket = db;
                    
                    

                } else {
                    /* Data arriving on an already-connected socket. */
	            struct connection *con = conns[i];
                    ssize_t data_size = recv(i, con->buffer + con->buffer_offset, BUFFERSIZE - con->buffer_offset, MSG_DONTWAIT);
                    if (data_size == 0) {
		      assert(con->type == CLIENT_CONNECTION);
                        debug("Connection closed by client");
                        close(i);

                        // Close corresponding DB socket
                        int db_socket = con->corresponding_socket;
			free(conns[db_socket]);
                        conns[db_socket] = NULL;
                        close(db_socket);

                        free(con);
                        conns[i] = NULL;
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

                    if (con->type == CLIENT_CONNECTION) {
                        struct HttpRequest *request = NULL;
                        // status = http_request_parse(con, &request);
                        int status = http_receive_request(i, &request);
                        if (status == 0) {
			    http_send_request(con->corresponding_socket, request);
                            HttpRequest_free(request);
                        }
                    } else {
                        // database connection
		        assert(con->type == DB_CONNECTION);
                        struct HttpResponse *response = NULL;
                        // status = connection_http_response_parse(con, &response);
                        int status = http_receive_response(i, &response);
                        if (status == 0) {
			    http_send_response(con->corresponding_socket, response);
                            HttpResponse_free(response);
                        }
                    }
                }
            }
        }
    }
}




int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("USAGE: ./start_dispatcher PORT\n");
        return -1;
    }

    char *port = argv[1];

    start_dispatcher(port);
    return 0;
}

