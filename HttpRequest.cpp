#include "HttpRequest.h"

HttpRequest::HttpRequest() {};
HttpRequest::~HttpRequest() {};

void HttpRequest::setMethod(std::string method) {
    m_method = method;
}

void HttpRequest::setMethod(char* method) {
    m_method = std::string(method);
}

std::string HttpRequest::getMethod() {
    return m_method;
}

void HttpRequest::setResource(std::string resource) {
    m_resource = resource;
};

void HttpRequest::setResource(char* resource) {
    m_resource = std::string(resource);
};

std::string HttpRequest::getResource() {
    return m_resource;
}

void HttpRequest::setContentLength(int length) {
    m_contentLength = length;
}

int HttpRequest::getContentLength() {
    return m_contentLength;
}

void HttpRequest::setContent(char* content) {
    m_content = content;
    std::string contentAsString(content);
    m_decodedContent = parseContent(contentAsString);
}

char* HttpRequest::getContent() {
    return m_content;
}

bool HttpRequest::hasDecodedContent(std::string key) {
    std::map<std::string, std::string>::const_iterator got = m_decodedContent.find (key);

    if ( got == m_decodedContent.end() )
        return false;
    else
        return true;
}

std::string& HttpRequest::getDecodedContent(std::string key) {
    return m_decodedContent.at(key);
}

std::map<std::string, std::string> HttpRequest::parseContent(std::string content) {
    int pLastKey, pValue, pNextKey;
    std::map<std::string, std::string> result;

    pLastKey = 0;
    while (true) {
        pValue = content.find('=', pLastKey);
        pNextKey = content.find('&', pValue);
        std::string key = content.substr(pLastKey, pValue - pLastKey);
        std::string value = content.substr(pValue + 1, pNextKey == std::string::npos ? pNextKey : pNextKey - pValue -1);
        result.emplace(key, urlDecode(value));
        if (pNextKey == std::string::npos) break;
        pLastKey = pNextKey + 1;
    }

#ifdef DEBUG
    for (auto& x: result)
        std::cout << x.first << ": " << x.second << std::endl;
#endif

    return result;
}

std::string HttpRequest::urlDecode(std::string &SRC) {
    std::string ret;
    char ch;
    int i, ii;
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
