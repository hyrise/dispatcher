#include "SimpleRoundRobinDispatcher.h"

SimpleRoundRobinDispatcher::SimpleRoundRobinDispatcher(std::vector<Host>* hosts): AbstractDispatcher(hosts) {
    m_readCount.store(0);
};

SimpleRoundRobinDispatcher::~SimpleRoundRobinDispatcher() {};

void SimpleRoundRobinDispatcher::dispatch(HttpRequest& request, int sock) {
    std::unique_ptr<HttpResponse> response;
    Parser p = Parser();
    unsigned int host_id;
    Host* host;
    int readQuery = p.queryType(request.getDecodedContent());
    switch (readQuery) {
    case 0:
        host_id = m_readCount.fetch_add(1) % m_hosts->size();
        host = &(m_hosts->at(host_id));
        response = host->executeRequest(request);
#ifdef DEBUG
		std::cout << "read on host " << host->getUrl() << ":" << host->getPort() << std::endl;
#endif
        break;
    case 1:
        host = &(m_hosts->at(0));
        response = host->executeRequest(request);
#ifdef DEBUG
		std::cout << "write" << std::endl;
#endif
        break;
    case 2:
        for (Host host : *m_hosts) {
            response = host.executeRequest(request);
#ifdef DEBUG
		std::cout << "load" << std::endl;
#endif
        }
        break;
    default:
        return;
    }
    
    char *buf;
    char http_response[] = "HTTP/1.1 200 OK\r\nContent-Length: %d\r\nConnection: Keep-Alive\r\n\r\n%s";
    asprintf(&buf, http_response, response->getContentLength(), response->getContent());
    send(sock, buf, strlen(buf), 0);
    free(buf);
    close(sock);
#ifdef DEBUG
    std::cout << "Closed socket " << std::endl;
#endif
}
