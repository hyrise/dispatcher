#include "http.h"
#include "dbg.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#define NUM_THREADS 5


static pthread_mutex_t request_queue_mutex = PTHREAD_MUTEX_INITIALIZER;

struct request {
    int socket_fd;
    STAILQ_ENTRY(request) requests;
};


int start_hyrise_mock(const char *port) {

    // for (int i = 0; i < NUM_THREADS; ++i) {
    //     dispatch_requests, this, i);
    // }

    // create dispatcher socket
    int socket = http_create_inet_socket(port);
    debug("Hyrise mock: Listening on port %s", port);

    STAILQ_HEAD(slisthead, request) head = STAILQ_HEAD_INITIALIZER(head);
    //struct slisthead *headp;
    STAILQ_INIT(&head);

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
        debug("Main: new request.");
        {
            pthread_mutex_lock(&request_queue_mutex);
            debug("Main: push to request_queue.");

            struct request *r = (struct request *) malloc(sizeof(struct request));
            r->socket_fd = socket_fd;

            STAILQ_INSERT_TAIL(&head, r, requests);

            struct request *np;
            STAILQ_FOREACH(np, &head, requests)
                printf("%d\n", np->socket_fd);
            // TODO notify
            pthread_mutex_unlock(&request_queue_mutex);
        }
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
}