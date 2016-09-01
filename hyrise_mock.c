#include "http.h"
#include "dbg.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>



void start_hyrise_mock(const char *port) {

    // create socket
    int socket = http_create_inet_socket(port);
    debug("Hyrise mock: Listening on port %s", port);

    // Disptach requests
    while(1) {
        // Allocates memory for request
        struct sockaddr socket_addr;
        socklen_t socket_len;
        int socket_fd = accept(socket, &socket_addr, &socket_len);
        if (socket_fd < 0) {
            log_err("Error: on accept.");
            continue;
        }

        struct HttpRequest *r;
        http_receive_request(socket_fd, &r);
        HttpRequest_free(r);


        struct HttpResponse resp;
        resp.status = 200;
        resp.payload = "Supi";
        resp.content_length = strlen(resp.payload);

        http_send_response(socket_fd, &resp);

        close(socket_fd);
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

