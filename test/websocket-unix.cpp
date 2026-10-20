// reference: https://beej.us/guide/bgnet/html/split/slightly-advanced-techniques.html#poll

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

#include "websocket-accept.h"

// cpp does't support marco __STDC_VERSION__, we need to define it by ourself
// In this project c standard is set to c99, the `thp` library has the symbols we need
#define __STDC_VERSION__ 199901L
#include "tiny-http-parser.h"

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
    bool wsUpgraded;
    TinyHttpParserWebsocketContext_t wsContext;
};

static void _onWsError(TinyHttpParserWebsocketContext_t *context, const char *reason)
{
    ClientContext *clientContext = tiny_http_parser_get_user_context(context, ClientContext *);
    clientContext->customError = reason;
}

static void _onWsControl(TinyHttpParserWebsocketContext_t *context, uint8_t opcode, const char *data, size_t size)
{
    ClientContext *clientContext = tiny_http_parser_get_user_context(context, ClientContext *);
    printf("[Control(%d)]%.*s\n", (int)opcode, (int)size, data);
    switch (opcode)
    {
    case 8: // close event
    {
        if (size != 0)
        {
            // echo the close status code
            assert(size < 126);
            const uint8_t closeMessage[] = {0x88, (uint8_t)size};
            send(clientContext->fd, closeMessage, sizeof(closeMessage), 0);
            // this app acts as a server and no need to mask data
            send(clientContext->fd, data, size, 0);
        }
        else
        {
            const uint8_t closeMessage[] = {0x88, 0x00};
            send(clientContext->fd, closeMessage, sizeof(closeMessage), 0);
        }
        clientContext->customError = "Websocket closed by peer"; // mask error to close socket later
        break;
    }
    case 9: // ping event
    {
        const uint8_t pongMessage[] = {0x8a, 0x00};
        send(clientContext->fd, pongMessage, sizeof(pongMessage), 0);
        break;
    }
    case 10: // pong event
    default:
        break;
    }
}

static void _onWsMessage(TinyHttpParserWebsocketContext_t *context, uint8_t opcode, const char *data, size_t size)
{
    const char *messageType;
    switch (opcode)
    {
    case 1:
        messageType = "UTF-8 text";
        break;
    case 2:
        messageType = "Binary";
        break;
    default:
        messageType = "Unknown";
        break;
    }
    printf("[Message(%s)]%.*s\n", messageType, (int)size, data);
    if (size < 126)
    {
        ClientContext *clientContext = tiny_http_parser_get_user_context(context, ClientContext *);
        const uint8_t echoMessage[] = {(uint8_t)(0x80 | opcode), (uint8_t)size};
        send(clientContext->fd, echoMessage, sizeof(echoMessage), 0);
        // this app acts as a server and no need to mask data
        send(clientContext->fd, data, size, 0);
    }
}

static size_t _onHttpHeader(TinyHttpParserContext_t *context, const TinyHttpParserHeader_t *header)
{
    ClientContext *clientContext = tiny_http_parser_get_user_context(context, ClientContext *);
    if (clientContext->customError != nullptr)
    {
        // already has error
        // no more further process
        return 0;
    }

    const TinyHttpParserStringView_t *wsKey = nullptr;
    if (header->request_header != nullptr && header->request_header->headers_list != nullptr)
    {
        const TinyHttpParserPair_t *headers_list = header->request_header->headers_list;
        for (size_t i = 0; i < header->request_header->headers_list_size; i++)
        {
            const TinyHttpParserPair_t *pair = &headers_list[i];
#define FMT_LITERAL(literal) (literal), (sizeof(literal) - 1)
            if (tiny_http_parser_string_view_compare(&pair->key, FMT_LITERAL("Sec-WebSocket-Key")) == 0)
            {
                wsKey = &pair->value;
            }
        }
    }

    if (wsKey == nullptr)
    {
        clientContext->customError = "Not a websocket connect";
        return 0;
    }

    assert(wsKey->size == WEBSOCKET_KEY_SIZE);
    char accept_key[WEBSOCKET_ACCEPT_KEY_SIZE];
    size_t accept_key_size = websocket_accept(wsKey->data, accept_key);
    assert(accept_key_size == WEBSOCKET_ACCEPT_KEY_SIZE);

    const char *response = "HTTP/1.1 101 Switching Protocols\r\n"
                           "Upgrade: websocket\r\n"
                           "Connection: Upgrade\r\n"
                           "Sec-WebSocket-Accept: ";
    const char *end = "\r\n\r\n";
    send(clientContext->fd, response, strlen(response), 0);
    printf("[Sec-WebSocket-Accept]%.*s (%zu)\n", (int)accept_key_size, accept_key, accept_key_size);
    send(clientContext->fd, accept_key, accept_key_size, 0);
    send(clientContext->fd, end, strlen(end), 0);

    tiny_http_parser_websocket_init(&clientContext->wsContext,
                                    clientContext,
                                    _onWsControl,
                                    _onWsMessage,
                                    _onWsError,
                                    NULL,
                                    NULL);
    clientContext->wsUpgraded = true; // switch to ws
    return SIZE_MAX;                  // infinite body
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
    if (clientContext->wsUpgraded)
    {
        // forward data to websocket context
        return tiny_http_parser_websocket_input(&clientContext->wsContext, data, size, remain_not_recv_size);
    }
    clientContext->customError = "Recv body data before ws upgrade";
    return size;
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
        .wsUpgraded = false,
    };

    tiny_http_parser_init(&clientContext.thpContext, &clientContext,
                          _onHttpHeader,
                          _onHttpBodyStream,
                          NULL,
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

    if (iter->second.wsUpgraded)
    {
        tiny_http_parser_websocket_deinit(&iter->second.wsContext);
    }
    tiny_http_parser_deinit(&iter->second.thpContext);
    _clientContexts.erase(iter);
}

static void _onClientEvent(int fd, short revent, FdList *list)
{
    char buf[512]; // Buffer for client data

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
    { // We got some good data from a client
        printf("pollserver: recv from fd %d: %.*s\n", fd, nbytes, buf);

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
    }
    else
    {
        _openClient(newfd, list);
        char remoteIP[INET6_ADDRSTRLEN];
        printf("pollserver: new connection from %s on socket %d\n",
               inet_ntop2(&remoteaddr, remoteIP, sizeof remoteIP),
               newfd);
    }
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

    printf("pollserver: waiting for connections (ws://localhost:" PORT "/) ...\n");

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
