#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

int main()
{
    int sockfd;
    struct addrinfo hints, *listp, *p;
    int rc, rv;
    char host[INET6_ADDRSTRLEN];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    // Get a list of potential server addresses
    if ((rc = getaddrinfo("localhost", "9043", &hints, &listp)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return 1;
    }

    // Walk the list for one that we can successfully connect to
    for (p = listp; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        // Connect
        if ((rv = connect(sockfd, p->ai_addr, p->ai_addrlen)) == -1) {
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
    printf("client: connecting to %s\n", host);

    char wbuf[] = "Hello, I am client";
    send(sockfd, wbuf, strlen(wbuf), 0);

    char rbuf[64] = {};
    if (recv(sockfd, rbuf, sizeof(rbuf) - 1, 0) < 0) {
        perror("recv");
        return 1;
    }
    printf("server says: %s\n", rbuf);

    close(sockfd);

    return 0;
}