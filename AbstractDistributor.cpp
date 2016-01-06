#include "AbstractDistributor.h"
#include "dbg.h"

#include <string>
#include <iostream>


std::string urlDecode(std::string &SRC) {
    std::string ret;
    char ch;
    unsigned int i, ii;
    for (i=0; i<SRC.length(); i++) {
        if (int(SRC[i])==37) {
            sscanf(SRC.substr(i+1,2).c_str(), "%x", &ii);
            ch=static_cast<char>(ii);
            ret+=ch;
            i=i+2;
        } else {
            ret+=SRC[i];
        }
    }
    return (ret);
}


int AbstractDistributor::queryType(char *http_payload) {
    std::string http_payload_str(http_payload);
    Json::Reader reader = Json::Reader();
    size_t pLastKey, pValue, pNextKey;
    std::map<std::string, std::string> content;

    pLastKey = 0;
    while (true) {
        pValue = http_payload_str.find('=', pLastKey);
        pNextKey = http_payload_str.find('&', pValue);
        std::string key = http_payload_str.substr(pLastKey, pValue - pLastKey);
        std::string value = http_payload_str.substr(pValue + 1, pNextKey == std::string::npos ? pNextKey : pNextKey - pValue -1);
        content.emplace(key, urlDecode(value));
        if (pNextKey == std::string::npos) break;
        pLastKey = pNextKey + 1;
    }
    std::string &query_str = content.at("query");

    std::unique_ptr<Json::Value> root (new Json::Value);
    if (reader.parse(query_str, (*root)) == false) {
        // Error TODO: Error Response
        log_err("Error parsing json: %s", reader.getFormattedErrorMessages().c_str());
        debug("%s", http_payload);
        return -1;
    }
    std::unique_ptr<Json::Value> query = std::move(root);


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

    
    if (request.getResource() == "/query") {
        int query_t = queryType(request.getContent());
        switch(query_t) {
            case READ:
                distribute(request, sock);
                return;
            case LOAD:
                sendToAll(request, sock);
                return;
            case WRITE:
                sendToMaster(request, sock);
                return;
            default:
                log_err("Invalid query: %s", request.getContent());
                throw "Invalid query.";
        }
    } else if (request.getResource() == "/procedure") {
        sendToMaster(request, sock);
        return;
    }
    log_err("Invalid HTTP recource: %s", request.getResource().c_str());
    throw "Invalid recource.";
}
