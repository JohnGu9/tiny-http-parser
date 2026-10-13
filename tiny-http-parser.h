#ifndef TINY_HTTP_PARSER_H
#define TINY_HTTP_PARSER_H

#ifdef TINY_HTTP_PARSER_CONFIG
#include "tiny-http-parser-config.h"
#endif

#include "cc.h"
#include <string.h>

#ifndef TINY_HTTP_PARSER_SIZE_T // must be unsigned type
#define TINY_HTTP_PARSER_SIZE_T size_t
#endif

#ifdef __cplusplus
extern "C"
{
#endif

  // @TODO: add Transfer-Encoding: chunked

  typedef struct _TinyHttpParserStringView TinyHttpParserStringView_t;
  typedef struct _TinyHttpParserPair TinyHttpParserPair_t;
  typedef struct _TinyHttpParserContext TinyHttpParserContext_t;
  typedef struct _TinyHttpParserHeader TinyHttpParserHeader_t;
  typedef struct _TinyHttpParserRequestHeader TinyHttpParserRequestHeader_t;
  typedef struct _TinyHttpParserResponseHeader TinyHttpParserResponseHeader_t;

  struct _TinyHttpParserStringView
  {
    const char *data;
    TINY_HTTP_PARSER_SIZE_T size;
  };

  struct _TinyHttpParserPair
  {
    TinyHttpParserStringView_t raw;
    TinyHttpParserStringView_t key;
    TinyHttpParserStringView_t value;
  };

  struct _TinyHttpParserRequestHeader
  {
    TinyHttpParserStringView_t method;
    TinyHttpParserStringView_t path;
    TinyHttpParserStringView_t protocol;
    vec(TinyHttpParserPair_t) * headers;
  };

  struct _TinyHttpParserResponseHeader
  {
    TinyHttpParserStringView_t protocol;
    TinyHttpParserStringView_t status_code;
    TinyHttpParserStringView_t reason_phrase;
    vec(TinyHttpParserPair_t) * headers;
  };

  struct _TinyHttpParserHeader
  {
    TinyHttpParserStringView_t raw;
    TinyHttpParserRequestHeader_t *request_header;
    TinyHttpParserResponseHeader_t *response_header;
  };

  struct _TinyHttpParserContext
  {
    void *user_context;
    int parser_status;
    char *header_buffer;
    TINY_HTTP_PARSER_SIZE_T header_buffer_size;
    TINY_HTTP_PARSER_SIZE_T header_buffer_max_size;
    TINY_HTTP_PARSER_SIZE_T expected_body_size;
    TINY_HTTP_PARSER_SIZE_T (*on_header)(TinyHttpParserContext_t *context, const TinyHttpParserHeader_t *header);                                                              // return expected body size
    TINY_HTTP_PARSER_SIZE_T (*on_body_stream)(TinyHttpParserContext_t *context, const char *data, TINY_HTTP_PARSER_SIZE_T size, TINY_HTTP_PARSER_SIZE_T remain_not_recv_size); // return override recv body size
    void (*on_error)(TinyHttpParserContext_t *context, const char *reason);
    char *(*alloc_header_buffer)(TinyHttpParserContext_t *context, TINY_HTTP_PARSER_SIZE_T *header_buffer_max_size);
    void (*free_header_buffer)(TinyHttpParserContext_t *context, char *header_buffer, TINY_HTTP_PARSER_SIZE_T header_buffer_max_size);
  };

  void tiny_http_parser_init(TinyHttpParserContext_t *context,
                             void *user_context,
                             TINY_HTTP_PARSER_SIZE_T (*on_header)(TinyHttpParserContext_t *context, const TinyHttpParserHeader_t *header),
                             TINY_HTTP_PARSER_SIZE_T (*on_body_stream)(TinyHttpParserContext_t *context, const char *data, TINY_HTTP_PARSER_SIZE_T size, TINY_HTTP_PARSER_SIZE_T remain_not_recv_size),
                             void (*on_error)(TinyHttpParserContext_t *context, const char *reason),
                             char *(*alloc_header_buffer)(TinyHttpParserContext_t *context, TINY_HTTP_PARSER_SIZE_T *header_buffer_max_size),
                             void (*free_header_buffer)(TinyHttpParserContext_t *context, char *header_buffer, TINY_HTTP_PARSER_SIZE_T header_buffer_max_size));
  void tiny_http_parser_input(TinyHttpParserContext_t *context, const char *data, TINY_HTTP_PARSER_SIZE_T size);
  void tiny_http_parser_deinit(TinyHttpParserContext_t *context);

  TINY_HTTP_PARSER_SIZE_T tiny_http_parser_default_on_header(TinyHttpParserContext_t *context, const TinyHttpParserHeader_t *header);

  // extension

  typedef struct _TinyHttpParserMultiPartContext TinyHttpParserMultiPartContext_t;

  struct _TinyHttpParserMultiPartContext
  {
    void *user_context;
    int parser_status;
    TinyHttpParserStringView_t boundary;
    char *buffer;
    TINY_HTTP_PARSER_SIZE_T buffer_size;
    TINY_HTTP_PARSER_SIZE_T buffer_max_size;
    void (*on_part)(TinyHttpParserMultiPartContext_t *context, vec(TinyHttpParserPair_t) * headers, const char *data, TINY_HTTP_PARSER_SIZE_T size);
    void (*on_error)(TinyHttpParserMultiPartContext_t *context, const char *reason);
    char *(*alloc_multi_part_buffer)(TinyHttpParserMultiPartContext_t *context, TINY_HTTP_PARSER_SIZE_T *buffer_max_size);
    void (*free_multi_part_buffer)(TinyHttpParserMultiPartContext_t *context, char *header_buffer, TINY_HTTP_PARSER_SIZE_T buffer_max_size);
  };

  void tiny_http_parser_multi_part_init(TinyHttpParserMultiPartContext_t *context, const TinyHttpParserStringView_t *boundary,
                                        void *user_context,
                                        void (*on_part)(TinyHttpParserMultiPartContext_t *context, vec(TinyHttpParserPair_t) * headers, const char *data, TINY_HTTP_PARSER_SIZE_T size),
                                        void (*on_error)(TinyHttpParserMultiPartContext_t *context, const char *reason),
                                        char *(*alloc_multi_part_buffer)(TinyHttpParserMultiPartContext_t *context, TINY_HTTP_PARSER_SIZE_T *buffer_max_size),
                                        void (*free_multi_part_buffer)(TinyHttpParserMultiPartContext_t *context, char *header_buffer, TINY_HTTP_PARSER_SIZE_T buffer_max_size));
  TINY_HTTP_PARSER_SIZE_T tiny_http_parser_multi_part_input(TinyHttpParserMultiPartContext_t *context, const char *data, TINY_HTTP_PARSER_SIZE_T size);
  void tiny_http_parser_multi_part_deinit(TinyHttpParserMultiPartContext_t *context);

#ifdef __cplusplus
}
#endif

#endif
