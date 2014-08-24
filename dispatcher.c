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


#define READ_QUERY 1
#define WRITE_QUERY 2
#define NEW_MASTER 3
#define SET_SLAVES 4


const char HOSTS[2][2][16] = {{"127.0.0.1", "5000"}, {"127.0.0.1", "5001"}};
const int NUM_HOSTS = 2;

int slaves = 0;
int current_master = 0; 
int active[2] = {1, 1};


char NotImpl[] = "HTTP/1.1 501 Not Implemented\n";

char answer[] = "HTTP/1.0 200 OK\n\
Content-Type: text/html\n\
Content-Length: 155\r\n\r\n\
Supported Operations:\n\
POST /jsonQuery JSON_QUERY\n\
POST /jsonWrite JSON_WRITE\n\
POST /new_master MASTER_IP MASTER_PORT\n\
POST /number_of_slaves NUMBER_OF_SLAVES\n";

char http_post[] = "POST %s HTTP/1.1\r\n\
Content-Length: %d\r\n\r\n\
%s";

char http_response[] = "HTTP/1.1 200 OK\r\n\
Content-Length: %d\r\n\r\n\
%s";


char answer2[] = "HTTP/1.1 204 No Content\r\n\r\n";




int get_socket(int host_nr) {
    int port = atoi(HOSTS[host_nr][1]);

    int sockfd;
    struct sockaddr_in dest;

    if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
        printf("ERROR: could not create a socket\n");
    }

    /*---Initialize server address/port struct---*/
    bzero(&dest, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = inet_addr(HOSTS[host_nr][0]);
    dest.sin_port = htons(port);

    /*---Connect to server---*/
    if ( connect(sockfd, (struct sockaddr*)&dest, sizeof(dest)) != 0 ) {
        printf("ERROR: could not connect to host\n");
    }
    return sockfd;

}


const char *strnstr(const char *haystack, char *needle, size_t len_haystack, size_t len_needle) {
    if (len_haystack == 0) return haystack; /* degenerate edge case */
    if (len_needle == 0) return haystack; /* degenerate edge case */
    while (haystack = strchr(haystack, needle[0])) {
        if (!strncmp(haystack, needle, len_needle)) return haystack;
        haystack++; }
    return NULL;
}

int get_content_lenght(const char *buf, const int size) {
    const char *hit_ptr;
    int content_length;
    hit_ptr = strnstr(buf, "Content-Length:", size, 15);

    if (hit_ptr == NULL) {
        printf("ERROR: no Content-Length specified\n");
        return -1;
    }
    if (sscanf(hit_ptr, "Content-Length: %d", &content_length) != 1) {
        printf("ERROR----------------------- scanf\n");
        return -1;
    }
    return content_length;
}




int get_request(int sock, char *buf, int *offset, int *action, char **content, int *length) {
#ifndef NDEBUG
    printf("Offset: %d\n", *offset);
#endif
    int recv_size = 0;
    int first_line_received = 0;
    int header_received = 0;
    char *http_body_start = NULL;
    char method[16], recource[32];

    while ((recv_size = read(sock, buf+(*offset), BUFFERSIZE-(*offset))) > 0) {
#ifndef NDEBUG        
        printf("received %d bytes\n", recv_size);
        printf("%s\n", buf);
#endif
        *offset += recv_size;

        if (!first_line_received) {
            char *hit_ptr;
            hit_ptr = strnstr(buf, "\n", *offset, 1);
            if (hit_ptr == NULL) {
                continue;
            }
            first_line_received = 1;
            // first line received
            // it can be parsed for http method and recource
            int n;
            if ((n = sscanf(buf, "%15s %31s HTTP/1.1", (char *)&method, (char *)&recource)) == 2) {
#ifndef NDEBUG                
                printf("HTTP Request: Method %s  Recource: %s\n", method, recource);
#endif
                if (strcmp(recource, "/jsonQuery") == 0)
                    *action = READ_QUERY;
                else if (strcmp(recource, "/jsonWrite") == 0)
                    *action = WRITE_QUERY;
                else if (strcmp(recource, "/new_master") == 0)
                    *action = NEW_MASTER;
                else if (strcmp(recource, "/number_of_slaves") == 0)
                    *action = SET_SLAVES;

                if ((strcmp(method, "POST") != 0) || (*action == 0)) {
                    send(sock, answer, sizeof(answer), 0);
                    return -1;
                }
            }
            else {
                printf("ERROR scanf %d\n", n);
                return -1;
            }
        }

        // first line received and checked successfully
        // requested action was set
        // check for content next
        if (!header_received) {
            char *hit_ptr;
            hit_ptr = strnstr(buf, "\r\n\r\n", *offset, 4);
            http_body_start = hit_ptr + 4;
            if (hit_ptr == NULL) {
                printf("ERROR: not FOUND\n");
                continue;
            }
            header_received = 1;
            // header delimiter reached
            *length = get_content_lenght(buf, *offset);
            if (length == -1)
            {
                printf("ERROR: Could not read content length!");
                exit(1);
            } else {
#ifndef NDEBUG
               printf("Header Received #### Content-Length: %d\n", *length);
#endif
            }
        }

        // complete header was received
        // check whether message is complete
        if (http_body_start != NULL) {
            if (((http_body_start - buf) + *length) == *offset) {
#ifndef NDEBUG
                printf("complete message received\n header: %ld\n", http_body_start-buf);
#endif
                *content = http_body_start;
                return 0;
                //perform_action(sock, http_body_start, content_length, requested_action);
            }
        }
#ifndef NDEBUG
        printf("Read...\n");
#endif
    }
#ifndef NDEBUG
    printf("End of reception\n");
#endif

    if (recv_size == -1) {
        fprintf(stderr, "ERROR while receiving data\n");
        return -1;
    }

    if ((action == 0) || (*content == NULL)) {
        return -1;
    }
    return 0;
}

int get_response(int sock, char *buf, int *offset, int *status, char **content, int *content_length) {
    int recv_size = 0;
    int first_line_received = 0;
    int header_received = 0;
    char *http_body_start = NULL;

    while ((recv_size = read(sock, buf+(*offset), BUFFERSIZE-(*offset))) > 0) {
#ifndef NDEBUG
        printf("received %d bytes\n", recv_size);
        printf("%s\n", buf);
#endif
        *offset += recv_size;

        if (!first_line_received) {
            char *hit_ptr;
            hit_ptr = strnstr(buf, "\n", *offset, 1);
            if (hit_ptr == NULL) {
                continue;
            }
            first_line_received = 1;
            // first line received
            // it can be parsed for http method and recource
            int n;
            if ((n = sscanf(buf, "HTTP/1.1 %d", status)) == 1) {
#ifndef NDEBUG
                printf("HTTP Response status: %d\n", *status);
#endif
            }
            else {
                printf("ERROR----------------------- scanf %d\n", n);
                return -1;
            }
        }

        // first line received and checked successfully
        // requested action was set
        // check for content next
        if (!header_received) {
            char *hit_ptr;
            hit_ptr = strnstr(buf, "\r\n\r\n", *offset, 4);
            http_body_start = hit_ptr + 4;
            if (hit_ptr == NULL) {
                printf("not FOUND\n");
                continue;
            }
            header_received = 1;
            // header delimiter reached
            *content_length = get_content_lenght(buf, *offset);
#ifndef NDEBUG
            printf("Content-Length: %d\n", *content_length);
#endif
        }

        // complete header was received
        // check whether message is complete
        if (http_body_start != NULL) {
            if (((http_body_start - buf) + *content_length) == *offset) {
#ifndef NDEBUG
                printf("complete message received\n header: %ld\n", http_body_start-buf);
#endif
                *content = http_body_start;
                return 0;
                //perform_action(sock, http_body_start, content_length, requested_action);
            }
        }
#ifndef NDEBUG
        printf("Read...\n");
#endif
    }

#ifndef NDEBUG
    printf("End of reception\n");
#endif

    if (recv_size == -1) {
        fprintf(stderr, "ERROR while receiving data\n");
        return -1;
    }

    if (*content == NULL) {
        return -1;
    }
    return 0;
}


int handle_request(int sock, int action, char *content, int content_length) {

#ifndef NDEBUG    
    printf("ACTION: %d\n", action);
#endif

    switch (action) {
    case READ_QUERY: {
        time_t t;
        srand((unsigned) time(&t));
        int r = rand() % NUM_HOSTS;
#ifndef NDEBUG
        printf("Query sent to host %d\n", r);
#endif
        int socketfd = get_socket(r);

        char *buf;
        asprintf(&buf, http_post, "/jsonQuery", content_length, content);
        send(socketfd, buf, strlen(buf), 0);
        free(buf);

        int res_content_length = 0;
        char res_buf[BUFFERSIZE];
        char *res_content;
        int offset = 0;
        int status = 0;

        get_response(socketfd, (char *)res_buf, &offset, &status, &res_content, &res_content_length);
        asprintf(&buf, http_response, res_content_length, res_content);

        send(sock, buf, strlen(buf), 0);
        free(buf);

        break;
    }
    case WRITE_QUERY: {
        int socketfd = get_socket(current_master);

        char *buf;
        asprintf(&buf, http_post, "/jsonQuery", content_length, content);
        send(socketfd, buf, strlen(buf), 0);
        free(buf);

        int res_content_length = 0;
        char res_buf[BUFFERSIZE];
        char *res_content;
        int offset = 0;
        int status = 0;

        get_response(socketfd, (char *)res_buf, &offset, &status, &res_content, &res_content_length);
        asprintf(&buf, http_response, res_content_length, res_content);

        send(sock, buf, strlen(buf), 0);
        free(buf);
        break;
    }
    case NEW_MASTER:
        send(sock, answer2, sizeof(answer2), 0);
        break;
    case SET_SLAVES:
        send(sock, answer2, sizeof(answer2), 0);
        break;
    }

    return 0;
}



void new_connection(int sock) {
    int action = 0;
    int content_length = 0;
    int offset = 0;
    char buf[BUFFERSIZE];
    char *content = NULL;
    while (1) {
        action = 0;
        offset = 0;
        content_length = 0;
        content = NULL;
        if (get_request(sock, buf, &offset, &action, &content, &content_length) == -1) {
            close(sock);
#ifndef NDEBUG
            printf("connection closed\n");
#endif
            exit(1);
        }
#ifndef NDEBUG
        printf("CONTENT OF: %s\n", content);
#endif
        if (handle_request(sock, action, content, content_length) == -1) {
            close(sock);
#ifndef NDEBUG
            printf("connection closed\n");
#endif
            exit(1);
        }
    }
}



int main(int argc, char const *argv[])
{
    if (argc != 2) {
        printf("USAGE: ./a.out PORT\n");
        exit(1);
    }

    const char *Host = "localhost"; 
    const char *Port = argv[1];
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
#ifndef NDEBUG            
            printf("client %d\n", client_addr_ip4->sin_addr.s_addr);
#endif
        } else {
            /* not an IPv4 address */
        }



    }






    return 0;
}