#include "HttpResponse.h"
#include <cstring>

HttpResponse::HttpResponse() {
    m_content = new char[65535];
};
HttpResponse::~HttpResponse() {
    free(m_content);
};

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
    strncpy(m_content, content, m_contentLength);
}

char* HttpResponse::getContent() {
    return m_content;
}
