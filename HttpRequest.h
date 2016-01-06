#ifndef HTTP_REQUEST_H_
#define HTTP_REQUEST_H_

#include <string>
#include <map>
#include <iostream>

#include "dbg.h"

class HttpRequest {
public: 
    HttpRequest();
    ~HttpRequest();

    void setMethod(std::string method);
    void setMethod(char* method);
    std::string getMethod();

    void setResource(std::string ressource);
    void setResource(char* ressource);
    std::string getResource();

    void setContentLength(int length);
    int getContentLength();

    char* getContent();
    void setContent(char* content);
private:
    int m_contentLength;
    char* m_content;
    std::string m_method;
    std::string m_resource;
};

#endif
