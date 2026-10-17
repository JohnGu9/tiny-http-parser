#include "common.h"

#include <stdio.h>
#include <stdlib.h>

void print_headers_list(const TinyHttpParserPair_t *headers_list, size_t headers_list_size)
{
    // warning:
    // headers_list can be null when realloc_buffer failed
    if (headers_list == NULL)
    {
        printf("a null headers list with size (%zu)\n", headers_list_size);
        return;
    }
    for (size_t i = 0; i < headers_list_size; i++)
    {
        const TinyHttpParserPair_t *el = &headers_list[i];
        printf("%.*s: %.*s\n", FMT_STRING_VIEW(el->key), FMT_STRING_VIEW(el->value));
    }
}

size_t print_header(TinyHttpParserContext_t *context, const TinyHttpParserHeader_t *header)
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

size_t print_body_stream(TinyHttpParserContext_t *context, const char *data, size_t size, size_t *remain_not_recv_size)
{
    printf("size: %zu remain_not_recv_size: %zu data: \n%.*s\n", size, *remain_not_recv_size, (int)size, data);
    return size;
}

void on_error(TinyHttpParserContext_t *context, const char *reason)
{
    printf("on_error: %s\n", reason);
}

void *realloc_buffer(TinyHttpParserContext_t *context, void *buffer, size_t new_size)
{
    if (new_size > 16 * 1024)
    {
        // you can limit the buffer in your realloc callback
        printf("realloc_buffer: require too much memory. Refuse this requirement. ");
        return NULL;
    }
    void *new_buffer = realloc(buffer, new_size);
    printf("realloc_buffer: [buffer](%p) [new_buffer](%p) [new_size](%zu)\n", buffer, new_buffer, new_size);
    return new_buffer;
}

void free_buffer(TinyHttpParserContext_t *context, void *buffer, size_t current_size)
{
    printf("free_buffer: [buffer](%p) [current_size](%zu)\n", buffer, current_size);
    free(buffer);
}
