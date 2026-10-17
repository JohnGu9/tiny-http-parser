#include "common.h"
#include "tiny-http-parser.h"

#include <stdio.h>
#include <stdlib.h>

static void test_simple_multi_part();
static void test_multi_part();

int main()
{
    test_simple_multi_part();
    test_multi_part();
    return 0;
}

static void *realloc_buffer_multi_part(TinyHttpParserMultiPartContext_t *context, void *buffer, size_t new_size)
{
    void *new_buffer = realloc(buffer, new_size);
    printf("realloc_buffer_multi_part: [buffer](%p) [new_buffer](%p) [new_size](%zu)\n", buffer, new_buffer, new_size);
    return new_buffer;
}

static void free_buffer_multi_part(TinyHttpParserMultiPartContext_t *context, void *buffer, size_t current_size)
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
                    size_t headers_list_size,
                    const char *data, size_t size)
{
    print_headers_list(headers_list, headers_list_size);
    printf("on_part:\n%.*s\n", (int)size, data);
}

static size_t on_multi_part_body(TinyHttpParserContext_t *context, const char *data, size_t size, size_t *remain_not_recv_size)
{
    TinyHttpParserMultiPartContext_t *const mp_context = (TinyHttpParserMultiPartContext_t *)context->user_context;
    size_t recv_size = tiny_http_parser_multi_part_input(mp_context, data, size, remain_not_recv_size); // tiny_http_parser_multi_part_input handle the http body
    return recv_size;
}

static void on_part_completed(TinyHttpParserMultiPartContext_t *context);

static const char boundary_string[] = "hogehoge";
static TinyHttpParserStringView_t boundary = {
    .data = boundary_string,
    .size = sizeof(boundary_string) - 1,
};

static size_t on_multi_part_message_header(TinyHttpParserContext_t *context, const TinyHttpParserHeader_t *header)
{
#define is_string_view_equal_to(string_view, literal) ((string_view).size == (sizeof(literal) - 1) && strncmp(string_view.data, literal, sizeof(literal) - 1) == 0)

    for (size_t i = 0; i < header->request_header->headers_list_size; i++)
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
    size_t body_size = tiny_http_parser_default_on_header(context, header); // default handle get the body size from http headers
    printf("body_size: %zu\n", body_size);
    return body_size;
}

static void on_part_completed(TinyHttpParserMultiPartContext_t *context)
{
    printf("multi part completed. \n");
    tiny_http_parser_multi_part_deinit(context);
}

/**
 * @brief test_simple_multi_part
 * The basic use case.
 *
 */
static void test_simple_multi_part()
{
    printf("\ntest_simple_multi_part:\n");

    TinyHttpParserMultiPartContext_t mp_context;
    tiny_http_parser_multi_part_init(&mp_context,
                                     &boundary,
                                     NULL,
                                     on_part,
                                     NULL,
                                     on_multi_part_error,
                                     realloc_buffer_multi_part,
                                     free_buffer_multi_part);

    const char test_string_fragment1[] = "--hogehoge\r\n"
                                         "Content-Disposition: form-data; name=\"user_name\"\r\n"
                                         "\r\n"
                                         "test\r\n"
                                         "--hogehoge\r\n"
                                         "Content-Disposition: form-data; name=\"email\"\r\n"
                                         "\r\n"
                                         "sample@mail.com\r\n";
    const char test_string_fragment2[] = "--hogehoge--\r\n";
    tiny_http_parser_multi_part_input(&mp_context, test_string_fragment1, sizeof(test_string_fragment1) - 1, NULL);
    tiny_http_parser_multi_part_input(&mp_context, test_string_fragment2, sizeof(test_string_fragment2) - 1, NULL);

    tiny_http_parser_multi_part_deinit(&mp_context);
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
    tiny_http_parser_init(&context, &mp_context, on_multi_part_message_header, on_multi_part_body, NULL, on_error, realloc_buffer, free_buffer);

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
}
