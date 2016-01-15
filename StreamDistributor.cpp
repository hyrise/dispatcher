#include "StreamDistributor.h"
#include "dbg.h"
#include <sys/socket.h>
#include <unistd.h>

StreamDistributor::StreamDistributor(std::vector<struct Host*> *hosts): AbstractDistributor(hosts) {
    int thread_count = 4;
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

    while (1) {
        std::unique_lock<std::mutex> lck(m_read_queue_mtx);
        while (m_parsedReads.empty()) m_read_queue_cv.wait(lck);
        struct RequestTuple *request_tuple = m_parsedReads.front();
        m_parsedReads.pop();
        lck.unlock();

        host = cluster_nodes->at(host_id);
        debug("Request send to host %s:%d", host->url, host->port);
        response = executeRequest(host, request_tuple->request);

        sendResponse(response, request_tuple->socket);
    }
}

void StreamDistributor::executeWrite() {
    struct HttpResponse *response;
    struct Host *host;

    while (1) {
        std::unique_lock<std::mutex> lck(m_write_queue_mtx);
        //std::cout << "waiting" << std::endl;
        while (m_parsedWrites.empty()) m_write_queue_cv.wait(lck);
        //std::cout << "notified" << std::endl;
        struct RequestTuple *request_tuple = m_parsedWrites.front();
        m_parsedWrites.pop();
        lck.unlock();
        
        host = cluster_nodes->at(0);
        debug("Request send to host %s:%d", host->url, host->port);
        response = executeRequest(host, request_tuple->request);

        sendResponse(response, request_tuple->socket);
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


void StreamDistributor::sendToAll(struct HttpRequest *request, int sock) {
    debug("Load table.");
    for (struct Host *host : *cluster_nodes) {
        struct HttpResponse *response = executeRequest(host, request);
    }
    close(sock);
    // TODO send response
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


void StreamDistributor::sendResponse(struct HttpResponse *response, int sock) {
    char *buf;
    int allocatedBytes;
    char http_response[] = "HTTP/1.1 200 OK\r\nContent-Length: %d\r\nConnection: Keep-Alive\r\n\r\n%s";
    if (response) {
        allocatedBytes = asprintf(&buf, http_response, response->content_length, response->payload);
    } else {
        allocatedBytes = asprintf(&buf, http_response, 0, "");
    }
    if (allocatedBytes == -1) {
        log_err("An error occurred while creating response.");
        send(sock, error_response, strlen(error_response), 0);
        close(sock);
        return;
    }
    send(sock, buf, strlen(buf), 0);
    free(buf);
    close(sock);
    debug("Closed socket");
}
