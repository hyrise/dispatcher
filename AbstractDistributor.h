#ifndef ABSTRACT_DISTRIBUTOR_H_
#define ABSTRACT_DISTRIBUTOR_H_

#define READ 0
#define WRITE 1
#define LOAD 2

#include <vector>

#include "http.h"
#include "Host.h"
#include "jsoncpp/json.h"

class AbstractDistributor {
public:
    AbstractDistributor(std::vector<Host> *hosts) {
        cluster_nodes = hosts;
    };
    int queryType(char *http_payload);
    void dispatch(struct HttpRequest *request, int sock);
    virtual void sendToMaster(struct HttpRequest *request, int sock) = 0;
    virtual void sendToAll(struct HttpRequest *request, int sock) = 0;
    virtual void distribute(struct HttpRequest *request, int sock) = 0;
    
protected:
    std::vector<Host> *cluster_nodes;
};

#endif
