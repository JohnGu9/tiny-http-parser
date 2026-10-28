// reference: https://beej.us/guide/bgnet/html/split/slightly-advanced-techniques.html#poll

#include "tiny-http-parser.h"

#include <arpa/inet.h>
#include <assert.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <map>
#include <vector>

#define PORT "9034" // Port we're listening on

struct FdList
{
    typedef void (*OnEventFn)(int fd, short revent, FdList *list);
    struct Fd
    {
        short event;
        OnEventFn on_event;
    };
    std::map<int, Fd> fds;

    int poll()
    {
        std::vector<pollfd> pfds;
        pfds.reserve(fds.size());
        for (const auto &pair : fds)
        {
            pfds.emplace_back((pollfd){
                .fd = pair.first,
                .events = pair.second.event,
            });
        }
        int poll_count = ::poll(pfds.data(), pfds.size(), -1);
        if (poll_count < 0)
        {
            return poll_count;
        }
        for (const auto &pfd : pfds)
        {
            if (pfd.revents)
            {
                const auto &fd = fds[pfd.fd];
                fd.on_event(pfd.fd, pfd.revents, this);
            }
        }
        return 0;
    }

    int add(int fd, short event, OnEventFn on_event)
    {
        assert(fds.find(fd) == fds.end());
        fds[fd] = {
            .event = event,
            .on_event = on_event,
        };
        return 0;
    }

    int remove(int fd)
    {
        const auto iter = fds.find(fd);
        if (iter == fds.end())
        {
            return -1;
        }
        fds.erase(iter);
        return 0;
    }
};

struct ClientContext
{
    int fd;
    FdList *list;
    const char *customError;
    TinyHttpParserContext_t thpContext;
    int post;
};

static size_t _onHttpHeader(TinyHttpParserContext_t *context, const TinyHttpParserHeader_t *header)
{
    ClientContext *clientContext = tiny_http_parser_get_user_context(context, ClientContext *);
    if (clientContext->customError != nullptr)
    {
        // already has error
        // no more further process
        return SIZE_MAX; // push all rest data to `_onHttpBodyStream()`
    }

    if (header->request_header == NULL)
    {
        clientContext->customError = " Unknown request";
        return SIZE_MAX; // push all rest data to `_onHttpBodyStream()`
    }

#define FMT_STRING_VIEW(string_view) (int)(string_view)->size, (string_view)->data
    const TinyHttpParserStringView_t *method = &header->request_header->method;
    const TinyHttpParserStringView_t *path = &header->request_header->path;
    const TinyHttpParserStringView_t *protocol = &header->request_header->protocol;
    printf("[Client] %.*s %.*s %.*s\n", FMT_STRING_VIEW(method), FMT_STRING_VIEW(path), FMT_STRING_VIEW(protocol));

    clientContext->post = 0;
    if (strncmp(method->data, "POST", method->size) == 0)
    {
        printf("%.*s", (int)header->raw.size, header->raw.data);
        clientContext->post = 1;
        return tiny_http_parser_default_on_header(context, header);
    }

    if (strncmp(method->data, "GET", method->size) != 0 ||
        strncmp(path->data, "/", path->size) != 0)
    {
        const char *response = "HTTP/1.1 404 Not FOUND\r\n"
                               "\r\n";
        ssize_t res = send(clientContext->fd, response, strlen(response), 0);
        printf("[Client] 404 (%zd)\n", res);
        clientContext->customError = "404 Not FOUND";
        return SIZE_MAX;
    }

    const char *response = "HTTP/1.1 200 OK\r\n"
                           "Keep-Alive: timeout=5, max=200\r\n"
                           "Content-Type: text/html; charset=utf-8\r\n"
                           "Content-Length: ";
    send(clientContext->fd, response, strlen(response), 0);

    FILE *file = fopen("index.html", "r");
    assert(file != nullptr);

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char content_length[24];
    snprintf(content_length, sizeof(content_length), "%zu", length);
    send(clientContext->fd, content_length, strlen(content_length), 0);

    const char *end = "\r\n\r\n";
    send(clientContext->fd, end, strlen(end), 0);

    char buffer[1024];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        ssize_t result = send(clientContext->fd, buffer, bytes_read, 0);
        if (result < 0)
        {
            clientContext->customError = "Socket error";
            break;
        }
    }
    printf("[Client] index.html sent\n");
    fclose(file);

    return 0;
}

static size_t _onHttpBodyStream(TinyHttpParserContext_t *context, const char *data, size_t size, size_t *remain_not_recv_size)
{
    ClientContext *clientContext = tiny_http_parser_get_user_context(context, ClientContext *);
    if (clientContext->customError != nullptr)
    {
        // already has error
        // no more further process
        return size;
    }
    if (clientContext->post)
    {
        printf("[Body Stream] %.*s\n", (int)size, data);
        if (*remain_not_recv_size == 0)
        {
            const char *response = "HTTP/1.1 200 OK\r\n"
                                   "Keep-Alive: timeout=5, max=200\r\n"
                                   "\r\n";
            send(clientContext->fd, response, strlen(response), 0);
        }
        return size;
    }
    return size;
}

static void _onHttpBodyCompleted(TinyHttpParserContext_t *context)
{
    printf("[Body Completed]\n");
}

static void _onHttpError(TinyHttpParserContext_t *context, const char *reason)
{
    ClientContext *clientContext = tiny_http_parser_get_user_context(context, ClientContext *);
    assert(clientContext != nullptr);
    clientContext->customError = reason;
}

static std::map<int, ClientContext> _clientContexts;

static void _onClientEvent(int fd, short revent, FdList *list);

static void _openClient(int fd, FdList *list)
{
    list->add(fd, POLLIN | POLL_HUP, _onClientEvent);

    const auto iter = _clientContexts.find(fd);
    assert(iter == _clientContexts.end());

    ClientContext &clientContext = _clientContexts[fd] = {
        .fd = fd,
        .list = list,
        .customError = nullptr,
    };

    tiny_http_parser_init(&clientContext.thpContext, &clientContext,
                          _onHttpHeader,
                          _onHttpBodyStream,
                          _onHttpBodyCompleted,
                          _onHttpError,
                          NULL,
                          NULL);
}

static void _closeClient(int fd, FdList *list)
{
    const auto iter = _clientContexts.find(fd);
    assert(iter != _clientContexts.end());

    close(fd);
    list->remove(fd);

    tiny_http_parser_deinit(&iter->second.thpContext);
    _clientContexts.erase(iter);
}

static void _onClientEvent(int fd, short revent, FdList *list)
{
    char buf[sizeof(size_t) * 256]; // Buffer for client data
    int nbytes = recv(fd, buf, sizeof buf, 0);
    if (nbytes <= 0)
    {
        // Got error or connection closed by client
        if (nbytes == 0)
        {
            // Connection closed
            printf("pollserver: socket %d hung up\n", fd);
        }
        else
        {
            perror("recv");
        }

        _closeClient(fd, list);
    }
    else
    {
        // We got some good data from a client
        const auto iter = _clientContexts.find(fd);
        assert(iter != _clientContexts.end());
        ClientContext *clientContext = &iter->second;
        tiny_http_parser_input(&clientContext->thpContext, buf, nbytes);
        if (clientContext->customError != nullptr)
        {
            printf("[Error]%s\n", clientContext->customError);
            _closeClient(fd, list);
        }
    }
}

/*
 * Convert socket to IP address string.
 * addr: struct sockaddr_in or struct sockaddr_in6
 */
static const char *inet_ntop2(void *addr, char *buf, size_t size)
{
    struct sockaddr_storage *sas = (sockaddr_storage *)addr;
    struct sockaddr_in *sa4;
    struct sockaddr_in6 *sa6;
    void *src;

    switch (sas->ss_family)
    {
    case AF_INET:
        sa4 = (struct sockaddr_in *)addr;
        src = &(sa4->sin_addr);
        break;
    case AF_INET6:
        sa6 = (struct sockaddr_in6 *)addr;
        src = &(sa6->sin6_addr);
        break;
    default:
        return nullptr;
    }

    return inet_ntop(sas->ss_family, src, buf, size);
}

static void _onServerEvent(int fd, short revent, FdList *list)
{
    struct sockaddr_storage remoteaddr; // Client address

    socklen_t addrlen = sizeof remoteaddr;
    int newfd = accept(fd, (struct sockaddr *)&remoteaddr, &addrlen);
    if (newfd == -1)
    {
        perror("accept");
        return;
    }

    char remoteIP[INET6_ADDRSTRLEN];
    printf("pollserver: new connection from %s on socket %d\n",
           inet_ntop2(&remoteaddr, remoteIP, sizeof remoteIP),
           newfd);
    _openClient(newfd, list);
}

/*
 * Return a listening socket.
 */
static int _getListenerSocket(void)
{
    int listener; // Listening socket descriptor
    int yes = 1;  // For setsockopt() SO_REUSEADDR, below

    struct addrinfo hints = {0}, *ai, *p;

    // Get us a socket and bind it
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int rv = getaddrinfo(NULL, PORT, &hints, &ai);
    if (rv != 0)
    {
        fprintf(stderr, "pollserver: %s\n", gai_strerror(rv));
        exit(1);
    }

    for (p = ai; p != nullptr; p = p->ai_next)
    {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0)
        {
            continue;
        }

        // Lose the pesky "address already in use" error message
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0)
        {
            close(listener);
            continue;
        }

        break;
    }

    // If we got here, it means we didn't get bound
    if (p == nullptr)
    {
        return -1;
    }

    freeaddrinfo(ai); // All done with this

    // Listen
    if (listen(listener, 10) == -1)
    {
        return -1;
    }

    return listener;
}

/*
 * Main: create a listener and connection set, loop forever
 * processing connections.
 */
int main(void)
{

    char work_directory[PATH_MAX];
    if (getcwd(work_directory, sizeof(work_directory)) != NULL)
    {
        printf("Current working directory: %.*s\n", (int)sizeof(work_directory), work_directory);
    }
    else
    {
        perror("getcwd() error");
        return 1;
    }

    // Only for debug or demo
    // If in production env, you should limit realloc MAX alloc memory size
    // in arguments of the `tiny_http_parser_hook_init()`
    tiny_http_parser_hook_init(realloc, free);

    FdList fdList;

    // Set up and get a listening socket
    int listener = _getListenerSocket();

    if (listener == -1)
    {
        fprintf(stderr, "error getting listening socket\n");
        return 1;
    }

    fdList.add(listener, POLLIN, _onServerEvent);

    printf("pollserver: waiting for connections (http://localhost:" PORT "/) ...\n");

    // Main loop
    for (;;)
    {
        int poll_count = fdList.poll();
        if (poll_count == -1)
        {
            perror("poll");
            return 1;
        }
    }
    return 0;
}
