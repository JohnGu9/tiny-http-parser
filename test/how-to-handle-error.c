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

static void *no_buffer_alloc(TinyHttpParserContext_t *context, void *buffer, size_t size)
{
    printf("no_buffer_alloc: refuse to alloc memory for causing error. \n");
    return NULL;
}

/**
 * @brief test
 *
 */
static void test()
{
    printf("\ntest:\n");
    TinyHttpParserContext_t context;
    tiny_http_parser_init(&context, NULL, NULL, NULL, NULL, on_error, no_buffer_alloc, free_buffer);

    const char test_string_fragment0[] = "HTTP/1.1 200 OK\r\n"
                                         "Content-";
    const char test_string_fragment1[] = "Type: text/html\r\n"
                                         "\r\n"
                                         "GET  / HTTP/1.1\r\n";
    const char test_string_fragment2[] = "Host:       192.168.7.1\r\n"
                                         "Accept: */*\r\n"
                                         "\r\n";
    tiny_http_parser_input(&context, test_string_fragment0, sizeof(test_string_fragment0) - 1);

    /**
     * @brief `tiny_http_parser_get_error_reason()` only get the last error reason.
     *        in some case, one tiny_http_parser_input may incount multi time errors.
     *        because some slight errors would not stop next processes inside tiny-http-parser.
     *        if you want to handle this situation, register `on_error()` callback in init step.
     */
    const char *error_reason = tiny_http_parser_get_error_reason(&context);
    if (error_reason != NULL)
    {
        printf("Incount some error: %s\n", error_reason);
        printf("Give up next processes. ");
        tiny_http_parser_deinit(&context);
        return;
    }

    tiny_http_parser_input(&context, test_string_fragment1, sizeof(test_string_fragment1) - 1);
    tiny_http_parser_input(&context, test_string_fragment2, sizeof(test_string_fragment2) - 1);

    tiny_http_parser_deinit(&context);
}
