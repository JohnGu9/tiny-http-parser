#include "tiny-http-parser.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static void on_ws_control(TinyHttpParserWebsocketContext_t *context, uint8_t opcode, const char *data, size_t size)
{
    const char *messageType;
    switch (opcode)
    {
    case 8:
        messageType = "Close";
        break;
    case 9:
        messageType = "Ping";
        break;
    case 10:
        messageType = "Pong";
        break;
    default:
        messageType = "Unknown";
        break;
    }
    printf("[Message(%s)]%.*s\n", messageType, (int)size, data);
}

static void on_ws_message(TinyHttpParserWebsocketContext_t *context, uint8_t opcode, const char *data, size_t size)
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
}

int main()
{
    tiny_http_parser_hook_init(realloc, free);

    TinyHttpParserWebsocketContext_t context;

    tiny_http_parser_websocket_init(&context,
                                    NULL,
                                    on_ws_control,
                                    on_ws_message,
                                    NULL,
                                    NULL,
                                    NULL);

    char data_frame_header[14];
    size_t data_frame_header_size;
    uint64_t payload_size;

    payload_size = 4;
    data_frame_header_size = tiny_http_parser_websocket_build_data_frame_header(data_frame_header, 1, 1, payload_size, NULL);
    tiny_http_parser_websocket_input(&context, data_frame_header, data_frame_header_size, NULL);
    {
        const char data_frame_payload[] = {'a', 'b', 'c', 'd'};
        assert(sizeof(data_frame_payload) == payload_size);
        tiny_http_parser_websocket_input(&context, data_frame_payload, payload_size, NULL);
    }

    payload_size = 5;
    data_frame_header_size = tiny_http_parser_websocket_build_data_frame_header(data_frame_header, 0, 1, payload_size, NULL);
    tiny_http_parser_websocket_input(&context, data_frame_header, data_frame_header_size, NULL);
    {
        const char data_frame_payload[] = {'a', 'b', 'c', 'd', 'e'};
        assert(sizeof(data_frame_payload) == payload_size);
        tiny_http_parser_websocket_input(&context, data_frame_payload, payload_size, NULL);
    }

    payload_size = 3;
    data_frame_header_size = tiny_http_parser_websocket_build_data_frame_header(data_frame_header, 1, 1, payload_size, NULL);
    tiny_http_parser_websocket_input(&context, data_frame_header, data_frame_header_size, NULL);
    {
        const char data_frame_payload[] = {'a', 'b', 'c'};
        assert(sizeof(data_frame_payload) == payload_size);
        tiny_http_parser_websocket_input(&context, data_frame_payload, payload_size, NULL);
    }

    payload_size = 0;
    data_frame_header_size = tiny_http_parser_websocket_build_data_frame_header(data_frame_header, 1, 8, payload_size, NULL);
    tiny_http_parser_websocket_input(&context, data_frame_header, data_frame_header_size, NULL);

    tiny_http_parser_websocket_deinit(&context);

    return 0;
}
