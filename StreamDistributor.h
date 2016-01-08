#ifndef STREAM_DISTRIBUTOR_H_
#define STREAM_DISTRIBUTOR_H_

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

class StreamDistributor: public AbstractDistributor {
public:
    StreamDistributor(std::vector<Host> *hosts);
    ~StreamDistributor();

    virtual void sendToMaster(struct HttpRequest *request, int sock);
    virtual void sendToAll(struct HttpRequest *request, int sock);
    virtual void distribute(struct HttpRequest *request, int sock);

    void dispatch(struct HttpRequest *request, int sock);

    void executeRead(int host_id);
    void executeWrite();
private:
    std::vector<unsigned int> m_queryCount;
    std::vector<std::thread> m_threads;
    std::mutex m_read_queue_mtx;
    std::mutex m_write_queue_mtx;
    std::condition_variable m_write_queue_cv;
    std::condition_variable m_read_queue_cv;

    const char* error_response = "HTTP/1.1 500 ERROR\r\n\r\n";

    struct RequestTuple {
        struct HttpRequest *request;
        int host;
        int socket;

    };
    std::queue<struct RequestTuple*> m_parsedReads;
    std::queue<struct RequestTuple*> m_parsedWrites;

    int parseQuery(std::unique_ptr<Json::Value> query);
    void sendResponse(std::unique_ptr<HttpResponse> response, int sock);
};

#endif
