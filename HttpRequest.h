#ifndef HTTP_REQUEST_H_
#define HTTP_REQUEST_H_

#include <string>

class HttpRequest {
public: 
    HttpRequest();
    ~HttpRequest();

    void setMethod(std::string method);
    void setMethod(char* method);
    std::string getMethod();

    void setContentLength(int length);
    int getContentLength();

    char* getContent();
    std::string getDecodedContent();
    void setContent(char* content);
private:
    int m_contentLength;
    char* m_content;
    std::string m_decodedContent;
    std::string m_method;

    std::string urlDecode(std::string &SRC);
};

#endif
