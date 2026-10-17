#ifndef COMMON_H
#define COMMON_H

#include "tiny-http-parser.h"

#define FMT_STRING_VIEW(string_view) (int)(string_view).size, (string_view).data

#ifdef __cplusplus
extern "C"
{
#endif

    void print_headers_list(const TinyHttpParserPair_t *headers_list, size_t headers_list_size);

    size_t print_header(TinyHttpParserContext_t *context, const TinyHttpParserHeader_t *header);
    size_t print_body_stream(TinyHttpParserContext_t *context, const char *data, size_t size, size_t *remain_not_recv_size);
    void on_error(TinyHttpParserContext_t *context, const char *reason);
    void *realloc_buffer(TinyHttpParserContext_t *context, void *buffer, size_t new_size);
    void free_buffer(TinyHttpParserContext_t *context, void *buffer, size_t current_size);

#ifdef __cplusplus
}
#endif

#endif
