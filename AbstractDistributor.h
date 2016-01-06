#ifndef ABSTRACT_DISTRIBUTOR_H_
#define ABSTRACT_DISTRIBUTOR_H_

#define READ 0
#define WRITE 1
#define LOAD 2

#include <vector>

#include "HttpRequest.h"
#include "Host.h"
#include "jsoncpp/json.h"

class AbstractDistributor {
public:
    AbstractDistributor(std::vector<Host> *hosts) {
        cluster_nodes = hosts;
    };
    int queryType(std::unique_ptr<Json::Value> query);
    void dispatch(HttpRequest& request, int sock);
    virtual void sendToMaster(HttpRequest& request, int sock) = 0;
    virtual void sendToAll(HttpRequest& request, int sock) = 0;
    virtual void distribute(HttpRequest& request, int sock) = 0;
    
protected:
    std::vector<Host> *cluster_nodes;
};

#endif
