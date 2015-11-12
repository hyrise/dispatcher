#include "AbstractDistributor.h"

#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class Dispatcher
{
private:
    std::queue<int> request_queue;
    std::mutex request_queue_mutex;

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
