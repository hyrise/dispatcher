#ifndef ROUND_ROBIN_DISTRIBUTOR_H_
#define ROUND_ROBIN_DISTRIBUTOR_H_

#include <iostream>
#include <atomic>
#include <memory>
#include <limits>
#include <thread>
#include <mutex>
#include <chrono>
#include <condition_variable>
#include <queue>

#include "AbstractDistributor.h"
#include "HttpResponse.h"

class RoundRobinDistributor: public AbstractDistributor {
public:
    RoundRobinDistributor(std::vector<Host> *hosts);
    ~RoundRobinDistributor();

    virtual void dispatch(HttpRequest& request, int sock);
    virtual void dispatchQuery(HttpRequest& request, int sock, std::unique_ptr<Json::Value> query);
    virtual void dispatchProcedure(HttpRequest& request, int sock);

    void execute();
private:
    std::atomic<unsigned int> read_counter;
    const unsigned int m_boundary = std::numeric_limits<unsigned int>::max() / 2;
    std::vector<std::thread> thread_pool;
    std::mutex m_queue_mtx;
    std::condition_variable m_queue_cv;

    const char* error_response = "HTTP/1.1 500 ERROR\r\n\r\n";

    struct m_requestTuple_t {
        HttpRequest& request;
        int host;
        int socket;
        m_requestTuple_t(HttpRequest& request, int host, int socket) : request(request), host(host), socket(socket)
        {
        }
    };
    std::queue<m_requestTuple_t> m_parsedRequests;

    int parseQuery(std::unique_ptr<Json::Value> query);
    void sendResponse(std::unique_ptr<HttpResponse> response, int sock);
};

#endif
