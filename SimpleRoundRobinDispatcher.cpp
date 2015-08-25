#include "SimpleRoundRobinDispatcher.h"

SimpleRoundRobinDispatcher::SimpleRoundRobinDispatcher(std::vector<Host>* hosts): AbstractDispatcher(hosts) {
    m_readCount.store(0);
    int thread_count = 20;
    for (int i = 1; i <= thread_count; ++i) {
        m_threads.emplace_back(&SimpleRoundRobinDispatcher::execute, this);
    }
};

SimpleRoundRobinDispatcher::~SimpleRoundRobinDispatcher() {};

void SimpleRoundRobinDispatcher::execute() {
    std::unique_ptr<HttpResponse> response;
    Host* host;

    while (1) {
        std::unique_lock<std::mutex> lck(m_queue_mtx);
        //std::cout << "waiting" << std::endl;
        while (m_parsedRequests.empty()) m_queue_cv.wait(lck);
        //std::cout << "notified" << std::endl;
        m_requestTuple_t request = m_parsedRequests.front();
        m_parsedRequests.pop();
        lck.unlock();
        
        host = &(m_hosts->at(request.host));
        debug("Request send to host %s:%d", host->getUrl().c_str(), host->getPort());
        response = host->executeRequest(request.request);

        char *buf;
        char http_response[] = "HTTP/1.1 200 OK\r\nContent-Length: %d\r\nConnection: Keep-Alive\r\n\r\n%s";
        if (response) {
        	asprintf(&buf, http_response, response->getContentLength(), response->getContent());
        } else {
	        asprintf(&buf, http_response, 0, "");
        }
        send(request.socket, buf, strlen(buf), 0);
        free(buf);

        close(request.socket);
        debug("Closed socket");
    }
}

int SimpleRoundRobinDispatcher::parseQuery(std::unique_ptr<Json::Value> query) {
    bool writeQuery = false;
    Json::Value operators;
    Json::Value obj_value(Json::objectValue);
    
    if (!query->isObject() || query->isMember("operators") == false) {
        std::cerr << "query content does not contain any operators";
        return -1;
    }
    operators = query->get("operators", obj_value);

    for (auto op: operators) {
        if (op.get("type", "").asString() == "InsertScan") writeQuery = true;
        if (op.get("type", "").asString() == "TableLoad") return 2;
    }

    if (writeQuery) return 1;

    return 0;
}

void SimpleRoundRobinDispatcher::dispatchQuery(HttpRequest& request, int sock, std::unique_ptr<Json::Value> query) {
    std::unique_ptr<HttpResponse> response;
    unsigned int host_id, counter;
    Host* host;
    std::unique_lock<std::mutex> lck(m_queue_mtx);

    debug("dispatch query");
    int readQuery = parseQuery(std::move(query));
    switch (readQuery) {
    case 0:
        counter = m_readCount.fetch_add(1);
        //avoid numeric overflow, reset read count after half of unsigned int range queries
        if (counter == m_boundary)
            m_readCount.fetch_sub(m_boundary);
        host_id = counter % m_hosts->size();

        m_parsedRequests.emplace(request, host_id, sock);
        m_queue_cv.notify_one();
        
        break;
    case 1:
        m_parsedRequests.emplace(request, 0, sock);
        m_queue_cv.notify_one();
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

void SimpleRoundRobinDispatcher::dispatchProcedure(HttpRequest& request, int sock) {
    std::unique_ptr<HttpResponse> response;
    Host* host;

    debug("dispatch procedure");

    host = &(m_hosts->at(0));
    response = host->executeRequest(request);
    
    char *buf;
    char http_response[] = "HTTP/1.1 200 OK\r\nContent-Length: %d\r\nConnection: Keep-Alive\r\n\r\n%s";
    if (response) {
    	asprintf(&buf, http_response, response->getContentLength(), response->getContent());
    } else {
	    asprintf(&buf, http_response, 0, "");
    }
    send(sock, buf, strlen(buf), 0);
    free(buf);
    close(sock);

    debug("Closed socket");
}

void SimpleRoundRobinDispatcher::dispatch(HttpRequest& request, int sock) {
    std::unique_ptr<HttpResponse> response;
    unsigned int host_id;
    Host* host;

    debug("dispatch");

    host = &(m_hosts->at(0));
    response = host->executeRequest(request);
    
    char *buf;
    char http_response[] = "HTTP/1.1 200 OK\r\nContent-Length: %d\r\nConnection: Keep-Alive\r\n\r\n%s";
    if (response) {
    	asprintf(&buf, http_response, response->getContentLength(), response->getContent());
    } else {
	asprintf(&buf, http_response, 0, "");
    }
    send(sock, buf, strlen(buf), 0);
    free(buf);
    close(sock);
    debug("Closed socket");
}
