
#ifndef DISPATCHER_H_
#define DISPATCHER_H_

#include "AbstractDistributor.h"

#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <vector>
#include <sys/types.h>
#include <sys/socket.h>

struct Request {
    int socket;
    struct sockaddr addr;
    socklen_t addrlen;
};

void Request_free(struct Request *request);

class Dispatcher {
private:
    std::queue<struct Request*> request_queue;
    std::mutex request_queue_mutex;
    std::condition_variable request_queue_empty;


    char *port;

    int thread_pool_size;
    std::vector<std::thread> parser_thread_pool;

    AbstractDistributor *distributor;

    Dispatcher( const Dispatcher& other ); // non construction-copyable
    Dispatcher& operator=( const Dispatcher& ); // non copyable
    
public:
    Dispatcher(char *port, char *settings_file);
    void dispatch_requests(int id);
    int create_socket();
    void start();
    void shut_down();
};

#endif  // DISPATCHER_H_
