#ifndef ABSTRACT_DISPATCHER_H_
#define ABSTRACT_DISPATCHER_H_

#include <vector>

#include "HttpRequest.h"
#include "Host.h"

class AbstractDispatcher {
public:
    AbstractDispatcher(std::vector<Host>* hosts) : m_hosts(hosts) {};
    virtual void dispatch(HttpRequest& request, int sock) = 0;
protected:
    std::vector<Host>* m_hosts;
};

#endif
