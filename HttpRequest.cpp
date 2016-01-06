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
}

char* HttpRequest::getContent() {
    return m_content;
}
