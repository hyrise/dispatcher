#ifndef ABSTRACT_DISTRIBUTOR_H_
#define ABSTRACT_DISTRIBUTOR_H_


#include <vector>
extern "C"
{
#include "http.h"
}

class AbstractDistributor {
	friend class Dispatcher;
public:
    AbstractDistributor(std::vector<struct Host*> *hosts) {
        cluster_nodes = hosts;
    };
    int queryType(char *http_payload);
    virtual void sendToMaster(struct HttpRequest *request, int sock) = 0;
    virtual void distribute(struct HttpRequest *request, int sock) = 0;
    
protected:
    std::vector<struct Host*> *cluster_nodes;
};

#endif
