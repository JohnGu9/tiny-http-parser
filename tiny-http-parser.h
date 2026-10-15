#ifndef TINY_HTTP_PARSER_H
#define TINY_HTTP_PARSER_H

#ifdef TINY_HTTP_PARSER_CONFIG
#include "tiny-http-parser-config.h"
#endif

#include <string.h>

#ifndef TINY_HTTP_PARSER_SIZE_T // must be unsigned type
#define TINY_HTTP_PARSER_SIZE_T size_t
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#define tiny_http_parser_get_user_context(context, T) (T)((context)->user_context)
#define tiny_http_parser_set_user_context(context, user_context) (context)->user_context = (void *)(user_context)

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
    TinyHttpParserPair_t *headers_list;
    TINY_HTTP_PARSER_SIZE_T headers_list_size;
  };

  struct _TinyHttpParserResponseHeader
  {
    TinyHttpParserStringView_t protocol;
    TinyHttpParserStringView_t status_code;
    TinyHttpParserStringView_t reason_phrase;
    TinyHttpParserPair_t *headers_list;
    TINY_HTTP_PARSER_SIZE_T headers_list_size;
  };

  struct _TinyHttpParserHeader
  {
    TinyHttpParserStringView_t raw;
    TinyHttpParserRequestHeader_t *request_header;
    TinyHttpParserResponseHeader_t *response_header;
  };

  typedef TINY_HTTP_PARSER_SIZE_T (*tiny_http_parser_on_header_fn)(TinyHttpParserContext_t *context, const TinyHttpParserHeader_t *header);                                                               // return expected body size
  typedef TINY_HTTP_PARSER_SIZE_T (*tiny_http_parser_on_body_stream_fn)(TinyHttpParserContext_t *context, const char *data, TINY_HTTP_PARSER_SIZE_T size, TINY_HTTP_PARSER_SIZE_T *remain_not_recv_size); // return override recv body size
  typedef void (*tiny_http_parser_on_error_fn)(TinyHttpParserContext_t *context, const char *reason);
  typedef void *(*tiny_http_parser_realloc_buffer_fn)(TinyHttpParserContext_t *context, void *buffer, TINY_HTTP_PARSER_SIZE_T new_size);
  typedef void (*tiny_http_parser_free_buffer_fn)(TinyHttpParserContext_t *context, void *buffer, TINY_HTTP_PARSER_SIZE_T current_size);

  struct _TinyHttpParserContext
  {
    void *user_context;
    int parser_status;

    char *header_buffer;
    TINY_HTTP_PARSER_SIZE_T header_buffer_size;

    TINY_HTTP_PARSER_SIZE_T expected_body_size;

    tiny_http_parser_on_header_fn on_header;           // return expected body size
    tiny_http_parser_on_body_stream_fn on_body_stream; // return override recv body size
    tiny_http_parser_on_error_fn on_error;
    tiny_http_parser_realloc_buffer_fn realloc_buffer;
    tiny_http_parser_free_buffer_fn free_buffer;
  };

  void tiny_http_parser_init(TinyHttpParserContext_t *context,
                             void *user_context,
                             tiny_http_parser_on_header_fn on_header,
                             tiny_http_parser_on_body_stream_fn on_body_stream,
                             tiny_http_parser_on_error_fn on_error,
                             tiny_http_parser_realloc_buffer_fn alloc_buffer,
                             tiny_http_parser_free_buffer_fn free_buffer);
  void tiny_http_parser_input(TinyHttpParserContext_t *context, const char *data, TINY_HTTP_PARSER_SIZE_T size);
  void tiny_http_parser_deinit(TinyHttpParserContext_t *context);

  TINY_HTTP_PARSER_SIZE_T tiny_http_parser_default_on_header(TinyHttpParserContext_t *context, const TinyHttpParserHeader_t *header);

  // extension

  // multi part

  typedef struct _TinyHttpParserMultiPartContext TinyHttpParserMultiPartContext_t;

  typedef void (*tiny_http_parser_multi_part_on_part_fn)(TinyHttpParserMultiPartContext_t *context,
                                                         TinyHttpParserPair_t *headers_list,
                                                         TINY_HTTP_PARSER_SIZE_T headers_list_size,
                                                         const char *data, TINY_HTTP_PARSER_SIZE_T size);
  typedef void (*tiny_http_parser_multi_part_on_part_completed_fn)(TinyHttpParserMultiPartContext_t *context);
  typedef void (*tiny_http_parser_multi_part_on_error_fn)(TinyHttpParserMultiPartContext_t *context, const char *reason);
  typedef void *(*tiny_http_parser_multi_part_realloc_buffer_fn)(TinyHttpParserMultiPartContext_t *context, void *buffer, TINY_HTTP_PARSER_SIZE_T new_size);
  typedef void (*tiny_http_parser_multi_part_free_buffer_fn)(TinyHttpParserMultiPartContext_t *context, void *buffer, TINY_HTTP_PARSER_SIZE_T buffer_size);

  struct _TinyHttpParserMultiPartContext
  {
    void *user_context;
    int parser_status;
    TinyHttpParserStringView_t boundary;

    TinyHttpParserPair_t *headers_list;
    TINY_HTTP_PARSER_SIZE_T headers_list_size;

    char *buffer;
    TINY_HTTP_PARSER_SIZE_T buffer_size;

    tiny_http_parser_multi_part_on_part_fn on_part;
    tiny_http_parser_multi_part_on_part_completed_fn on_part_completed;
    tiny_http_parser_multi_part_on_error_fn on_error;
    tiny_http_parser_multi_part_realloc_buffer_fn realloc_buffer;
    tiny_http_parser_multi_part_free_buffer_fn free_buffer;
  };

  void tiny_http_parser_multi_part_init(TinyHttpParserMultiPartContext_t *context,
                                        const TinyHttpParserStringView_t *boundary,
                                        void *user_context,
                                        tiny_http_parser_multi_part_on_part_fn on_part /* nullable */,
                                        tiny_http_parser_multi_part_on_part_completed_fn on_part_completed /* nullable */,
                                        tiny_http_parser_multi_part_on_error_fn on_error /* nullable */,
                                        tiny_http_parser_multi_part_realloc_buffer_fn realloc_buffer,
                                        tiny_http_parser_multi_part_free_buffer_fn free_buffer);
  /* input http "BODY" data */
  TINY_HTTP_PARSER_SIZE_T tiny_http_parser_multi_part_input(TinyHttpParserMultiPartContext_t *context, const char *data, TINY_HTTP_PARSER_SIZE_T size, TINY_HTTP_PARSER_SIZE_T *remain_not_recv_size /* nullable */);
  void tiny_http_parser_multi_part_deinit(TinyHttpParserMultiPartContext_t *context);

  // chunk
  // @TODO: add Transfer-Encoding: chunked

  typedef struct _TinyHttpParserChunkContext TinyHttpParserChunkContext_t;

  typedef void (*tiny_http_parser_chunk_on_chunk_size_fn)(TinyHttpParserChunkContext_t *context, TINY_HTTP_PARSER_SIZE_T size);
  typedef void (*tiny_http_parser_chunk_on_chunk_fn)(TinyHttpParserChunkContext_t *context, const char *data, TINY_HTTP_PARSER_SIZE_T size);
  typedef void (*tiny_http_parser_chunk_on_error_fn)(TinyHttpParserChunkContext_t *context, const char *reason);
  typedef void *(*tiny_http_parser_chunk_realloc_buffer_fn)(TinyHttpParserChunkContext_t *context, void *buffer, TINY_HTTP_PARSER_SIZE_T new_size);
  typedef void (*tiny_http_parser_chunk_free_buffer_fn)(TinyHttpParserChunkContext_t *context, void *buffer, TINY_HTTP_PARSER_SIZE_T buffer_size);

  struct _TinyHttpParserChunkContext
  {
    void *user_context;
    int parser_status;
    char *buffer;
    TINY_HTTP_PARSER_SIZE_T buffer_size;
    TINY_HTTP_PARSER_SIZE_T expected_chunk_size;

    tiny_http_parser_chunk_on_chunk_size_fn on_chunk_size;
    tiny_http_parser_chunk_on_chunk_fn on_chunk;
    tiny_http_parser_chunk_on_error_fn on_error;
    tiny_http_parser_chunk_realloc_buffer_fn realloc_buffer;
    tiny_http_parser_chunk_free_buffer_fn free_buffer;
  };

  void tiny_http_parser_chunk_init(TinyHttpParserChunkContext_t *context,
                                   void *user_context,
                                   tiny_http_parser_chunk_on_chunk_size_fn on_chunk_size /* nullable */,
                                   tiny_http_parser_chunk_on_chunk_fn on_chunk /* nullable */,
                                   tiny_http_parser_chunk_on_error_fn on_error /* nullable */,
                                   tiny_http_parser_chunk_realloc_buffer_fn realloc_buffer,
                                   tiny_http_parser_chunk_free_buffer_fn free_buffer);
  TINY_HTTP_PARSER_SIZE_T tiny_http_parser_chunk_input(TinyHttpParserChunkContext_t *context, const char *data, TINY_HTTP_PARSER_SIZE_T size, TINY_HTTP_PARSER_SIZE_T *remain_not_recv_size /* nullable */);
  void tiny_http_parser_chunk_deinit(TinyHttpParserChunkContext_t *context);

#ifdef __cplusplus
}
#endif

#endif
