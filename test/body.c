#include "common.h"
#include "tiny-http-parser.h"

#include <stdio.h>
#include <stdlib.h>

static void test();

int main()
{
    test();
    return 0;
}

static void *body_realloc_buffer(TinyHttpParserBodyContext_t *context, void *buffer, size_t new_size)
{
    void *new_buffer = realloc(buffer, new_size);
    printf("body_realloc_buffer: [buffer](%p) [new_buffer](%p) [new_size](%zu)\n", buffer, new_buffer, new_size);
    return new_buffer;
}

static void body_free_buffer(TinyHttpParserBodyContext_t *context, void *buffer, size_t current_size)
{
    printf("free_buffer: [buffer](%p) [current_size](%zu)\n", buffer, current_size);
    free(buffer);
}

static void on_body_completed(TinyHttpParserBodyContext_t *context, const char *data, size_t size)
{
    printf("[Body Completed]%.*s\n", (int)size, data);
}

static size_t on_header(TinyHttpParserContext_t *context, const TinyHttpParserHeader_t *header)
{
    print_header(context, header);

    TinyHttpParserBodyContext_t *body_context = tiny_http_parser_get_user_context(context, TinyHttpParserBodyContext_t *);
    tiny_http_parser_body_init(body_context, NULL, on_body_completed, NULL, body_realloc_buffer, body_free_buffer);

    return tiny_http_parser_default_on_header(context, header);
}

static size_t on_body_stream(TinyHttpParserContext_t *context, const char *data, size_t size, size_t *remain_not_recv_size)
{
    TinyHttpParserBodyContext_t *body_context = tiny_http_parser_get_user_context(context, TinyHttpParserBodyContext_t *);
    return tiny_http_parser_body_input(body_context, data, size, remain_not_recv_size);
}

static void on_completed(TinyHttpParserContext_t *context)
{
    TinyHttpParserBodyContext_t *body_context = tiny_http_parser_get_user_context(context, TinyHttpParserBodyContext_t *);
    tiny_http_parser_body_deinit(body_context);
}

/**
 * @brief test
 *
 */
static void test()
{
    printf("\ntest:\n");
    TinyHttpParserBodyContext_t body_context;
    TinyHttpParserContext_t context;
    tiny_http_parser_init(&context, &body_context, on_header, on_body_stream, on_completed, on_error, realloc_buffer, free_buffer);

    const char test_string_fragment0[] = "HTTP/1.1 200 OK\r\n"
                                         "Content-Type: text/html\r\n";
    const char test_string_fragment1[] = "Content-Length: 7\r\n"
                                         "\r\n"
                                         "abcdefg"
                                         "HTTP/1.1 200 OK\r\n";
    const char test_string_fragment2[] = "Content-Length: 8\r\n"
                                         "\r\n"
                                         "abcda";
    const char test_string_fragment3[] = "bcd";
    tiny_http_parser_input(&context, test_string_fragment0, sizeof(test_string_fragment0) - 1);
    tiny_http_parser_input(&context, test_string_fragment1, sizeof(test_string_fragment1) - 1);
    tiny_http_parser_input(&context, test_string_fragment2, sizeof(test_string_fragment2) - 1);
    tiny_http_parser_input(&context, test_string_fragment3, sizeof(test_string_fragment3) - 1);

    tiny_http_parser_deinit(&context);
}
