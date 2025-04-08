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

    while (total_received < n) {
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
 * Handle a single request
 * Returns 0 on success, -1 on error
 */ 
static int32_t handle_request(int fd)
{
    char rbuf[4 + MAX_MSG];
    errno = 0;
    int32_t ra, sa;
    uint32_t len = 0;
    const char response[] = "Hello, I am server";

    // Get request header
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

    // Get request body
    ra = recv_all(fd, &rbuf[4], len);
    if (ra) {
        fprintf(stderr, "%s\n", ("recv() error"));
        return ra;
    }

    printf("client says: %.*s\n", len, &rbuf[4]);

    char wbuf[4 + sizeof(response)];
    len = (uint32_t)strlen(response);
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], response, len);

    // Send response using the same protocol
    sa = send_all(fd, wbuf, 4 + len);
    if (sa) {
        fprintf(stderr, "%s\n", ("send() error"));
        return sa;
    }

    return 0;
}

int main()
{
    int sockfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage client_addr;
    struct addrinfo hints, *listp, *p;
    int val = 1;
    int rc, rv;
    int32_t hr;
    char host[INET6_ADDRSTRLEN];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    // Get a list of potential server addresses
    rc = getaddrinfo(NULL, "9043", &hints, &listp);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return 1;
    }

    // Walk the list for one that we can successfully bind to
    for (p = listp; p != NULL; p = p->ai_next) {
        // Create a socket
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) {
            perror("server: socket");
            continue;
        }

        // Eliminate "Address already in use" error
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int)) == -1) {
            perror("setsockopt");
            return 1;
        }

        // Bind the socket file descriptor to the address
        rv = bind(sockfd, p->ai_addr, p->ai_addrlen);
        if (rv == -1) {
            close(sockfd);
            perror("bind");
            continue;
        }

        break;
    }

    // Clean up
    freeaddrinfo(listp);
    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        return 2;
    }

    // Listen for connection requests on the socket file descriptor
    rv = listen(sockfd, SOMAXCONN);
    if (rv == -1) {
        perror("listen");
        return 1;
    }

    while(1) {
        // Accept
        clientlen = sizeof(client_addr);
        connfd = accept(sockfd, (struct sockaddr *)&client_addr, &clientlen);
        if (connfd == -1) {
            perror("accept");
            continue;
        }

        // Lookup client host name
        getnameinfo(p->ai_addr, p->ai_addrlen, host, sizeof(host), NULL, 0, NI_NUMERICHOST | NI_NUMERICSERV);
        printf("server: got connection from %s\n\n", host);
        
        while (1) {   
            // Process requests for one client connection at once
            hr = handle_request(connfd);
            if (hr) {
                break;
            }
            
        }
        
        close(connfd);
    }

    return 0;
}