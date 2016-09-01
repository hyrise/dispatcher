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
    int db_s1 = http_open_connection("192.168.31.38", 5000);
    //int db_s2 = http_open_connection("192.168.31.38", 5001);

    // if (socket < 0 || db_s1 < 0 || db_s2 < 0) {
    //     log_err("connection opening");
    //     exit(-1);
    // }

    /* Initialize the set of active sockets. */
    FD_ZERO (&active_fd_set);
    FD_SET (dispatcher_socket, &active_fd_set);
    FD_SET (db_s1, &active_fd_set);
    //FD_SET (db_s2, &active_fd_set);

    int session_to_socket[FD_SETSIZE];

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
        for (i = 0; i < FD_SETSIZE; ++i) {
            if (FD_ISSET (i, &read_fd_set)) {
                if (i == dispatcher_socket) {
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
                             "Server: connect from host %s, port %hd.\n",
                             inet_ntoa (clientname.sin_addr),
                             ntohs (clientname.sin_port));
                    FD_SET (new, &active_fd_set);
                } else {
                    /* Data arriving on an already-connected socket. */

                    if (i == db_s1) {
                        debug("Server response");
                        struct HttpResponse *received_response;
                        int rc = http_receive_response(i, &received_response);
                        if (rc != HTTP_SUCCESS) {
                            debug("Invalid Http response. returned %d", rc);
                            assert(received_response == NULL);
                            // TODO send error msg to client
                            close(i);
                            FD_CLR (i, &active_fd_set);
                            continue;
                        }
                        int session_id = get_session_from_json(received_response->payload, received_response->content_length);
                        if (session_id == -1) {
                            log_err("invalid session");
                            exit(-1);
                        }
                        rc = http_send_response(session_to_socket[session_id], received_response);
                    }
                    else {
                        debug("Client request: %d", i);
                        struct HttpRequest *received_request;
                        int rc = http_receive_request(i, &received_request);
                        if (rc != HTTP_SUCCESS) {
                            debug("Invalid Http request. returned %d", rc);
                            assert(received_request == NULL);
                            // TODO send error msg to client
                            close(i);
                            FD_CLR (i, &active_fd_set);
                            continue;
                        }
                        int session_id = get_session_from_json(received_request->payload, received_request->content_length);
                        if (session_id == -1) {
                            log_err("invalid session");
                            exit(-1);
                        }
                        session_to_socket[session_id] = i;
                        rc = http_send_request(db_s1, received_request);
                    }
                }
            }
        }
    }
}

