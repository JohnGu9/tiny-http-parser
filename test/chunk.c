#include "common.h"
#include "tiny-http-parser.h"

#include <stdio.h>
#include <stdlib.h>

static void test_simple_chunk();

int main()
{
    test_simple_chunk();
    return 0;
}

static void *realloc_buffer_chunk(TinyHttpParserChunkContext_t *context, void *buffer, TINY_HTTP_PARSER_SIZE_T new_size)
{
    void *new_buffer = realloc(buffer, new_size);
    printf("realloc_buffer_chunk: [buffer](%p) [new_buffer](%p) [new_size](%zu)\n", buffer, new_buffer, new_size);
    return new_buffer;
}

static void free_buffer_chunk(TinyHttpParserChunkContext_t *context, void *buffer, TINY_HTTP_PARSER_SIZE_T current_size)
{
    printf("free_buffer_chunk: [buffer](%p) [current_size](%zu)\n", buffer, current_size);
    free(buffer);
}

static void print_chunk_size(TinyHttpParserChunkContext_t *context, TINY_HTTP_PARSER_SIZE_T size)
{
    printf("[chunk size](%zu)\n", size);
}

static void print_chunk(TinyHttpParserChunkContext_t *context, const char *buffer, TINY_HTTP_PARSER_SIZE_T size)
{
    printf("[chunk](%zu) %.*s\n", size, (int)size, buffer);
}

static void test_simple_chunk()
{
    printf("\ntest_simple_chunk:\n");

    TinyHttpParserChunkContext_t context;
    tiny_http_parser_chunk_init(&context, NULL, print_chunk_size, print_chunk, NULL, realloc_buffer_chunk, free_buffer_chunk);

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
