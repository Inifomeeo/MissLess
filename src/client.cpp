#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <vector>
#include <string>

typedef std::vector<uint8_t> Buffer;

const size_t MAX_MSG = 64 << 20;

// Append to the back
static void buf_append(Buffer &buf, const uint8_t *data, size_t len)
{
    buf.insert(buf.end(), data, data + len);
}

/*
 * Read n bytes into buf from socket fd
 * Returns 0 on success, -1 on error
 */
static int32_t recv_all(int fd, uint8_t *buf, size_t n)
{
    size_t total_read = 0;
    size_t byetes_left = n;
    ssize_t num_read;

    while (total_read < n) {
        num_read = recv(fd, buf + total_read, byetes_left, 0);
        if (num_read <= 0) {
            return -1;
        }
        total_read += num_read;    
        byetes_left -= num_read;
    }

    return 0;
}

/*
 * Send n bytes of buf to socket fd
 * Returns 0 on success, -1 on error
 */
static int32_t send_all(int fd, const uint8_t *buf, size_t n)
{
    size_t total_sent = 0;
    size_t byetes_left = n;
    ssize_t num_sent;

    while (total_sent < n) {
        num_sent = send(fd, buf + total_sent, byetes_left, 0);
        if (num_sent <= 0) {
            return -1;
        }
        total_sent += num_sent;    
        byetes_left -= num_sent;
    }

    return 0;
}

/*
 * Send a request
 * Returns 0 on success, -1 on error
 */
static int32_t send_req(int fd, const uint8_t *text, size_t len)
{
    int32_t sa;
    Buffer wbuf;

    if (len > MAX_MSG) {
        return -1;
    }

    buf_append(wbuf, (const uint8_t *)&len, 4);
    buf_append(wbuf, text, len);

    sa = send_all(fd, wbuf.data(), wbuf.size());
    if (sa) {
        fprintf(stderr, "%s\n", ("send"));
        return sa;
    }

    return 0;
}

/*
 * Read a response
 * Returns 0 on success, -1 on error
 */
static int32_t read_res(int fd)
{
    int32_t ra;
    Buffer rbuf;
    errno = 0;
    uint32_t len = 0;

    // Get response header
    rbuf.resize(4);
    ra = recv_all(fd, &rbuf[0], 4);
    if (ra) {
        if (errno == 0) {
            fprintf(stderr, "%s\n", "EOF");
        } else {
            fprintf(stderr, "%s\n", "recv");
        }
        return ra;
    }

    memcpy(&len, rbuf.data(), 4);  // assume little endian
    if (len > MAX_MSG) {
        fprintf(stderr, "%s\n", "too long");
        return -1;
    }

    // Get response body
    rbuf.resize(4 + len);
    ra = recv_all(fd, &rbuf[4], len);
    if (ra) {
        fprintf(stderr, "%s\n", "recv");
        return ra;
    }

    printf("server echoed: length: %u, data: %.*s\n", len, len < 100 ? len : 100, &rbuf[4]);

    return 0;
}

int main()
{
    int sockfd;
    struct addrinfo hints, *listp, *p;
    int rc, rv;
    int32_t sr, rr;
    char host[INET6_ADDRSTRLEN];

    // Configure the server address structure
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    // Get a list of potential server addresses
    rc = getaddrinfo("localhost", "9043", &hints, &listp);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return 1;
    }

    // Walk the list for one that we can successfully connect to
    for (p = listp; p != NULL; p = p->ai_next) {
        // Create a socket
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) {
            perror("socket");
            continue;
        }

        // Connect to the server
        rv = connect(sockfd, p->ai_addr, p->ai_addrlen);
        if (rv == -1) {
            perror("connect");
            close(sockfd);
            continue;
        }

        break;
    }

    // Clean up
    freeaddrinfo(listp);
    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        exit(EXIT_FAILURE);
    }

    // Lookup server host name
    getnameinfo(p->ai_addr, p->ai_addrlen, host, sizeof(host), NULL, 0, NI_NUMERICHOST | NI_NUMERICSERV);
    printf("client: connecting to %s\n\n", host);

    // Pipelined requests
    std::vector<std::string> query_list = {
        "hello_1", "hello_2", "hello_3",
        // A large message requiring multiple event loop iterations
        std::string(MAX_MSG, 'z'),
        "hello_4",
    };
    for (const std::string &s : query_list) {
        sr = send_req(sockfd, (uint8_t *)s.data(), s.size());
        if (sr) {
            goto query_end;
        }
    }
    for (size_t i = 0; i < query_list.size(); ++i) {
        rr = read_res(sockfd);
        if (rr) {
            goto query_end;
        }
    }

query_end:
    close(sockfd);

    return 0;
}