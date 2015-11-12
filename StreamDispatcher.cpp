#include "StreamDispatcher.h"

StreamDispatcher::StreamDispatcher(std::vector<Host> *hosts): AbstractDispatcher(hosts) {
    int thread_count = 4;
    for (unsigned int j = 0; j < hosts->size(); j++)
        for (int i = 1; i <= thread_count; ++i) {
            if (j == 0 and i == 1)
                m_threads.emplace_back(&StreamDispatcher::executeWrite, this);
            else
                m_threads.emplace_back(&StreamDispatcher::executeRead, this, j);
        }
};

StreamDispatcher::~StreamDispatcher() {};

void StreamDispatcher::executeRead(int host_id) {
    std::unique_ptr<HttpResponse> response;
    Host* host;

    while (1) {
        std::unique_lock<std::mutex> lck(m_read_queue_mtx);
        while (m_parsedReads.empty()) m_read_queue_cv.wait(lck);
        m_requestTuple_t request = m_parsedReads.front();
        m_parsedReads.pop();
        lck.unlock();

        host = &(m_hosts->at(host_id));
        debug("Request send to host %s:%d", host->getUrl().c_str(), host->getPort());
        response = host->executeRequest(request.request);

        sendResponse(std::move(response), request.socket);
    }
}

void StreamDispatcher::executeWrite() {
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
        
        host = &(m_hosts->at(0));
        debug("Request send to host %s:%d", host->getUrl().c_str(), host->getPort());
        response = host->executeRequest(request.request);

        sendResponse(std::move(response), request.socket);
    }
}

int StreamDispatcher::parseQuery(std::unique_ptr<Json::Value> query) {
    bool writeQuery = false;
    Json::Value operators;
    Json::Value obj_value(Json::objectValue);
    
    if (!query->isObject() || query->isMember("operators") == false) {
        std::cerr << "query content does not contain any operators";
        return -1;
    }
    operators = query->get("operators", obj_value);

    for (auto op: operators) {
        auto type = op.get("type", "").asString();
        if (type == "InsertScan" or type == "Delete" or type == "PosUpdateIncrementScan" or type == "PosUpdateScan") writeQuery = true;
        if (type == "TableLoad") return 2;
    }

    if (writeQuery) return 1;

    return 0;
}

void StreamDispatcher::dispatchQuery(HttpRequest& request, int sock, std::unique_ptr<Json::Value> query) {
    std::unique_ptr<HttpResponse> response;
    std::unique_lock<std::mutex> lck;

    debug("dispatch query");
    int readQuery = parseQuery(std::move(query));
    if (request.hasDecodedContent("oltp") and request.getDecodedContent("oltp") == "true")
        readQuery = 1;
    switch (readQuery) {
    case 0:
        lck = std::unique_lock<std::mutex>(m_read_queue_mtx);
        m_parsedReads.emplace(request, 0, sock);
        m_read_queue_cv.notify_one();
        
        break;
    case 1:
        lck = std::unique_lock<std::mutex>(m_write_queue_mtx);
        m_parsedWrites.emplace(request, 0, sock);
        m_write_queue_cv.notify_one();
        break;
    case 2:
        for (Host host : *m_hosts) {
            response = host.executeRequest(request);
        }
        close(sock);
        debug("load table");
        break;
    default:
        return;
    }
}

void StreamDispatcher::dispatchProcedure(HttpRequest& request, int sock) {
    debug("dispatch procedure");

    std::unique_lock<std::mutex> lck(m_write_queue_mtx);
    m_parsedWrites.emplace(request, 0, sock);
    m_write_queue_cv.notify_one();
}

void StreamDispatcher::dispatch(HttpRequest& request, int sock) {
    debug("dispatch");

    std::unique_lock<std::mutex> lck(m_write_queue_mtx);
    m_parsedWrites.emplace(request, 0, sock);
    m_write_queue_cv.notify_one();
}

void StreamDispatcher::sendResponse(std::unique_ptr<HttpResponse> response, int sock) {
    char *buf;
    int allocatedBytes;
    char http_response[] = "HTTP/1.1 200 OK\r\nContent-Length: %d\r\nConnection: Keep-Alive\r\n\r\n%s";
    if (response) {
    	allocatedBytes = asprintf(&buf, http_response, response->getContentLength(), response->getContent());
    } else {
	    allocatedBytes = asprintf(&buf, http_response, 0, "");
    }
    if (allocatedBytes == -1) {
        std::cerr << "An error occurred while creating response" << std::endl;
        send(sock, error_response, strlen(error_response), 0);
        close(sock);
        return;
    }
    send(sock, buf, strlen(buf), 0);
    free(buf);
    close(sock);
    debug("Closed socket");
}
