#ifndef HTTP_RESPONSE_H_
#define HTTP_RESPONSE_H_

#include <string>

class HttpResponse {
public: 
    HttpResponse();
    ~HttpResponse();

    void setStatus(std::string status);
    void setStatus(char* status);
    void setStatus(int status);
    int getStatus();

    void setContentLength(int length);
    int getContentLength();

    char* getContent();
    void setContent(char* content);
private:
    int m_contentLength;
    char* m_content;
    int m_status;
};

#endif
