#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>


#define MAXPENDING 5
#define BUFFERSIZE 65535

#define MAX_THREADS 20000
#define MAX_SOCKETS 20000

#define READ_QUERY 1
#define WRITE_QUERY 2
#define NEW_MASTER 3
#define SET_SLAVES_1 4
#define SET_SLAVES_2 5
#define SET_SLAVES_3 6
#define STATISTICS 7
#define DELAY_QUERY 8

#define ANZAHL_HOSTS 4


pthread_mutex_t global_lock;
int query_nr = 0;   // used to schedule queries in round robin manner
size_t number_of_read_queries;
size_t number_of_write_queries;
int dispatcher_socket = 0;
const char HOSTS[ANZAHL_HOSTS][2][16] = {{"127.0.0.1", "5000"}, {"127.0.0.1", "5001"}, {"127.0.0.1", "5002"}, {"127.0.0.1", "5003"}};

int slaves = 0;
int openSockets[MAX_SOCKETS];
int num_opensockets = 0;

int current_master = 0; 
int active_hosts[ANZAHL_HOSTS] = {0, 1, 2, 3};
// int active_hosts_num = ANZAHL_HOSTS;
int active_hosts_num = 1;

int failoverdone = 0;

char NotImpl[] = "HTTP/1.1 501 Not Implemented\n";

char answer[] = "HTTP/1.0 200 OK\n\
Content-Type: text/html\n\
Content-Length: 155\r\n\r\n\
Supported Operations:\n\
POST /procedureRevenueSelect/ JSON_QUERY\n\
POST /delay DELAY_QUERY\n\
POST /procedureRevenueInsert/ JSON_WRITE\n\
POST /new_master MASTER_IP MASTER_PORT\n\
POST /number_of_slaves NUMBER_OF_SLAVES\n";

char http_post[] = "POST %s HTTP/1.1\r\n\
Content-Length: %d\r\n\
Connection: Keep-Alive\r\n\r\n\
%s";

char http_response[] = "HTTP/1.1 200 OK\r\n\
Content-Length: %d\r\n\
Connection: Keep-Alive\r\n\r\n\
%s";

char answer2[] = "HTTP/1.1 204 No Content\r\n\r\n";
char answer3[] = "HTTP/1.1 500 ERROR\r\n\r\n";


void add_opensocket(int socket) {
    openSockets[num_opensockets] = socket;
    ++num_opensockets;
}

void term(int signum)
{
    printf("Exiting...\n");

    int i = 0;
    while (i < num_opensockets) {
        close(openSockets[i]);
        ++i;
    }
    printf("Closed %d sockets.\n", i);
    fflush(stdout);
}

int get_socket(int host_nr) {

#ifndef NDEBUG
    printf("get socket for host %d\n", host_nr);
#endif  

    int port = atoi(HOSTS[host_nr][1]);

    int sockfd;
    struct sockaddr_in dest;

    if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
        printf("ERROR: could not create a socket\n");
    }
    add_opensocket(sockfd);

    /*---Initialize server address/port struct---*/
    bzero(&dest, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = inet_addr(HOSTS[host_nr][0]);
    dest.sin_port = htons(port);

    /*---Connect to server---*/
    if ( connect(sockfd, (struct sockaddr*)&dest, sizeof(dest)) != 0 ) {
        printf("ERROR: could not connect to host\n");
    }
    fflush(stdout);
    return sockfd;

}


char *strnstr_(const char *haystack, const char *needle, size_t len_haystack, size_t len_needle) {
    if (len_haystack == 0) return (char *)haystack; /* degenerate edge case */
    if (len_needle == 0) return (char *)haystack; /* degenerate edge case */
    while ((haystack = strchr(haystack, needle[0]))) {
        if (!strncmp(haystack, needle, len_needle)) return (char *)haystack;
        haystack++; }
    return NULL;
}


int get_content_lenght1(const char *buf, const int size, const char *lengthname) {
    const char *hit_ptr;
    int content_length;
    hit_ptr = strnstr_(buf, lengthname, size, 15);
    if (hit_ptr == NULL) {
        return -1;
    }
    char format [50];
    strcpy(format,lengthname);
    strcat(format," %d");
    fflush(stdout);
    if (sscanf(hit_ptr, format, &content_length) != 1) {
        return -1;
    }
    return content_length;
}

int get_content_lenght(const char *buf, const int size) {
    int res = get_content_lenght1(buf, size, "Content-Length:");
    if (res == -1)
        res = get_content_lenght1(buf, size, "Content-length:");
        if (res == -1)
            res = get_content_lenght1(buf, size, "content-length:");
    return res;
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
            hit_ptr = strnstr_(buf, "\n", *offset, 1);
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
                if (strcmp(recource, "/procedureRevenueSelect/") == 0)
                    *action = READ_QUERY;
                else if (strcmp(recource, "/procedureRevenueInsert/") == 0)
                    *action = WRITE_QUERY;
                else if (strcmp(recource, "/new_master") == 0)
                    *action = NEW_MASTER;
                else if (strcmp(recource, "/number_of_slaves_1") == 0)
                    *action = SET_SLAVES_1;
                else if (strcmp(recource, "/number_of_slaves_2") == 0)
                    *action = SET_SLAVES_2;
                else if (strcmp(recource, "/number_of_slaves_3") == 0)
                    *action = SET_SLAVES_3;
                else if (strcmp(recource, "/statistics") == 0)
                    *action = STATISTICS;
                else if (strcmp(recource, "/delay") == 0)
                    *action = DELAY_QUERY;
                else {
                    printf("ERROR! Unkown action! Resource: %s\n", recource);
                }
#ifndef NDEBUG                
                printf("Resource: %s -> Action: %d\n", recource, *action);
#endif

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
            hit_ptr = strnstr_(buf, "\r\n\r\n", *offset, 4);
            http_body_start = hit_ptr + 4;
            if (hit_ptr == NULL) {
                printf("ERROR: not FOUND\n");
                continue;
            }
            header_received = 1;
            // header delimiter reached
            *length = get_content_lenght(buf, *offset);
            if (*length == -1)
            {
                printf("ERROR: Could not read content length!\n");
                printf("%s\n", buf);
                return -1;
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
        fflush(stdout);
    }
#ifndef NDEBUG
    printf("End of reception\n");
#endif
    fflush(stdout);
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
            hit_ptr = strnstr_(buf, "\n", *offset, 1);
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
	    if (*status != 200) {
	      printf("Status: %d\n", *status);
	      return -1;
	    }
        }

        // first line received and checked successfully
        // requested action was set
        // check for content next
        if (!header_received) {
            char *hit_ptr;
            hit_ptr = strnstr_(buf, "\r\n\r\n", *offset, 4);
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
        fflush(stdout);
    }

#ifndef NDEBUG
    printf("End of reception\n");
#endif

    if (recv_size <= 0) {
        fprintf(stderr, "ERROR while receiving data\n");
        return -1;
    }

    if (*content == NULL) {
        return -1;
    }
    return 0;
}


int handle_request(int sock, int action, char *content, int content_length, int *socket_list) {

#ifndef NDEBUG    
    printf ("handle_request\n");
    printf("ACTION: %d\n", action);
#endif

    int new_number_of_slaves = -1;
    char* slaves_num_char = "0";
    int i=0;
    switch (action) {
    case READ_QUERY: {
        // pthread_mutex_lock(&global_lock);

        int r = query_nr % active_hosts_num;

        query_nr++;
        // pthread_mutex_unlock(&global_lock);
#ifndef NDEBUG
        printf("Query sent to host %d\n", r);
#endif
        //int socketfd = get_socket(r);
        int socketfd = socket_list[active_hosts[r]];

        char *buf;
        asprintf(&buf, http_post, "/procedureRevenueSelect/", content_length, content);
        send(socketfd, buf, strlen(buf), 0);
        free(buf);

        int res_content_length = 0;
        char res_buf[BUFFERSIZE];
        char *res_content;
        int offset = 0;
        int status = 0;

        int res = get_response(socketfd, (char *)res_buf, &offset, &status, &res_content, &res_content_length);
        if (res != -1) {
	  	    __sync_add_and_fetch(&number_of_read_queries, 1);
        }
        else {
            printf("ERROR: get_response: %d\n", res);
        }

        asprintf(&buf, http_response, res_content_length, res_content);

        send(sock, buf, strlen(buf), 0);
        free(buf);

        break;
    }
    case WRITE_QUERY: {
        //int socketfd = get_socket(current_master);
        int socketfd = socket_list[current_master];
#ifndef NDEBUG
        printf("send write to master: %d\n", current_master);
#endif
        char *buf;
        asprintf(&buf, http_post, "/procedureRevenueInsert/", content_length, content);
        send(socketfd, buf, strlen(buf), 0);
        free(buf);

        int res_content_length = 0;
        char res_buf[BUFFERSIZE];
        char *res_content;
        int offset = 0;
        int status = 0;

        if (get_response(socketfd, (char *)res_buf, &offset, &status, &res_content, &res_content_length) != -1)
	  __sync_add_and_fetch(&number_of_write_queries, 1);
        asprintf(&buf, http_response, res_content_length, res_content);

#ifndef NDEBUG
        printf("Sending response!\n%s\n", buf);
#endif


        send(sock, buf, strlen(buf), 0);

        free(buf);
        break;
    }
    case DELAY_QUERY: {

        int socketfd = socket_list[current_master];

#ifndef NDEBUG
        printf("send delay query to master: %d\n", current_master);
#endif
        char *buf;
        asprintf(&buf, http_post, "/query/", content_length, content);
        send(socketfd, buf, strlen(buf), 0);
        free(buf);

        int res_content_length = 0;
        char res_buf[BUFFERSIZE];
        char *res_content;
        int offset = 0;
        int status = 0;

        get_response(socketfd, (char *)res_buf, &offset, &status, &res_content, &res_content_length);
        asprintf(&buf, http_response, res_content_length, res_content);

#ifndef NDEBUG
        printf("Sending response!\n%s\n", buf);
#endif
        send(sock, buf, strlen(buf), 0);

        free(buf);
        break;
    }
    case NEW_MASTER:
      if(!failoverdone){
        printf("NEW MASTER INT TOWN!!!\n");
        printf("old master: %d\n", current_master);
        printf("old num hosts: %d\n", active_hosts_num);

        int tmp = active_hosts[0];
        active_hosts[0] = active_hosts[active_hosts_num-1];
        active_hosts[active_hosts_num-1] = tmp;
        active_hosts_num = active_hosts_num-1;
        current_master = 1;


        printf("new master: %d\n", current_master);
        printf("new num hosts: %d\n", active_hosts_num);

        printf("hosts:");
        while (i<active_hosts_num) {
            printf("%d.", active_hosts[i]);
            ++i;
        }
        printf("\n");

        send(sock, answer2, sizeof(answer2), 0);
	failoverdone = 1;
      } else {
        printf("ERROR: only supporting one time failover!\n");
        send(sock, NotImpl, sizeof(NotImpl), 0);
        // exit(1);
      }
        break;
    case SET_SLAVES_1:
        printf("SETSLAVES to 1\n");
        active_hosts_num = 2;
        send(sock, answer2, sizeof(answer2), 0);
        break;
    case SET_SLAVES_2:
        printf("SETSLAVES to 2\n");
        active_hosts_num = 3;
        send(sock, answer2, sizeof(answer2), 0);
        break;
    case SET_SLAVES_3:
        printf("SETSLAVES to 3\n");
        active_hosts_num = 4;
        send(sock, answer2, sizeof(answer2), 0);
        break;
    case STATISTICS:
        ;
        char *buf1;
	char *buf2;
	struct timeval tim;
	gettimeofday(&tim, NULL);
	char content[] = "{\"read\": %d, \"write\": %d, \"timestamp\": %f}\n";
	int length = asprintf(&buf1, content, number_of_read_queries, 
			      number_of_write_queries, tim.tv_sec+(tim.tv_usec/1000000.0)); 
	asprintf(&buf2, http_response, length, buf1);
	send(sock, buf2, strlen(buf2), 0);
	return -1;
	break;
    default:
        printf("ERROR: handle_request. Unkow ACTION.\n");
        break;
    }
    fflush(stdout);

    return 0;
}



void *new_connection(void *sock_p) {
    int sock = *(int *)sock_p;
    int action = 0;
    int content_length = 0;
    int offset = 0;
    char buf[BUFFERSIZE];
    char *content = NULL;

    int socket_list[ANZAHL_HOSTS];

    // initialize socket list. one socket for each host.
    int i=0;
    while (i < ANZAHL_HOSTS) {
        socket_list[i] = get_socket(i);
        ++i;
    }

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
            return 0;
        }
#ifndef NDEBUG
        printf("CONTENT OF: %s\n", content);
#endif
        if (handle_request(sock, action, content, content_length, socket_list) == -1) {
            close(sock);

            // close all connections to hosts
            int i=0;
            while (i < ANZAHL_HOSTS) {
                close(socket_list[i]);
                ++i;
            }

#ifndef NDEBUG
            printf("connection closed\n");
#endif
            return 0;
        }
    }
    close(sock);
    fflush(stdout);
}



int main(int argc, char const *argv[])
{
    if (argc != 2) {
        printf("USAGE: ./a.out PORT\n");
        exit(1);
    }

    // handle sigterm
    struct sigaction action;
    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = term;
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGABRT, &action, NULL);
    sigaction(SIGINT, &action, NULL);

    // ignore broken sockets
    signal(SIGPIPE, SIG_IGN);

    const char *Host = "0.0.0.0"; 
    const char *Port = argv[1];
    int n, errno;

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((n = getaddrinfo(Host, Port, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", strerror(n));
        return 1;
    }

    if ((dispatcher_socket = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0) {
        fprintf(stderr, "can't create socket: %s\n", strerror(errno));
        return 2;
    }
    add_opensocket(dispatcher_socket);

    if (bind(dispatcher_socket, res->ai_addr, res->ai_addrlen) < 0) {
        close(dispatcher_socket);
        fprintf(stderr, "can't bind to socket: %s\n", strerror(errno));
        return 2;
    }

    if (listen(dispatcher_socket, MAXPENDING) < 0) {
        fprintf(stderr, "can't listen to socket: %s\n", strerror(errno));
        return 2;
    }

    socklen_t client_addrlen = sizeof(client_addrlen);
    struct sockaddr client_addr;

    pthread_t thread_ptrs[MAX_THREADS];
    int thread_nr = 0;
    int client_sockets[MAX_THREADS];

    if (pthread_mutex_init(&global_lock, NULL) != 0) {
        fprintf(stderr, "ERROR on mutex_init\n");
        exit(1);
    };

    printf("Dispatcher listening on port %s...\n", Port);
    fflush(stdout);

    while(1) {
        client_sockets[thread_nr] = accept(dispatcher_socket, &client_addr, &client_addrlen);
        
        if (client_sockets[thread_nr] < 0) {
            fprintf(stderr, "ERROR on accept\n");
            exit(1);
        }


        if (pthread_create(thread_ptrs + thread_nr, NULL, &new_connection, client_sockets + thread_nr) != 0) {
            fprintf(stderr, "ERROR: pthread_create failed()\n");
            exit(1);
        }
        thread_nr++;

        if (client_addr.sa_family == AF_INET){
            struct sockaddr_in *client_addr_ip4 = (struct sockaddr_in *) &client_addr;
#ifndef NDEBUG            
            printf("client %d\n", client_addr_ip4->sin_addr.s_addr);
#endif
        } else {
            /* not an IPv4 address */
        }
        fflush(stdout);
    }

    int i;
    for (i=0; i < thread_nr; i++) {
        pthread_join(thread_ptrs[i], NULL);
    }
    close(dispatcher_socket);
    pthread_mutex_destroy(&global_lock);

    return 0;
}
