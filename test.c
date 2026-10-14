#include "tiny-http-parser.h"

#include <stdio.h>
#include <stdlib.h>

static void test0();
static void test1();
static void test2();
static void test3();
static void test_multi_part();

int main()
{
    test0();
    test1();
    test2();
    test3();
    test_multi_part();
    return 0;
}

#define FMT_STRING_VIEW(string_view) (int)(string_view).size, (string_view).data

static void print_headers_list(const TinyHttpParserPair_t *headers_list, TINY_HTTP_PARSER_SIZE_T headers_list_size)
{
    for (TINY_HTTP_PARSER_SIZE_T i = 0; i < headers_list_size; i++)
    {
        const TinyHttpParserPair_t *el = &headers_list[i];
        printf("%.*s: %.*s\n", FMT_STRING_VIEW(el->key), FMT_STRING_VIEW(el->value));
    }
}

static TINY_HTTP_PARSER_SIZE_T print_header(TinyHttpParserContext_t *context, const TinyHttpParserHeader_t *header)
{
    if (header->response_header != NULL)
    {
        printf("A response http message: \n");
        printf("%.*s %.*s %.*s\n", FMT_STRING_VIEW(header->response_header->protocol),
               FMT_STRING_VIEW(header->response_header->status_code),
               FMT_STRING_VIEW(header->response_header->reason_phrase));

        print_headers_list(header->response_header->headers_list, header->response_header->headers_list_size);
    }

    if (header->request_header != NULL)
    {
        printf("A request http message: \n");
        printf("%.*s %.*s %.*s\n", FMT_STRING_VIEW(header->request_header->method),
               FMT_STRING_VIEW(header->request_header->path),
               FMT_STRING_VIEW(header->request_header->protocol));

        print_headers_list(header->request_header->headers_list, header->request_header->headers_list_size);
    }

    return 0;
}

static TINY_HTTP_PARSER_SIZE_T print_body_stream(TinyHttpParserContext_t *context, const char *data, TINY_HTTP_PARSER_SIZE_T size, TINY_HTTP_PARSER_SIZE_T *remain_not_recv_size)
{
    printf("size: %zu remain_not_recv_size: %zu data: \n%.*s\n", size, *remain_not_recv_size, (int)size, data);
    return size;
}

static void on_error(TinyHttpParserContext_t *context, const char *reason)
{
    printf("on_error: %s\n", reason);
}

static void *realloc_buffer(TinyHttpParserContext_t *context, void *buffer, TINY_HTTP_PARSER_SIZE_T new_size)
{
    void *new_buffer = realloc(buffer, new_size);
    printf("realloc_buffer: [buffer](%p) [new_buffer](%p) [new_size](%zu)\n", buffer, new_buffer, new_size);
    return new_buffer;
}

static void free_buffer(TinyHttpParserContext_t *context, void *buffer, TINY_HTTP_PARSER_SIZE_T current_size)
{
    printf("free_buffer: [buffer](%p) [current_size](%zu)\n", buffer, current_size);
    free(buffer);
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
    tiny_http_parser_init(&context, NULL, print_header, print_body_stream, on_error, realloc_buffer, free_buffer);

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
    tiny_http_parser_init(&context, NULL, print_header, print_body_stream, on_error, realloc_buffer, free_buffer);

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
    tiny_http_parser_init(&context, NULL, print_header, print_body_stream, on_error, realloc_buffer, free_buffer);

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
    return body_size;
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
    tiny_http_parser_init(&context, NULL, print_content_length, print_body_stream, on_error, realloc_buffer, free_buffer);

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

static void *realloc_buffer_multi_part(TinyHttpParserMultiPartContext_t *context, void *buffer, TINY_HTTP_PARSER_SIZE_T new_size)
{
    void *new_buffer = realloc(buffer, new_size);
    printf("realloc_buffer_multi_part: [buffer](%p) [new_buffer](%p) [new_size](%zu)\n", buffer, new_buffer, new_size);
    return new_buffer;
}

static void free_buffer_multi_part(TinyHttpParserMultiPartContext_t *context, void *buffer, TINY_HTTP_PARSER_SIZE_T current_size)
{
    printf("free_buffer_multi_part: [buffer](%p) [current_size](%zu)\n", buffer, current_size);
    free(buffer);
}

static void on_multi_part_error(TinyHttpParserMultiPartContext_t *context, const char *reason)
{
    printf("on_multi_part_error: %s\n", reason);
}

static void on_part(TinyHttpParserMultiPartContext_t *context,
                    TinyHttpParserPair_t *headers_list,
                    TINY_HTTP_PARSER_SIZE_T headers_list_size,
                    const char *data, TINY_HTTP_PARSER_SIZE_T size)
{
    print_headers_list(headers_list, headers_list_size);
    printf("on_part:\n%.*s\n", (int)size, data);
}

static TINY_HTTP_PARSER_SIZE_T on_multi_part_body(TinyHttpParserContext_t *context, const char *data, TINY_HTTP_PARSER_SIZE_T size, TINY_HTTP_PARSER_SIZE_T *remain_not_recv_size)
{
    TinyHttpParserMultiPartContext_t *const mp_context = (TinyHttpParserMultiPartContext_t *)context->user_context;
    TINY_HTTP_PARSER_SIZE_T recv_size = tiny_http_parser_multi_part_input(mp_context, data, size, remain_not_recv_size); // tiny_http_parser_multi_part_input get the body size
    return recv_size;
}

static void on_part_completed(TinyHttpParserMultiPartContext_t *context);

static const char boundary_string[] = "hogehoge";
static TinyHttpParserStringView_t boundary = {
    .data = boundary_string,
    .size = sizeof(boundary_string) - 1,
};

static TINY_HTTP_PARSER_SIZE_T on_multi_part_message_header(TinyHttpParserContext_t *context, const TinyHttpParserHeader_t *header)
{
#define is_string_view_equal_to(string_view, literal) ((string_view).size == (sizeof(literal) - 1) && strncmp(string_view.data, literal, sizeof(literal) - 1) == 0)

    for (TINY_HTTP_PARSER_SIZE_T i = 0; i < header->request_header->headers_list_size; i++)
    {
        if (is_string_view_equal_to(header->request_header->headers_list[i].key, "Content-Type"))
        {
            if (is_string_view_equal_to(header->request_header->headers_list[i].value, "multipart/form-data; boundary=hogehoge;"))
            {
                printf("[OK]Verified header. \n");
                TinyHttpParserMultiPartContext_t *mp_context = tiny_http_parser_get_user_context(context, TinyHttpParserMultiPartContext_t *);
                tiny_http_parser_multi_part_init(mp_context,
                                                 &boundary,
                                                 NULL,
                                                 on_part,
                                                 on_part_completed,
                                                 on_multi_part_error,
                                                 realloc_buffer_multi_part,
                                                 free_buffer_multi_part);
            }
            else
            {
                printf("[Error]Unverified header. \n");
                context->on_error(context, "my error");
            }
        }
    }
    print_header(context, header);
    TINY_HTTP_PARSER_SIZE_T body_size = tiny_http_parser_default_on_header(context, header); // default handle get the body size from http headers
    printf("body_size: %zu\n", body_size);
    return body_size;
}

static void on_part_completed(TinyHttpParserMultiPartContext_t *context)
{
    printf("multi part completed. \n");
    tiny_http_parser_multi_part_deinit(context);
    // when multi part completed
    // just deinit the context
    // context can't be reused until reinit
}

/**
 * @brief test_multi_part
 * The advance use case. Input come with multi part.
 *
 */
static void test_multi_part()
{
    printf("\ntest_multi_part:\n");

    TinyHttpParserMultiPartContext_t mp_context;

    TinyHttpParserContext_t context;
    tiny_http_parser_init(&context, &mp_context, on_multi_part_message_header, on_multi_part_body, on_error, realloc_buffer, free_buffer);

    const char test_string_fragment0[] = "GET / HTTP/1.1\r\n"
                                         "Host: 192.168.7.1\r\n"
                                         "Content-Type: multipart/form-data; boundary=hogehoge;\r\n";
    const char test_string_fragment1[] = "\r\n"
                                         "--hogehoge\r\n"
                                         "Content-Disposition: form-data; name=\"user_name\"\r\n"
                                         "\r\n"
                                         "test\r\n"
                                         "--hogehoge\r\n"
                                         "Content-Disposition: form-data; name=\"email\"\r\n"
                                         "\r\n"
                                         "sample@mail.com\r\n";
    const char test_string_fragment2[] = "--hogehoge--\r\n"
                                         "GET / HTTP/1.1\r\n"
                                         "Host: 192.168.7.1\r\n";
    const char test_string_fragment3[] = "Accept: */*\r\n"
                                         "\r\n";
    tiny_http_parser_input(&context, test_string_fragment0, sizeof(test_string_fragment0) - 1);
    tiny_http_parser_input(&context, test_string_fragment1, sizeof(test_string_fragment1) - 1);
    tiny_http_parser_input(&context, test_string_fragment2, sizeof(test_string_fragment2) - 1);
    tiny_http_parser_input(&context, test_string_fragment3, sizeof(test_string_fragment3) - 1);

    tiny_http_parser_deinit(&context);
    tiny_http_parser_multi_part_deinit(&mp_context);
}
