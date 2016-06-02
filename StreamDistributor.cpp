#include "StreamDistributor.h"
#include "dbg.h"
#include <sys/socket.h>
#include <unistd.h>
#include <sys/time.h>



unsigned timediff2(struct timeval start,  struct timeval stop) {
    return (stop.tv_usec - start.tv_usec) + (stop.tv_sec - start.tv_sec) * 1000 * 1000;
}


StreamDistributor::StreamDistributor(std::vector<struct Host*> *hosts): AbstractDistributor(hosts) {
    int thread_count = 10;
    for (unsigned int j = 0; j < hosts->size(); j++)
        for (int i = 1; i <= thread_count; ++i) {
            if (j == 0 and i == 1)
                m_threads.emplace_back(&StreamDistributor::executeWrite, this);
            else
                m_threads.emplace_back(&StreamDistributor::executeRead, this, j);
        }
};

StreamDistributor::~StreamDistributor() {};

void StreamDistributor::executeRead(int host_id) {
    struct HttpResponse *response;
    struct Host *host;
    struct timeval query_start, query_end;

    while (1) {
        struct RequestTuple *request_tuple;
        {
            std::unique_lock<std::mutex> lck(m_read_queue_mtx);
            while (m_parsedReads.empty()) {
                m_read_queue_cv.wait(lck);
            }
            request_tuple = m_parsedReads.front();
            m_parsedReads.pop();
        }

        host = cluster_nodes->at(host_id);
        debug("Request send to host %s:%d", host->url, host->port);

        gettimeofday(&query_start, NULL);
        response = executeRequest(host, request_tuple->request);
        gettimeofday(&query_end, NULL);
        (host->total_queries)++;
        host->total_time += (unsigned int)timediff2(query_start, query_end);

        http_send_response(request_tuple->socket, response);
        HttpResponse_free(response);
        close(request_tuple->socket);
    }
}

void StreamDistributor::executeWrite() {
    struct HttpResponse *response;
    struct Host *host;

    while (1) {
        struct RequestTuple *request_tuple;
        {
            std::unique_lock<std::mutex> lck(m_write_queue_mtx);
            while (m_parsedWrites.empty()) {
                m_write_queue_cv.wait(lck);
            }
            request_tuple = m_parsedWrites.front();
            m_parsedWrites.pop();
        }
        
        host = cluster_nodes->at(0);
        debug("Request send to host %s:%d", host->url, host->port);
        response = executeRequest(host, request_tuple->request);

        http_send_response(request_tuple->socket, response);
        HttpResponse_free(response);
        close(request_tuple->socket);
    }
}


void StreamDistributor::distribute(struct HttpRequest *request, int sock) {
    std::unique_ptr<HttpResponse> response;
    std::unique_lock<std::mutex> lck;

    lck = std::unique_lock<std::mutex>(m_read_queue_mtx);

    struct RequestTuple *request_tuple = new RequestTuple();
    request_tuple->request = request;
    request_tuple->host = 0;
    request_tuple->socket = sock;

    m_parsedReads.push(request_tuple);
    m_read_queue_cv.notify_one();
}


void StreamDistributor::sendToMaster(struct HttpRequest *request, int sock) {
    debug("Dispatch procedure.");
    std::unique_lock<std::mutex> lck(m_write_queue_mtx);

    struct RequestTuple *request_tuple = new RequestTuple();
    request_tuple->request = request;
    request_tuple->host = 0;
    request_tuple-> socket = sock;

    m_parsedWrites.push(request_tuple);
    m_write_queue_cv.notify_one();
}

