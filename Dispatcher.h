
#ifndef DISPATCHER_H_
#define DISPATCHER_H_

#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <map>
#include <sys/types.h>
#include <sys/socket.h>


#define READ 0
#define WRITE 1
#define LOAD 2

struct request_parser_data;

class Dispatcher {
private:
    char *port;

    std::vector<struct Host*> *cluster_nodes;
    std::mutex cluster_nodes_mutex;

    Dispatcher( const Dispatcher& other ); // non construction-copyable
    Dispatcher& operator=( const Dispatcher& ); // non copyable
    
public:
    Dispatcher(char *port, char *settings_file);
    void start();
    void set_master(const char *url, int port);
    void add_host(const char *url, int port);
    int remove_host(const char *url, int port);
    void handle_connection(int client_socket);
    void send_to_db_node(struct HttpRequest *request, std::map<std::string, int> *connections,
                         int node_offset, struct HttpResponse **response_ref);
    void send_to_all(struct HttpRequest *request, struct HttpResponse **response_ref);
    void get_node_info(char **dynamic_generated_payload);

    void send_to_db_node_async(struct request_parser_data *data, int node_offset);
};

#endif  // DISPATCHER_H_
