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

void HttpRequest::setContentLength(int length) {
    m_contentLength = length;
}

int HttpRequest::getContentLength() {
    return m_contentLength;
}

void HttpRequest::setContent(char* content) {
    m_content = content;
    std::string content_as_string(content + 6);
    m_decodedContent = urlDecode(content_as_string);
}

char* HttpRequest::getContent() {
    return m_content;
}

std::string HttpRequest::getDecodedContent() {
    return m_decodedContent;
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
