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
#include <fcntl.h>
#include <sys/epoll.h>

typedef std::vector<uint8_t> Buffer;

struct Connection {
    int fd = -1;
    bool want_read = false;
    bool want_write = false;
    bool want_close = false;
    Buffer incoming;
    Buffer outgoing;
};

const size_t MAX_MSG = 64 << 20;

// Set a file descriptor to non-blocking
static void fd_set_nb(int fd)
{
    errno = 0;
    int flags = fcntl(fd, F_GETFL, 0);
    if (errno) {
        perror("fcntl");
        exit(EXIT_FAILURE);
        return;
    }

    flags |= O_NONBLOCK;

    errno = 0;
    (void)fcntl(fd, F_SETFL, flags);
    if (errno) {
        perror("fcntl");
        exit(EXIT_FAILURE);
    }
}

// Append to the back
static void buf_append(Buffer &buf, const uint8_t *data, size_t len)
{
    buf.insert(buf.end(), data, data + len);
}

// Remove from the front
static void buf_consume(Buffer &buf, size_t n)
{
    buf.erase(buf.begin(), buf.begin() + n);
}

// Application callback when the listening socket is ready
static Connection *handle_accept(int fd)
{
    struct sockaddr_storage client_addr;
    int connfd;
    socklen_t clientlen;
    Connection *conn;

    // Accept a client connection
    clientlen = sizeof(client_addr);
    connfd = accept(fd, (struct sockaddr *)&client_addr, &clientlen);
    if (connfd < 0) {
        perror("accept");
        return NULL;
    }

    // Set the new connection fd to non-blocking
    fd_set_nb(connfd);

    // Set the connection state
    conn = new Connection();
    conn->fd = connfd;
    conn->want_read = true;
    return conn;
}

// Process 1 request if there is enough data
static bool try_one_request(Connection *conn)
{
    uint32_t len = 0;

    // Parse the message header
    if (conn->incoming.size() < 4) {
        return false;
    }
    
    memcpy(&len, conn->incoming.data(), 4);
    if (len > MAX_MSG) {
        fprintf(stderr, "%s\n", "too long");
        conn->want_close = true;
        return false;
    }

    // Parse the message body
    if (4 + len > conn->incoming.size()) {
        return false;
    }
    const uint8_t *request = &conn->incoming[4];

    printf("client says: length: %d, data: %.*s\n", len, len < 100 ? len : 100, request);

    // Echo the request as the response
    buf_append(conn->outgoing, (const uint8_t *)&len, 4);
    buf_append(conn->outgoing, request, len);

    // Remove the request message from the input buffer
    buf_consume(conn->incoming, 4 + len);

    return true;
}

// Application callback when the socket is writable
static void handle_write(Connection *conn)
{
    ssize_t rv;

    rv = send(conn->fd, &conn->outgoing[0], conn->outgoing.size(), 0);
    if (rv < 0 && errno == EAGAIN) {
        return; // socket is not ready
    }
    if (rv < 0) {
        perror("send");
        conn->want_close = true;
        return;
    }

    // Remove written data from the output buffer
    buf_consume(conn->outgoing, (size_t)rv);

    // Update the readiness intention
    if (conn->outgoing.size() == 0) { // all data written
        conn->want_read = true;
        conn->want_write = false;
    } // else: want write
}

// Application callback when the socket is readable
static void handle_read(Connection *conn)
{
    ssize_t rv;
    uint8_t buf[64 * 1024];

    rv = recv(conn->fd, buf, sizeof(buf), 0);
    if (rv < 0 && errno == EAGAIN) {
        return; // socket is not ready
    }
    if (rv < 0) {
        perror("recv");
        conn->want_close = true;
        return;
    }

    // Handle EOF
    if (rv == 0) {
        if (conn->incoming.size() == 0) {
            fprintf(stderr, "%s\n", "client closed");
        } else {
            fprintf(stderr, "%s\n", "unexpected EOF");
        }
        conn->want_close = true;
        return;
    }

    // Add read data to the input buffer
    buf_append(conn->incoming, buf, (size_t)rv);

    // Parse requests and generate responses
    while (try_one_request(conn)) {}

    // Update the readiness intention
    if (conn->outgoing.size() > 0) { // has a response
        conn->want_read = false;
        conn->want_write = true;
        // The socket is likely ready to write in a request-response protocol,
        // so try to write to it without waiting for the next iteration of the event loop
        return handle_write(conn);
    }   // else: want read
}

int main()
{
    int sockfd;
    struct addrinfo hints, *listp, *p;
    int val = 1;
    int rc, rv;
    char host[INET6_ADDRSTRLEN];
    int epfd;
    const int MAX_EVENTS = 64;
    struct epoll_event events[MAX_EVENTS];
    uint32_t revents;
    Connection *conn;
    Connection *new_conn;

    // Configure the server address structure
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
            perror("socket");
            continue;
        }

        // Eliminate "Address already in use" error
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int)) == -1) {
            perror("setsockopt");
            exit(EXIT_FAILURE);
        }

        // Bind the socket to the address
        rv = bind(sockfd, p->ai_addr, p->ai_addrlen);
        if (rv == -1) {
            perror("bind");
            close(sockfd);
            continue;
        }

        break;
    }

    // Clean up
    freeaddrinfo(listp);
    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        exit(EXIT_FAILURE);
    }

    // Set the listen fd to non-blocking
    fd_set_nb(sockfd);

    // Listen for incoming connections
    rv = listen(sockfd, SOMAXCONN);
    if (rv == -1) {
        perror("listen");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Create the epoll fd
    epfd = epoll_create1(0);
    if (epfd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.ptr = nullptr; // distinguish the listening socket
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev) == -1) {
        perror("epoll_ctl");
        exit(EXIT_FAILURE);
    }

    // Map file descriptors to client connections
    std::vector<Connection*> fd_to_conn;

    // Event loop
    while (true) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (n == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < n; ++i) {
            conn = static_cast<Connection*>(events[i].data.ptr);

            // Listening sockets
            if (!conn) {
                new_conn = handle_accept(sockfd);
                if (new_conn) {
                    // Lookup client host name
                    getnameinfo(p->ai_addr, p->ai_addrlen, host, sizeof(host), NULL, 0, NI_NUMERICHOST | NI_NUMERICSERV);
                    printf("server: got connection from %s\n\n", host);

                    if ((int)fd_to_conn.size() <= new_conn->fd) {
                        fd_to_conn.resize(new_conn->fd + 1);
                    }
                    fd_to_conn[new_conn->fd] = new_conn;

                    struct epoll_event ev;
                    ev.events = EPOLLIN | EPOLLET;
                    ev.data.ptr = new_conn;
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, new_conn->fd, &ev) == -1) {
                        perror("epoll_ctl: conn_add");
                        exit(EXIT_FAILURE);
                    }
                }
                continue;
            }

            // Connection sockets
            revents = events[i].events;
            if (revents & EPOLLIN) {
                handle_read(conn);
            }
            if (revents & EPOLLOUT) {
                handle_write(conn);
            }

            if ((revents & (EPOLLERR | EPOLLHUP)) || conn->want_close) {
                epoll_ctl(epfd, EPOLL_CTL_DEL, conn->fd, nullptr);
                close(conn->fd);
                fd_to_conn[conn->fd] = nullptr;
                delete conn;
                continue;
            }

            // Update interest
            struct epoll_event ev;
            ev.data.ptr = conn;
            ev.events = EPOLLET;
            if (conn->want_read)
                ev.events |= EPOLLIN;
            if (conn->want_write)
                ev.events |= EPOLLOUT;
            if (epoll_ctl(epfd, EPOLL_CTL_MOD, conn->fd, &ev) == -1) {
                perror("epoll_ctl: mod");
                exit(EXIT_FAILURE);
            }
        }
    }

    return 0;
}