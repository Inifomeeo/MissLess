#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

static void communicate(int connfd)
{
    char rbuf[64] = {};
    if (recv(connfd, rbuf, sizeof(rbuf) - 1, 0) < 0) {
        perror("recv");
        return;
    }

    printf("client says: %s\n", rbuf);

    char wbuf[] = "Hello, I am server";
    send(connfd, wbuf, strlen(wbuf), 0);
}

int main()
{
    int sockfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage client_addr;
    struct addrinfo hints, *listp, *p;
    int val = 1;
    int rc, rv;
    char host[INET6_ADDRSTRLEN];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    // Get a list of potential server addresses
    if ((rc = getaddrinfo(NULL, "9043", &hints, &listp)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return 1;
    }

    // Walk the list for one that we can successfully bind to
    for (p = listp; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        // Eliminate "Address already in use" error
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int)) == -1) {
            perror("setsockopt");
            return 1;
        }

        // Bind
        if ((rv = bind(sockfd, p->ai_addr, p->ai_addrlen)) == -1) {
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

    // Listen
    if ((rv = listen(sockfd, SOMAXCONN)) == -1) {
        perror("listen");
        return 1;
    }

    while(1) {
        // Accept
        clientlen = sizeof(client_addr);
        if ((connfd = accept(sockfd, (struct sockaddr *)&client_addr, &clientlen)) == -1) {
            perror("accept");
            continue;
        }

        // Lookup client host name
        getnameinfo(p->ai_addr, p->ai_addrlen, host, sizeof(host), NULL, 0, NI_NUMERICHOST | NI_NUMERICSERV);
        printf("server: got connection from %s\n", host);
        
        communicate(connfd);
        close(connfd);
    }

    return 0;
}