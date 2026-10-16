#include "common.h"
#include "tiny-http-parser.h"

#include <stdio.h>
#include <stdlib.h>

static void test0();
static void test1();
static void test2();
static void test3();

int main()
{
    test0();
    test1();
    test2();
    test3();
    return 0;
}

/**
 * @brief test0
 * The basic use case. Single response data input.
 *
 */
static void test0()
{
    printf("\ntest0:\n");
    TinyHttpParserContext_t context;
    tiny_http_parser_init(&context, NULL, print_header, NULL, NULL, on_error, realloc_buffer, free_buffer);

    const char test_string[] = "HTTP/1.1          200 OK\r\n"
                               "Content-Type: text/html\r\n"
                               "\r\n";
    tiny_http_parser_input(&context, test_string, sizeof(test_string) - 1);

    tiny_http_parser_deinit(&context);
}

/**
 * @brief test1
 * The basic use case. Single request data input.
 *
 */
static void test1()
{
    printf("\ntest1:\n");
    TinyHttpParserContext_t context;
    tiny_http_parser_init(&context, NULL, print_header, NULL, NULL, on_error, realloc_buffer, free_buffer);

    const char test_string[] = "GET /     HTTP/1.1\r\n"
                               "Host: 192.168.7.1\r\n"
                               "Accept: */*\r\n"
                               "\r\n";
    tiny_http_parser_input(&context, test_string, sizeof(test_string) - 1);

    tiny_http_parser_deinit(&context);
}

/**
 * @brief test2
 * The advance use case. Multi response/request input split into multi fragments.
 *
 */
static void test2()
{
    printf("\ntest2:\n");
    TinyHttpParserContext_t context;
    tiny_http_parser_init(&context, NULL, print_header, NULL, NULL, on_error, realloc_buffer, free_buffer);

    const char test_string_fragment0[] = "HTTP/1.1 200 OK\r\n"
                                         "Content-";
    const char test_string_fragment1[] = "Type: text/html\r\n"
                                         "\r\n"
                                         "GET  / HTTP/1.1\r\n";
    const char test_string_fragment2[] = "Host:       192.168.7.1\r\n"
                                         "Accept: */*\r\n"
                                         "\r\n";
    tiny_http_parser_input(&context, test_string_fragment0, sizeof(test_string_fragment0) - 1);
    tiny_http_parser_input(&context, test_string_fragment1, sizeof(test_string_fragment1) - 1);
    tiny_http_parser_input(&context, test_string_fragment2, sizeof(test_string_fragment2) - 1);

    tiny_http_parser_deinit(&context);
}

static TINY_HTTP_PARSER_SIZE_T print_content_length(TinyHttpParserContext_t *context, const TinyHttpParserHeader_t *header)
{
    print_header(context, header);
    TINY_HTTP_PARSER_SIZE_T body_size = tiny_http_parser_default_on_header(context, header); // default handle get the body size from http headers
    printf("body_size: %zu\n", body_size);
    return body_size; // return a expected body size
}

static void print_body_completed(TinyHttpParserContext_t *context)
{
    printf("http body completed\n");
}

/**
 * @brief test3
 * The advance use case. Input come with body.
 *
 */
static void test3()
{
    printf("\ntest3:\n");
    TinyHttpParserContext_t context;
    tiny_http_parser_init(&context, NULL, print_content_length, print_body_stream, print_body_completed, on_error, realloc_buffer, free_buffer);

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
