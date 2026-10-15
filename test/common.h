#ifndef COMMON_H
#define COMMON_H

#include "tiny-http-parser.h"

#define FMT_STRING_VIEW(string_view) (int)(string_view).size, (string_view).data

#ifdef __cplusplus
extern "C"
{
#endif

    void print_headers_list(const TinyHttpParserPair_t *headers_list, TINY_HTTP_PARSER_SIZE_T headers_list_size);

    TINY_HTTP_PARSER_SIZE_T print_header(TinyHttpParserContext_t *context, const TinyHttpParserHeader_t *header);
    TINY_HTTP_PARSER_SIZE_T print_body_stream(TinyHttpParserContext_t *context, const char *data, TINY_HTTP_PARSER_SIZE_T size, TINY_HTTP_PARSER_SIZE_T *remain_not_recv_size);
    void on_error(TinyHttpParserContext_t *context, const char *reason);
    void *realloc_buffer(TinyHttpParserContext_t *context, void *buffer, TINY_HTTP_PARSER_SIZE_T new_size);
    void free_buffer(TinyHttpParserContext_t *context, void *buffer, TINY_HTTP_PARSER_SIZE_T current_size);

#ifdef __cplusplus
}
#endif

#endif
