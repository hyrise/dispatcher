#include "HttpResponse.h"

HttpResponse::HttpResponse() {};
HttpResponse::~HttpResponse() {};

void HttpResponse::setStatus(std::string status) {
    m_status = atoi(status.c_str());
}

void HttpResponse::setStatus(char* status) {
    m_status = atoi(status);
}

void HttpResponse::setStatus(int status) {
    m_status = status;
}

int HttpResponse::getStatus() {
    return m_status;
}

void HttpResponse::setContentLength(int length) {
    m_contentLength = length;
}

int HttpResponse::getContentLength() {
    return m_contentLength;
}

void HttpResponse::setContent(char* content) {
    m_content = content;
}

char* HttpResponse::getContent() {
    return m_content;
}
