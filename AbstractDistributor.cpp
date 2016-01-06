#include "AbstractDistributor.h"
#include "dbg.h"


int AbstractDistributor::queryType(std::unique_ptr<Json::Value> query) {
    Json::Value operators;
    Json::Value obj_value(Json::objectValue);
    
    if (!query->isObject() || query->isMember("operators") == false) {
        log_err("query content does not contain any operators");
        throw "query content does not contain any operators";
    }
    operators = query->get("operators", obj_value);

    for (auto op: operators) {
        auto type = op.get("type", "").asString();
        if (type == "InsertScan" or
            type == "Delete" or
            type == "PosUpdateIncrementScan" or
            type == "PosUpdateScan") return WRITE;
        if (type == "TableLoad") return LOAD;
    }
    return READ;
}


void AbstractDistributor::dispatch(HttpRequest& request, int sock) {
    debug("Distribute.");

    Json::Reader reader = Json::Reader();
    if (request.getResource() == "/query") {
        if (request.hasDecodedContent("query")) {
            std::unique_ptr<Json::Value> root (new Json::Value);
            if (reader.parse(request.getDecodedContent("query"), (*root)) == false) {
                // Error TODO: Error Response
                log_err("Error parsing json: %s", reader.getFormattedErrorMessages().c_str());
                debug("%s", request.getContent());
                debug("%s", request.getDecodedContent("query").c_str());
                close(sock);
                return;
            }
            int query_t = queryType(std::move(root));
            if (query_t == READ) {
                distribute(request, sock);
                return;
            } else if (query_t == LOAD) {
                sendToAll(request, sock);
                return;
            }
        }
    }
    sendToMaster(request, sock);
}
