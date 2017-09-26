#include <string.h>
#include <sys/select.h>
#include <stdio.h>
#include <assert.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "http.h"
#include "dbg.h"


char *strnstr(const char *s1, const char *s2, size_t len)
{
	size_t l2;
	l2 = strlen(s2);
	if (!l2)
		return (char *)s1;
	while (len >= l2) {
		len--;
		if (!memcmp(s1, s2, l2))
			return (char *)s1;
		s1++;
	}
	return NULL;
}

int get_session_from_json(char *str, size_t str_len) {
    debug("get_session_from_json");
    int session_id = -1;
    char *session_id_s = strnstr(str, "sessionId", str_len);
    if (session_id_s == NULL) {
        log_err("payload without sessionId: got:\n%s", str);
        exit(EXIT_FAILURE);
    }
    debug("Session String: %s\n", session_id_s);
    sscanf(session_id_s, "sessionId\":%d", &session_id);
    debug("Session %d\n", session_id);
    return session_id;
}



int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("USAGE: %s PORT\n", argv[0]);
        exit(-1);
    }

    const char *port = argv[1];

    fd_set active_fd_set, read_fd_set;

    int dispatcher_socket =  http_create_inet_socket(port);
    // int db_s1 = http_open_connection("127.0.0.1", 5001);
    // int db_s2 = http_open_connection("127.0.0.1", 5002);

    // if (socket < 0 || db_s1 < 0 || db_s2 < 0) {
    //     log_err("connection opening");
    //     exit(-1);
    // }

    /* Initialize the set of active sockets. */
    FD_ZERO (&active_fd_set);
    FD_SET (dispatcher_socket, &active_fd_set);

    //int db_socket_to_client_socket[FD_SETSIZE];
    int client_connections[FD_SETSIZE][3];
    int client_connections_length = 0;

    char buffer[1024];
    ssize_t data_size;
    ssize_t offset = 0;

    while (1) {
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
        if (FD_ISSET (dispatcher_socket, &read_fd_set)) {
            /* Connection request on original socket. */
            int new;
            size = sizeof (clientname);
            new = accept (dispatcher_socket,
                          (struct sockaddr *) &clientname,
                          &size);
            if (new < 0) {
                perror ("accept");
                exit (EXIT_FAILURE);
            }
            fprintf (stderr,
                     "Server: connect from host %s, port %hu.\n",
                     inet_ntoa (clientname.sin_addr),
                     ntohs (clientname.sin_port));

            int db_s1 = http_open_connection("127.0.0.1", 5001);
            int db_s2 = http_open_connection("127.0.0.1", 5002);
            FD_SET (new, &active_fd_set);
            FD_SET (db_s1, &active_fd_set);
            FD_SET (db_s2, &active_fd_set);
            //db_socket_to_client_socket[db_s1] = new;
            //db_socket_to_client_socket[db_s2] = new;
            client_connections[client_connections_length][0] = new;
            client_connections[client_connections_length][1] = db_s1;
            client_connections[client_connections_length][2] = db_s2;
            debug("New connection (Client %d): %d %d %d", client_connections_length, new, db_s1, db_s2);
            client_connections_length += 1;

        }
        for (i = 0; i < client_connections_length; i++) {
            if (client_connections[i][0] == -1) {
                continue;
            }
            if (FD_ISSET(client_connections[i][0], &read_fd_set)) {
                /* Data arriving on client socket. */
                debug("Client request: %d", i);
                data_size = recv(client_connections[i][0], buffer, 1024, MSG_DONTWAIT);
                if (data_size == 0) {
                    log_err("Connection closed by client %d", i);
                    close(client_connections[i][0]);
                    FD_CLR (client_connections[i][0], &active_fd_set);
                    close(client_connections[i][1]);
                    FD_CLR (client_connections[i][1], &active_fd_set);
                    close(client_connections[i][2]);
                    FD_CLR (client_connections[i][2], &active_fd_set);
                    client_connections[i][0] = -1;
                    continue;
                } else if (data_size < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        log_err("Ignored blocking.");
                    } else {
                        log_err("Error recv");
                        // TODO
                        exit(-1);
                    }
                } else {
                    debug("Received '''%.*s'''(%lu)", (int)data_size, buffer, data_size);
                    data_size = send_all(client_connections[i][1], buffer, data_size, 0);
                }
                //offset += 1;
            }
            if (FD_ISSET(client_connections[i][1], &read_fd_set)) {
                /* Data arriving on db socket. */
                debug("DB response: %d", i);
                data_size = recv(client_connections[i][1], buffer, 1024, MSG_DONTWAIT);

                if (data_size == 0) {
                    log_err("Connection closed by DB");
                    close(client_connections[i][1]);
                    FD_CLR (client_connections[i][1], &active_fd_set);
                    exit(-1);
                } else if (data_size < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        log_err("Ignored blocking.");
                    } else {
                        log_err("Error recv");
                        // TODO
                        exit(-1);
                    }
                } else {
                    debug("Received '''%.*s'''(%lu)", (int)data_size, buffer, data_size);
                    data_size = send_all(client_connections[i][0], buffer, data_size, 0);
                }
            }
            if (FD_ISSET(client_connections[i][2], &read_fd_set)) {
                /* Data arriving on db socket. */
                data_size = read(client_connections[i][2], buffer, 1024);
                if (data_size == 0) {
                    log_err("Connection closed by DB");
                    close(client_connections[i][2]);
                    FD_CLR (client_connections[i][2], &active_fd_set);
                    exit(-1);
                } else if (data_size < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // ignor
                    } else {
                        log_err("Error recv");
                        // TODO
                        exit(-1);
                    }
                } else {
                    debug("Received '''%.*s'''(%lu)", (int)data_size, buffer, data_size);
                    data_size = send_all(client_connections[i][0], buffer, data_size, 0);
                }
            }
        }
    }
}

