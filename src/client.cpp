#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

const size_t MAX_MSG = 4096;

/*
 * Read n bytes into buf from socket fd
 * Returns 0 on success, -1 on error
 */
static int32_t recv_all(int fd, char *buf, size_t n)
{
    size_t total_received = 0;
    size_t byetes_left = n;
    ssize_t num_read;

    while (total_received < n)
    {
        num_read = recv(fd, buf + total_received, byetes_left, 0);
        if (num_read <= 0) {
            return -1;
        }
        total_received += num_read;    
        byetes_left -= num_read;
    }

    return 0;
}

/*
 * Send n bytes of buf to socket fd
 * Returns 0 on success, -1 on error
 */
static int32_t send_all(int fd, const char *buf, size_t n)
{
    size_t total_sent = 0;
    size_t byetes_left = n;
    ssize_t num_sent;

    while (total_sent < n)
    {
        num_sent = send(fd, buf + total_sent, byetes_left, 0);
        if (num_sent <= 0) {
            return -1;
        }
        total_sent += num_sent;    
        byetes_left -= num_sent;
    }

    return 0;
}

static int32_t query(int fd, const char *text) {
    char rbuf[4 + MAX_MSG + 1];
    errno = 0;
    int32_t ra, sa;

    uint32_t len = (uint32_t)strlen(text);
    if (len > MAX_MSG) {
        return -1;
    }

    char wbuf[4 + MAX_MSG];
    memcpy(wbuf, &len, 4);  // assume little endian
    memcpy(&wbuf[4], text, len);

    // Send request
    sa = send_all(fd, wbuf, 4 + len);
    if (sa) {
        return sa;
    }

    // Get response header
    ra = recv_all(fd, rbuf, 4);
    if (ra) {
        fprintf(stderr, "%s\n", (errno == 0 ? "EOF" : "recv() error"));
        return ra;
    }

    memcpy(&len, rbuf, 4);  // assume little endian
    if (len > MAX_MSG) {
        fprintf(stderr, "%s\n", ("message is too long"));
        return -1;
    }

    // Get response body
    ra = recv_all(fd, &rbuf[4], len);
    if (ra) {
        fprintf(stderr, "%s\n", ("recv() error"));
        return ra;
    }

    printf("server says: %.*s\n", len, &rbuf[4]);

    return 0;
}

int main()
{
    int sockfd;
    struct addrinfo hints, *listp, *p;
    int rc, rv;
    int32_t q;
    char host[INET6_ADDRSTRLEN];

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
            perror("client: socket");
            continue;
        }

        // Connect to the server
        rv = connect(sockfd, p->ai_addr, p->ai_addrlen);
        if (rv == -1) {
            close(sockfd);
            perror("connect");
            continue;
        }

        break;
    }

    // Clean up
    freeaddrinfo(listp);
    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        return 2;
    }

    // Lookup server host name
    getnameinfo(p->ai_addr, p->ai_addrlen, host, sizeof(host), NULL, 0, NI_NUMERICHOST | NI_NUMERICSERV);
    printf("client: connecting to %s\n\n", host);

    // Send multiple requests
    q = query(sockfd, "hello1");
    if (q) {
        goto query_end;
    }
    q = query(sockfd, "hello2");
    if (q) {
        goto query_end;
    }
    q = query(sockfd, "hello3");
    if (q) {
        goto query_end;
    }

query_end:
    close(sockfd);

    return 0;
}