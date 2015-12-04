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
    std::string& getDecodedContent(std::string key);
    bool hasDecodedContent(std::string key);
    void setContent(char* content);
private:
    int m_contentLength;
    char* m_content;
    std::map<std::string, std::string> m_decodedContent;
    std::string m_method;
    std::string m_resource;

    std::string urlDecode(std::string &SRC);
    std::map<std::string, std::string> parseContent(std::string content);
};

#endif
