#include "common.h"
#include "tiny-http-parser.h"

#include <stdio.h>
#include <stdlib.h>

static void test_simple_chunk();
static void test_chunk();

int main()
{
    test_simple_chunk();
    test_chunk();
    return 0;
}

static void *realloc_buffer_chunk(TinyHttpParserChunkContext_t *context, void *buffer, size_t new_size)
{
    void *new_buffer = realloc(buffer, new_size);
    printf("realloc_buffer_chunk: [buffer](%p) [new_buffer](%p) [new_size](%zu)\n", buffer, new_buffer, new_size);
    return new_buffer;
}

static void free_buffer_chunk(TinyHttpParserChunkContext_t *context, void *buffer, size_t current_size)
{
    printf("free_buffer_chunk: [buffer](%p) [current_size](%zu)\n", buffer, current_size);
    free(buffer);
}

static void print_chunk_size(TinyHttpParserChunkContext_t *context, size_t size)
{
    printf("[chunk size](%zu)\n", size);
}

static void print_chunk(TinyHttpParserChunkContext_t *context, const char *buffer, size_t size)
{
    printf("[chunk](%zu) %.*s\n", size, (int)size, buffer);
}

static void test_simple_chunk()
{
    printf("\ntest_simple_chunk:\n");

    TinyHttpParserChunkContext_t context;
    tiny_http_parser_chunk_init(&context, NULL, print_chunk_size, print_chunk, NULL, NULL, realloc_buffer_chunk, free_buffer_chunk);

    const char test_string_fragment1[] = "7\r\n"
                                         "Welcome\r\n"
                                         "1";
    const char test_string_fragment2[] = "c\r\n"
                                         "to Mozilla Developer Network\r";
    const char test_string_fragment3[] = "\n"
                                         "0\r\n"
                                         "\r\n";

    tiny_http_parser_chunk_input(&context, test_string_fragment1, sizeof(test_string_fragment1) - 1, NULL);
    tiny_http_parser_chunk_input(&context, test_string_fragment2, sizeof(test_string_fragment2) - 1, NULL);
    tiny_http_parser_chunk_input(&context, test_string_fragment3, sizeof(test_string_fragment3) - 1, NULL);

    tiny_http_parser_chunk_deinit(&context);
}

static void on_chunk_completed(TinyHttpParserChunkContext_t *context);
static size_t on_chunk_message_header(TinyHttpParserContext_t *context, const TinyHttpParserHeader_t *header)
{
#define FMT_LITERAL(literal) (literal), (sizeof(literal) - 1)

    if (header->response_header != NULL)
    {
        for (size_t i = 0; i < header->response_header->headers_list_size; i++)
        {
            const TinyHttpParserPair_t *pair = &(header->response_header->headers_list[i]);
            if (tiny_http_parser_string_view_compare(&(pair->key), FMT_LITERAL("Transfer-Encoding")) == 0)
            {
                if (tiny_http_parser_string_view_compare(&(pair->value), FMT_LITERAL("chunked")) == 0)
                {
                    printf("[OK]Verified header. \n");
                    TinyHttpParserChunkContext_t *chunk_context = tiny_http_parser_get_user_context(context, TinyHttpParserChunkContext_t *);
                    tiny_http_parser_chunk_init(chunk_context, NULL,
                                                print_chunk_size,
                                                print_chunk,
                                                on_chunk_completed,
                                                NULL,
                                                realloc_buffer_chunk,
                                                free_buffer_chunk);
                }
                else
                {
                    printf("[Error]Unverified header. \n");
                    context->on_error(context, "my error");
                }
            }
        }
    }
    print_header(context, header);
    size_t body_size = tiny_http_parser_default_on_header(context, header); // default handle get the body size from http headers
    printf("body_size: %zu\n", body_size);
    return body_size;
}

static void on_chunk_completed(TinyHttpParserChunkContext_t *context)
{
    printf("chunk completed. \n");
    tiny_http_parser_chunk_deinit(context);
}

static size_t on_chunk_body(TinyHttpParserContext_t *context, const char *data, size_t size, size_t *remain_not_recv_size)
{
    TinyHttpParserChunkContext_t *const chunk_context = tiny_http_parser_get_user_context(context, TinyHttpParserChunkContext_t *);
    size_t recv_size = tiny_http_parser_chunk_input(chunk_context, data, size, remain_not_recv_size); // tiny_http_parser_multi_part_input get the body size
    return recv_size;
}

static void test_chunk()
{
    printf("\ntest_chunk:\n");

    TinyHttpParserChunkContext_t chunk_context;
    TinyHttpParserContext_t context;
    tiny_http_parser_init(&context, &chunk_context, on_chunk_message_header, on_chunk_body, NULL, on_error, realloc_buffer, free_buffer);

    const char test_string_fragment0[] = "HTTP/1.1 200 OK\r\n"
                                         "Content-Type: text/plain\r\n";
    const char test_string_fragment1[] = "Transfer-Encoding: chunked\r\n"
                                         "\r\n"
                                         "7\r\n"
                                         "Welcome\r\n"
                                         "1c\r\n";
    const char test_string_fragment2[] = "to Mozilla Developer Network\r\n"
                                         "0\r\n"
                                         "\r\n"
                                         "GET / HTTP/1.1\r\n"
                                         "Host: 192.168.7.1\r\n";
    const char test_string_fragment3[] = "Accept: */*\r\n"
                                         "\r\n";
    tiny_http_parser_input(&context, test_string_fragment0, sizeof(test_string_fragment0) - 1);
    tiny_http_parser_input(&context, test_string_fragment1, sizeof(test_string_fragment1) - 1);
    tiny_http_parser_input(&context, test_string_fragment2, sizeof(test_string_fragment2) - 1);
    tiny_http_parser_input(&context, test_string_fragment3, sizeof(test_string_fragment3) - 1);

    tiny_http_parser_deinit(&context);
}
