#include "StreamDistributor.h"
#include "dbg.h"

StreamDistributor::StreamDistributor(std::vector<Host> *hosts): AbstractDistributor(hosts) {
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
    std::unique_ptr<HttpResponse> response;
    Host* host;

    while (1) {
        std::unique_lock<std::mutex> lck(m_read_queue_mtx);
        while (m_parsedReads.empty()) m_read_queue_cv.wait(lck);
        m_requestTuple_t request = m_parsedReads.front();
        m_parsedReads.pop();
        lck.unlock();

        host = &(cluster_nodes->at(host_id));
        debug("Request send to host %s:%d", host->getUrl().c_str(), host->getPort());
        response = host->executeRequest(request.request);

        sendResponse(std::move(response), request.socket);
    }
}

void StreamDistributor::executeWrite() {
    std::unique_ptr<HttpResponse> response;
    Host* host;

    while (1) {
        std::unique_lock<std::mutex> lck(m_write_queue_mtx);
        //std::cout << "waiting" << std::endl;
        while (m_parsedWrites.empty()) m_write_queue_cv.wait(lck);
        //std::cout << "notified" << std::endl;
        m_requestTuple_t request = m_parsedWrites.front();
        m_parsedWrites.pop();
        lck.unlock();
        
        host = &(cluster_nodes->at(0));
        debug("Request send to host %s:%d", host->getUrl().c_str(), host->getPort());
        response = host->executeRequest(request.request);

        sendResponse(std::move(response), request.socket);
    }
}


void StreamDistributor::distribute(HttpRequest& request, int sock) {
    std::unique_ptr<HttpResponse> response;
    std::unique_lock<std::mutex> lck;

    lck = std::unique_lock<std::mutex>(m_read_queue_mtx);
    m_parsedReads.emplace(request, 0, sock);
    m_read_queue_cv.notify_one();
}


void StreamDistributor::sendToAll(HttpRequest& request, int sock) {
    debug("Load table.");
    for (Host host : *cluster_nodes) {
        std::unique_ptr<HttpResponse> response = host.executeRequest(request);
    }
    close(sock);
    // TODO send response
}


void StreamDistributor::sendToMaster(HttpRequest& request, int sock) {
    debug("Dispatch procedure.");

    std::unique_lock<std::mutex> lck(m_write_queue_mtx);
    m_parsedWrites.emplace(request, 0, sock);
    m_write_queue_cv.notify_one();
}


void StreamDistributor::sendResponse(std::unique_ptr<HttpResponse> response, int sock) {
    char *buf;
    int allocatedBytes;
    char http_response[] = "HTTP/1.1 200 OK\r\nContent-Length: %d\r\nConnection: Keep-Alive\r\n\r\n%s";
    if (response) {
    	allocatedBytes = asprintf(&buf, http_response, response->getContentLength(), response->getContent());
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
