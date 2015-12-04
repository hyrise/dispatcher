#ifndef ABSTRACT_DISTRIBUTOR_H_
#define ABSTRACT_DISTRIBUTOR_H_

#include <vector>
#include <memory>

#include "HttpRequest.h"
#include "Host.h"
#include "jsoncpp/json.h"

class AbstractDistributor {
public:
    AbstractDistributor(std::vector<Host> *hosts) {
        cluster_nodes = hosts;
    };
    virtual void dispatch(HttpRequest& request, int sock) = 0;
    virtual void dispatchQuery(HttpRequest& request, int sock, std::unique_ptr<Json::Value> query) = 0;
    virtual void dispatchProcedure(HttpRequest& request, int sock) = 0;
    virtual void notify(std::string message) {}
protected:
    std::vector<Host> *cluster_nodes;
};

#endif
