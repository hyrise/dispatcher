#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>



#define MAXPENDING 5
#define BUFFERSIZE 65535

const char HOSTS[4][2][16] = {{"127.0.0.1", "9010"}, {"127.0.0.1", "9011"}, 
                    {"127.0.0.1", "9012"}, {"127.0.0.1", "9013"}};

char answer[] = "HTTP/1.1 200 OK\n\
Content-Type: text/html\n\
Content-Length: 4\r\n\r\n\
json";




void new_connection(int sock) {
    char buf[BUFFERSIZE];
    int recv_size = read(sock, buf, BUFFERSIZE);
    printf("received %d bytes\n", recv_size);
    printf("%s\n", buf);

    send(sock, answer, strlen(answer), 0);
    close(sock);
}




int db_server(int i) {
    printf("Start server: %d\n", i);

    const char *Host = HOSTS[i][0]; 
    const char *Port = HOSTS[i][1];
    int n, sock, errno;

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((n = getaddrinfo(Host, Port, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", strerror(n));
        return 1;
    }

    if ((sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0) {
        fprintf(stderr, "can't create socket: %s\n", strerror(errno));
        return 2;
    }

    if (bind(sock, res->ai_addr, res->ai_addrlen) < 0) {
        close(sock);
        fprintf(stderr, "can't bind to socket: %s\n", strerror(errno));
        return 2;
    }

    if (listen(sock, MAXPENDING) < 0) {
        fprintf(stderr, "can't listen to socket: %s\n", strerror(errno));
        return 2;
    }

    socklen_t client_addrlen = sizeof(client_addrlen);
    struct sockaddr client_addr;


    while(1) {
        int new_sock = accept(sock, &client_addr, &client_addrlen);

        if (new_sock < 0) {
            fprintf(stderr, "ERROR on accept\n");
            exit(1);
        }

        int pid;

        pid = fork();

        if (pid < 0) {
            fprintf(stderr, "ERROR on fork\n");
            exit(1);
        }

        if (pid == 0) {
            // clield process
            close(sock);
            new_connection(new_sock);
            exit(0);
        }
        
        // parent
        close(new_sock);

        if (client_addr.sa_family == AF_INET){
            struct sockaddr_in *client_addr_ip4 = (struct sockaddr_in *) &client_addr;
            printf("client %d\n", client_addr_ip4->sin_addr.s_addr);
        } else {
            /* not an IPv4 address */
        }



    }
}



int main(int argc, char const *argv[]){
    int pid;
    int i;
    for (i=0; i<3; i++) {
        pid = fork();
        if (pid == 0) {
            // clield process
            db_server(i);
            exit(0);
        }
    }
    db_server(3);

    return 0;
}