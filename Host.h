#ifndef HOST_H_
#define HOST_H_

//C libraries
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netdb.h>

//C++ libraries
#include <iostream>
#include <string>
#include <memory>

#include "http.h"
#include "HttpResponse.h"

class Host {
public:
    Host(std::string url, int port, bool is_master = false);
    ~Host();

    bool getMaster();
    void setMaster(bool is_master);

    std::string getUrl();
    void setUrl(std::string url);

    int getPort();
    void setPort(int port);

    std::unique_ptr<HttpResponse> executeRequest(struct HttpRequest *request);
private:
    std::string url;
    int port;
    bool is_master;

    int openConnection();
    char *strnstr_(const char *haystack, const char *needle, size_t len_haystack, size_t len_needle);
    int get_content_lenght1(const char *buf, const int size, const char *lengthname);
    int get_content_lenght(const char *buf, const int size);
};

#endif
