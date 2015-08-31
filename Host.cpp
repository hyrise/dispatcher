#include "Host.h"

#define BUFFERSIZE 65535

Host::Host(std::string url, int port, bool isMaster):
        m_url(url), m_port(port), m_master(isMaster) {};

Host::~Host() {
}

bool Host::getMaster() {
    return m_master;
}

void Host::setMaster(bool isMaster) {
    m_master = isMaster;
}

std::string Host::getUrl() {
    return m_url;
}

void Host::setUrl(std::string url) {
    m_url = url;
}

int Host::getPort() {
    return m_port;
}

void Host::setPort(int port) {
    m_port = port;
}

char *Host::strnstr_(const char *haystack, const char *needle, size_t len_haystack, size_t len_needle) {
    if (len_haystack == 0) return (char *)haystack; /* degenerate edge case */
    if (len_needle == 0) return (char *)haystack; /* degenerate edge case */
    while ((haystack = strchr(haystack, needle[0]))) {
        if (!strncmp(haystack, needle, len_needle)) return (char *)haystack;
        haystack++; }
    return NULL;
}

int Host::get_content_lenght1(const char *buf, const int size, const char *lengthname) {
    const char *hit_ptr;
    int content_length;
    hit_ptr = strnstr_(buf, lengthname, size, 15);
    if (hit_ptr == NULL) {
        return -1;
    }
    char format [50];
    strcpy(format,lengthname);
    strcat(format," %d");
    fflush(stdout);
    if (sscanf(hit_ptr, format, &content_length) != 1) {
        return -1;
    }
    return content_length;
}

int Host::get_content_lenght(const char *buf, const int size) {
    int res = get_content_lenght1(buf, size, "Content-Length:");
    if (res == -1)
        res = get_content_lenght1(buf, size, "Content-length:");
        if (res == -1)
            res = get_content_lenght1(buf, size, "content-length:");
    return res;
}

std::unique_ptr<HttpResponse> Host::executeRequest(HttpRequest& request) {
    int sock = openConnection();
    std::unique_ptr<HttpResponse> response(new HttpResponse);

    char http_post[] = "POST %s HTTP/1.1\r\n\
Content-Length: %d\r\n\
Connection: Keep-Alive\r\n\r\n\
%s";

    char *buf;
    int allocatedBytes = asprintf(&buf, http_post, "/query/" /*request.getResource().c_str()*/, request.getContentLength(), request.getContent());
    if (allocatedBytes == -1) {
	std::cerr << "Error during creating request" << std::endl;
        return NULL;
    }
    send(sock, buf, strlen(buf), 0);
    free(buf);

    int offset = 0;
    int recv_size = 0;
    int first_line_received = 0;
    int header_received = 0;
    char *http_body_start = NULL;
    int status = 0;
    int content_length = 0;
    char *content;

    buf = new char[BUFFERSIZE];
    memset( buf, 0, BUFFERSIZE );

    while ((recv_size = read(sock, buf+offset, BUFFERSIZE-offset)) > 0) {
	debug("received %i bytes\ncontent: %s", recv_size, buf );
        offset += recv_size;
        if (!first_line_received) {
            char *hit_ptr;
            hit_ptr = strnstr_(buf, "\n", offset, 1);
            if (hit_ptr == NULL) {
                continue;
            }
            first_line_received = 1;
            // first line received
            // it can be parsed for http method and recource
            int n;
            if ((n = sscanf(buf, "HTTP/1.1 %d", &status)) == 1) {
                response->setStatus(status);
		        debug("HTTP Response status: %i", status);
            } else {
                std::cerr << "ERROR----------------------- scanf " << n << std::endl;
                close(sock);
                return NULL;
            }
	        if (status != 200) {
                close(sock);
                std::cerr << "Wrong Status" << std::endl;
	            return NULL;
	        }
        }

        // first line received and checked successfully
        // check for content next
        if (!header_received) {
            char *hit_ptr;
            hit_ptr = strnstr_(buf, "\r\n\r\n", offset, 4);
            http_body_start = hit_ptr + 4;
            if (hit_ptr == NULL) {
                std::cerr << "not FOUND" << std::endl;
                continue;
            }
            header_received = 1;
            // header delimiter reached
            content_length = get_content_lenght(buf, offset);
            response->setContentLength(content_length);
            debug("Content-Length: %i", content_length);
        }

        // complete header was received
        // check whether message is complete
        if (http_body_start != NULL) {
            if (((http_body_start - buf) + content_length) == offset) {
                debug("complete message received\n header: %s", http_body_start-buf);
                content = http_body_start;
                response->setContent(content);
                free(buf);
                close(sock);
                return response;
            }
        }
        debug("Read...");
    }
    free(buf);
    close(sock);
    return NULL;
}

int Host::openConnection() {
    int sock;
    struct sockaddr_in dest;

    if ( (sock = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
        std::cerr << "ERROR: could not create a socket" << std::endl;
    }

    //---Initialize server address/port struct
    bzero(&dest, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = inet_addr(this->m_url.data());
    dest.sin_port = htons(this->m_port);

    //---Connect to server
    if ( connect(sock, (struct sockaddr*)&dest, sizeof(dest)) != 0 ) {
        std::cerr << "ERROR: could not connect to host"  << std::endl;
    }

    return sock;
}
