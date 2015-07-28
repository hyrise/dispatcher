#include "Parser.h"

Parser::Parser() {
    m_reader = Json::Reader();
}

Parser::~Parser() {};

int Parser::queryType (const std::string &document) {
    bool writeQuery = false;
    Json::Value root;
    Json::Value operators;
    Json::Value obj_value(Json::objectValue);

    if (m_reader.parse(document, root) == false) {
        std::cerr << "Error parsing json:" << m_reader.getFormattedErrorMessages() << std::endl;
        return -1;
    }

    if (!root.isObject() || root.isMember("operators") == false) {
        std::cerr << "query content does not contain any operators";
        return -1;
    }
    operators = root.get("operators", obj_value);

    for (auto op: operators) {
        if (op.get("type", "").asString() == "InsertScan") writeQuery = true;
        if (op.get("type", "").asString() == "TableLoad") return 2;
    }

    if (writeQuery) return 1;

    return 0;
}
