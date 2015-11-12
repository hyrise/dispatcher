#ifndef ABSTRACT_DISPATCHER_H_
#define ABSTRACT_DISPATCHER_H_

#include <vector>
#include <memory>

#include "HttpRequest.h"
#include "Host.h"
#include "jsoncpp/json.h"

class AbstractDispatcher {
public:
    AbstractDispatcher(std::vector<Host> *hosts)
    {
        m_hosts = hosts;
    };
    virtual void dispatch(HttpRequest& request, int sock) = 0;
    virtual void dispatchQuery(HttpRequest& request, int sock, std::unique_ptr<Json::Value> query) = 0;
    virtual void dispatchProcedure(HttpRequest& request, int sock) = 0;
    virtual void notify(std::string message) {}
protected:
    std::vector<Host> *m_hosts;
};

#endif
