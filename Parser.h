#include <iostream>

#include "jsoncpp/json.h"

class Parser {
public:
    Parser();
    ~Parser();

    int modifyingQuery(const std::string &document);
protected:
    Json::Reader m_reader;
};
