#ifndef STREAM_DISPATCHER_H_
#define STREAM_DISPATCHER_H_

#include <iostream>
#include <atomic>
#include <memory>
#include <limits>
#include <thread>
#include <mutex>
#include <chrono>
#include <condition_variable>
#include <queue>

#include "AbstractDispatcher.h"
#include "HttpResponse.h"
#include "dbg.h"

class StreamDispatcher : public AbstractDispatcher {
public:
    StreamDispatcher(std::vector<Host>* hosts);
    ~StreamDispatcher();

    virtual void dispatch(HttpRequest& request, int sock);
    virtual void dispatchQuery(HttpRequest& request, int sock, std::unique_ptr<Json::Value> query);
    virtual void dispatchProcedure(HttpRequest& request, int sock);

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

    struct m_requestTuple_t {
        HttpRequest& request;
        int host;
        int socket;
        m_requestTuple_t(HttpRequest& request, int host, int socket) : request(request), host(host), socket(socket)
        {
        }
    };
    std::queue<m_requestTuple_t> m_parsedReads;
    std::queue<m_requestTuple_t> m_parsedWrites;

    int parseQuery(std::unique_ptr<Json::Value> query);
    void sendResponse(std::unique_ptr<HttpResponse> response, int sock);
};

#endif
