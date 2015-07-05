#ifndef SIMPLE_ROUND_ROBIN_DISPATCHER_H_
#define SIMPLE_ROUND_ROBIN_DISPATCHER_H_

#include <iostream>
#include <atomic>
#include <memory>

#include "AbstractDispatcher.h"
#include "HttpResponse.h"
#include "Parser.h"

class SimpleRoundRobinDispatcher : public AbstractDispatcher {
public:
    SimpleRoundRobinDispatcher(std::vector<Host>* hosts);
    ~SimpleRoundRobinDispatcher();

    virtual void dispatch(HttpRequest& request, int sock);
private:
    std::atomic<unsigned int> m_readCount;
};

#endif
