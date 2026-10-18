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

  typedef void *(*tiny_http_parser_realloc_fn)(void *buffer, size_t size);
  typedef void (*tiny_http_parser_free_fn)(void *buffer);
  /**
   * @brief if you init hook, you can ignore the realloc/free callback
   *        arguments in the init function, for example `tiny_http_parser_init()`
   *
   * @param realloc
   * @param free
   */
  void tiny_http_parser_hook_init(tiny_http_parser_realloc_fn realloc, tiny_http_parser_free_fn free);

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

#define StaticTinyHttpParserStringView(literal) \
  (TinyHttpParserStringView_t) { .data = (literal), .size = sizeof(literal) - 1, }

  int tiny_http_parser_string_view_compare(const TinyHttpParserStringView_t *string_view, const char *data, size_t size);

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
    const TinyHttpParserPair_t *headers_list;
    size_t headers_list_size;
  };

  struct _TinyHttpParserResponseHeader
  {
    TinyHttpParserStringView_t protocol;
    TinyHttpParserStringView_t status_code;
    TinyHttpParserStringView_t reason_phrase;
    const TinyHttpParserPair_t *headers_list;
    size_t headers_list_size;
  };

  struct _TinyHttpParserHeader
  {
    TinyHttpParserStringView_t raw;
    TinyHttpParserRequestHeader_t *request_header;
    TinyHttpParserResponseHeader_t *response_header;
  };

  typedef size_t (*tiny_http_parser_on_header_fn)(TinyHttpParserContext_t *context, const TinyHttpParserHeader_t *header);                                                                          // return expected body size
  typedef size_t (*tiny_http_parser_on_body_stream_fn)(TinyHttpParserContext_t *context, const char *data, size_t size, size_t *remain_not_recv_size /* override the remain expected body size */); // return consumed data size
  typedef void (*tiny_http_parser_on_body_completed_fn)(TinyHttpParserContext_t *context);
  typedef void (*tiny_http_parser_on_error_fn)(TinyHttpParserContext_t *context, const char *reason);
  typedef void *(*tiny_http_parser_realloc_buffer_fn)(TinyHttpParserContext_t *context, void *buffer, size_t new_size);
  typedef void (*tiny_http_parser_free_buffer_fn)(TinyHttpParserContext_t *context, void *buffer, size_t current_size);

  struct _TinyHttpParserContext
  {
    void *user_context;
    const char *error_reason;
    int parser_status;

    char *header_buffer;
    size_t header_buffer_size;

    size_t expected_body_size;

    tiny_http_parser_on_header_fn on_header;
    tiny_http_parser_on_body_stream_fn on_body_stream;
    tiny_http_parser_on_body_completed_fn on_body_completed;
    tiny_http_parser_on_error_fn on_error;
    tiny_http_parser_realloc_buffer_fn realloc_buffer;
    tiny_http_parser_free_buffer_fn free_buffer;
  };

  void tiny_http_parser_init(TinyHttpParserContext_t *context,
                             void *user_context,
                             tiny_http_parser_on_header_fn on_header /* nullable */,           // return expected body size
                             tiny_http_parser_on_body_stream_fn on_body_stream /* nullable */, // return consumed data size
                             tiny_http_parser_on_body_completed_fn on_body_completed /* nullable */,
                             tiny_http_parser_on_error_fn on_error /* nullable */,
                             tiny_http_parser_realloc_buffer_fn alloc_buffer /* nullable */,
                             tiny_http_parser_free_buffer_fn free_buffer /* nullable */);
  void tiny_http_parser_input(TinyHttpParserContext_t *context, const char *data, size_t size);
  void tiny_http_parser_deinit(TinyHttpParserContext_t *context);

  // helper function

#define tiny_http_parser_get_user_context(context, T) (T)((context)->user_context)
#define tiny_http_parser_set_user_context(context, user_context) (context)->user_context = (void *)(user_context)
#define tiny_http_parser_get_error_reason(context) ((context)->error_reason)

  size_t tiny_http_parser_default_on_header(TinyHttpParserContext_t *context, const TinyHttpParserHeader_t *header);

  // extension

  // mono body

  typedef struct _TinyHttpParserBodyContext TinyHttpParserBodyContext_t;

  typedef void (*tiny_http_parser_body_on_completed_fn)(TinyHttpParserBodyContext_t *context, const char *data, size_t size);
  typedef void (*tiny_http_parser_body_on_error_fn)(TinyHttpParserBodyContext_t *context, const char *reason);
  typedef void *(*tiny_http_parser_body_realloc_buffer_fn)(TinyHttpParserBodyContext_t *context, void *buffer, size_t new_size);
  typedef void (*tiny_http_parser_body_free_buffer_fn)(TinyHttpParserBodyContext_t *context, void *buffer, size_t buffer_size);

  struct _TinyHttpParserBodyContext
  {
    void *user_context;
    const char *error_reason;

    char *buffer;
    size_t buffer_size;

    tiny_http_parser_body_on_completed_fn on_completed;
    tiny_http_parser_body_on_error_fn on_error;
    tiny_http_parser_body_realloc_buffer_fn realloc_buffer;
    tiny_http_parser_body_free_buffer_fn free_buffer;
  };

  void tiny_http_parser_body_init(TinyHttpParserBodyContext_t *context,
                                  void *user_context,
                                  tiny_http_parser_body_on_completed_fn on_completed /* nullable */,
                                  tiny_http_parser_body_on_error_fn on_error /* nullable */,
                                  tiny_http_parser_body_realloc_buffer_fn realloc_buffer /* nullable */,
                                  tiny_http_parser_body_free_buffer_fn free_buffer /* nullable */);
  /**
   * @brief
   *
   * @param data input http body data. Or NULL to force invoke body stream end
   */
  size_t tiny_http_parser_body_input(TinyHttpParserBodyContext_t *context, const char *data /* input http body data */, size_t size, size_t *remain_not_recv_size /* nullable */);
  void tiny_http_parser_body_deinit(TinyHttpParserBodyContext_t *context);

  // multi part

  typedef struct _TinyHttpParserMultiPartContext TinyHttpParserMultiPartContext_t;

  typedef void (*tiny_http_parser_multi_part_on_part_fn)(TinyHttpParserMultiPartContext_t *context,
                                                         const TinyHttpParserPair_t *headers_list,
                                                         size_t headers_list_size,
                                                         const char *data, size_t size);
  typedef void (*tiny_http_parser_multi_part_on_completed_fn)(TinyHttpParserMultiPartContext_t *context);
  typedef void (*tiny_http_parser_multi_part_on_error_fn)(TinyHttpParserMultiPartContext_t *context, const char *reason);
  typedef void *(*tiny_http_parser_multi_part_realloc_buffer_fn)(TinyHttpParserMultiPartContext_t *context, void *buffer, size_t new_size);
  typedef void (*tiny_http_parser_multi_part_free_buffer_fn)(TinyHttpParserMultiPartContext_t *context, void *buffer, size_t buffer_size);

  struct _TinyHttpParserMultiPartContext
  {
    void *user_context;
    const char *error_reason;
    int parser_status;
    const TinyHttpParserStringView_t *boundary;

    char *buffer;
    size_t buffer_size;

    tiny_http_parser_multi_part_on_part_fn on_part;
    tiny_http_parser_multi_part_on_completed_fn on_completed;
    tiny_http_parser_multi_part_on_error_fn on_error;
    tiny_http_parser_multi_part_realloc_buffer_fn realloc_buffer;
    tiny_http_parser_multi_part_free_buffer_fn free_buffer;
  };

  void tiny_http_parser_multi_part_init(TinyHttpParserMultiPartContext_t *context,
                                        const TinyHttpParserStringView_t *boundary,
                                        void *user_context,
                                        tiny_http_parser_multi_part_on_part_fn on_part /* nullable */,
                                        tiny_http_parser_multi_part_on_completed_fn on_completed /* nullable */,
                                        tiny_http_parser_multi_part_on_error_fn on_error /* nullable */,
                                        tiny_http_parser_multi_part_realloc_buffer_fn realloc_buffer /* nullable */,
                                        tiny_http_parser_multi_part_free_buffer_fn free_buffer /* nullable */);
  size_t tiny_http_parser_multi_part_input(TinyHttpParserMultiPartContext_t *context, const char *data /* input http body data */, size_t size, size_t *remain_not_recv_size /* nullable */);
  void tiny_http_parser_multi_part_deinit(TinyHttpParserMultiPartContext_t *context);

  // chunk

  typedef struct _TinyHttpParserChunkContext TinyHttpParserChunkContext_t;

  typedef void (*tiny_http_parser_chunk_on_chunk_size_fn)(TinyHttpParserChunkContext_t *context, size_t size);
  typedef void (*tiny_http_parser_chunk_on_chunk_fn)(TinyHttpParserChunkContext_t *context, const char *data, size_t size);
  typedef void (*tiny_http_parser_chunk_on_completed_fn)(TinyHttpParserChunkContext_t *context);
  typedef void (*tiny_http_parser_chunk_on_error_fn)(TinyHttpParserChunkContext_t *context, const char *reason);
  typedef void *(*tiny_http_parser_chunk_realloc_buffer_fn)(TinyHttpParserChunkContext_t *context, void *buffer, size_t new_size);
  typedef void (*tiny_http_parser_chunk_free_buffer_fn)(TinyHttpParserChunkContext_t *context, void *buffer, size_t buffer_size);

  struct _TinyHttpParserChunkContext
  {
    void *user_context;
    const char *error_reason;
    int parser_status;
    char *buffer;
    size_t buffer_size;
    size_t expected_chunk_size;

    tiny_http_parser_chunk_on_chunk_size_fn on_chunk_size;
    tiny_http_parser_chunk_on_chunk_fn on_chunk;
    tiny_http_parser_chunk_on_completed_fn on_completed;
    tiny_http_parser_chunk_on_error_fn on_error;
    tiny_http_parser_chunk_realloc_buffer_fn realloc_buffer;
    tiny_http_parser_chunk_free_buffer_fn free_buffer;
  };

  void tiny_http_parser_chunk_init(TinyHttpParserChunkContext_t *context,
                                   void *user_context,
                                   tiny_http_parser_chunk_on_chunk_size_fn on_chunk_size /* nullable */,
                                   tiny_http_parser_chunk_on_chunk_fn on_chunk /* nullable */,
                                   tiny_http_parser_chunk_on_completed_fn on_completed /* nullable */,
                                   tiny_http_parser_chunk_on_error_fn on_error /* nullable */,
                                   tiny_http_parser_chunk_realloc_buffer_fn realloc_buffer /* nullable */,
                                   tiny_http_parser_chunk_free_buffer_fn free_buffer /* nullable */);
  size_t tiny_http_parser_chunk_input(TinyHttpParserChunkContext_t *context, const char *data /* input http "BODY" data */, size_t size, size_t *remain_not_recv_size /* nullable */);
  void tiny_http_parser_chunk_deinit(TinyHttpParserChunkContext_t *context);

#ifdef __cplusplus
}
#endif

#endif
