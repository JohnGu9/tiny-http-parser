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
   * @param realloc not nullable `void *(void *buffer, size_t size)`
   * @param free not nullable `void (void *buffer)`
   */
  void tiny_http_parser_hook_init(tiny_http_parser_realloc_fn realloc, tiny_http_parser_free_fn free);

  typedef struct _TinyHttpParserStringView TinyHttpParserStringView_t;
  typedef struct _TinyHttpParserPair TinyHttpParserPair_t;
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
    const TinyHttpParserRequestHeader_t *request_header;
    const TinyHttpParserResponseHeader_t *response_header;
  };

  typedef struct _TinyHttpParserContext TinyHttpParserContext_t;

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

  /**
   * @brief init the `TinyHttpParserContext_t`
   *
   * @param context `TinyHttpParserContext_t`
   * @param user_context nullable
   * @param on_header nullable `size_t (TinyHttpParserContext_t *context, const TinyHttpParserHeader_t *header)`
   * @param on_body_stream nullable `size_t (TinyHttpParserContext_t *context, const char *data, size_t size, size_t *remain_not_recv_size)`
   * @param on_body_completed nullable `void (TinyHttpParserContext_t *context)`
   * @param on_error nullable `void (TinyHttpParserContext_t *context, const char *reason)`
   * @param alloc_buffer nullable if tiny_http_parser_hook_init is called
   * @param free_buffer nullable if tiny_http_parser_hook_init is called
   */
  void tiny_http_parser_init(TinyHttpParserContext_t *context,
                             void *user_context,
                             tiny_http_parser_on_header_fn on_header,           // return expected body size
                             tiny_http_parser_on_body_stream_fn on_body_stream, // return consumed data size
                             tiny_http_parser_on_body_completed_fn on_body_completed,
                             tiny_http_parser_on_error_fn on_error,
                             tiny_http_parser_realloc_buffer_fn alloc_buffer,
                             tiny_http_parser_free_buffer_fn free_buffer);
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

  /**
   * @brief init the `TinyHttpParserBodyContext_t`
   *
   * @param context `TinyHttpParserBodyContext_t`
   * @param user_context nullable
   * @param on_completed nullable `void (TinyHttpParserBodyContext_t *context, const char *data, size_t size)`
   * @param on_error nullable `void (TinyHttpParserBodyContext_t *context, const char *reason)`
   * @param realloc_buffer nullable if tiny_http_parser_hook_init is called
   * @param free_buffer nullable if tiny_http_parser_hook_init is called
   */
  void tiny_http_parser_body_init(TinyHttpParserBodyContext_t *context,
                                  void *user_context,
                                  tiny_http_parser_body_on_completed_fn on_completed,
                                  tiny_http_parser_body_on_error_fn on_error,
                                  tiny_http_parser_body_realloc_buffer_fn realloc_buffer,
                                  tiny_http_parser_body_free_buffer_fn free_buffer);
  /**
   * @brief
   *
   * @param data input http body data. Or NULL to force invoke body stream end
   */
  size_t tiny_http_parser_body_input(TinyHttpParserBodyContext_t *context, const char *data /* input http body data */, size_t size, size_t *remain_not_recv_size /* nullable */);
  void tiny_http_parser_body_deinit(TinyHttpParserBodyContext_t *context);

  // multi part

#ifndef TINY_HTTP_PARSER_MULTI_PART_BOUNDARY_MAX_SIZE
#define TINY_HTTP_PARSER_MULTI_PART_BOUNDARY_MAX_SIZE 70
#endif

  typedef struct _TinyHttpParserMultiPartContext TinyHttpParserMultiPartContext_t;

  typedef void (*tiny_http_parser_multi_part_on_part_fn)(TinyHttpParserMultiPartContext_t *context,
                                                         const TinyHttpParserPair_t *headers_list, size_t headers_list_size,
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
    char boundary[TINY_HTTP_PARSER_MULTI_PART_BOUNDARY_MAX_SIZE + 10];
    size_t boundary_size;

    char *buffer;
    size_t buffer_size;

    tiny_http_parser_multi_part_on_part_fn on_part;
    tiny_http_parser_multi_part_on_completed_fn on_completed;
    tiny_http_parser_multi_part_on_error_fn on_error;
    tiny_http_parser_multi_part_realloc_buffer_fn realloc_buffer;
    tiny_http_parser_multi_part_free_buffer_fn free_buffer;
  };

  /**
   * @brief init the `TinyHttpParserMultiPartContext_t`
   *
   * @param context `TinyHttpParserMultiPartContext_t`
   * @param boundary not nullable
   * @param user_context nullable
   * @param on_part nullable `void (TinyHttpParserMultiPartContext_t *context, const TinyHttpParserPair_t *headers_list, size_t headers_list_size, const char *data, size_t size)`
   * @param on_completed nullable `void (TinyHttpParserMultiPartContext_t *context)`
   * @param on_error nullable `void (TinyHttpParserMultiPartContext_t *context, const char *reason)`
   * @param realloc_buffer nullable if tiny_http_parser_hook_init is called
   * @param free_buffer nullable if tiny_http_parser_hook_init is called
   */
  void tiny_http_parser_multi_part_init(TinyHttpParserMultiPartContext_t *context,
                                        const TinyHttpParserStringView_t *boundary,
                                        void *user_context,
                                        tiny_http_parser_multi_part_on_part_fn on_part,
                                        tiny_http_parser_multi_part_on_completed_fn on_completed,
                                        tiny_http_parser_multi_part_on_error_fn on_error,
                                        tiny_http_parser_multi_part_realloc_buffer_fn realloc_buffer,
                                        tiny_http_parser_multi_part_free_buffer_fn free_buffer);
  size_t tiny_http_parser_multi_part_input(TinyHttpParserMultiPartContext_t *context, const char *data /* input http body data */, size_t size, size_t *remain_not_recv_size /* nullable */);
  void tiny_http_parser_multi_part_deinit(TinyHttpParserMultiPartContext_t *context);

  // chunk

  typedef struct _TinyHttpParserChunkContext TinyHttpParserChunkContext_t;

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

    tiny_http_parser_chunk_on_chunk_fn on_chunk;
    tiny_http_parser_chunk_on_completed_fn on_completed;
    tiny_http_parser_chunk_on_error_fn on_error;
    tiny_http_parser_chunk_realloc_buffer_fn realloc_buffer;
    tiny_http_parser_chunk_free_buffer_fn free_buffer;
  };

  /**
   * @brief init the `TinyHttpParserChunkContext_t`
   *
   * @param context `TinyHttpParserChunkContext_t`
   * @param user_context nullable
   * @param on_chunk nullable `void (TinyHttpParserChunkContext_t *context, const char *data, size_t size)`
   * @param on_completed nullable `void (TinyHttpParserChunkContext_t *context)`
   * @param on_error nullable `void (TinyHttpParserChunkContext_t *context, const char *reason)`
   * @param realloc_buffer nullable if tiny_http_parser_hook_init is called
   * @param free_buffer nullable if tiny_http_parser_hook_init is called
   */
  void tiny_http_parser_chunk_init(TinyHttpParserChunkContext_t *context,
                                   void *user_context,
                                   tiny_http_parser_chunk_on_chunk_fn on_chunk,
                                   tiny_http_parser_chunk_on_completed_fn on_completed,
                                   tiny_http_parser_chunk_on_error_fn on_error,
                                   tiny_http_parser_chunk_realloc_buffer_fn realloc_buffer,
                                   tiny_http_parser_chunk_free_buffer_fn free_buffer);
  size_t tiny_http_parser_chunk_input(TinyHttpParserChunkContext_t *context, const char *data /* input http "BODY" data */, size_t size, size_t *remain_not_recv_size /* nullable */);
  void tiny_http_parser_chunk_deinit(TinyHttpParserChunkContext_t *context);

#ifdef __STDC_VERSION__
#if __STDC_VERSION__ >= 199901L // C99

#include <stdint.h>

  // websocket
  // reference: https://en.wikipedia.org/wiki/WebSocket
  //            https://www.rfc-editor.org/info/rfc6455/#section-5.2

  typedef struct _TinyHttpParserWebsocketContext TinyHttpParserWebsocketContext_t;

  /**
   * @brief
   *
   * @param opcode - [8]  close;
   *                 [9]  ping;
   *                 [10] pong;
   *
   */
  typedef void (*tiny_http_parser_websocket_on_control_fn)(TinyHttpParserWebsocketContext_t *context, uint8_t opcode, const char *data, size_t size);
  /**
   * @brief
   *
   * @param opcode - [1]  utf-8;
   *                 [2]  binray;
   *
   */
  typedef void (*tiny_http_parser_websocket_on_message_fn)(TinyHttpParserWebsocketContext_t *context, uint8_t opcode, const char *data, size_t size);
  typedef void (*tiny_http_parser_websocket_on_error_fn)(TinyHttpParserWebsocketContext_t *context, const char *reason);
  typedef void *(*tiny_http_parser_websocket_realloc_buffer_fn)(TinyHttpParserWebsocketContext_t *context, void *buffer, size_t new_size);
  typedef void (*tiny_http_parser_websocket_free_buffer_fn)(TinyHttpParserWebsocketContext_t *context, void *buffer, size_t buffer_size);

  struct _TinyHttpParserWebsocketContext
  {
    void *user_context;
    const char *error_reason;
    int parser_status;

    uint8_t header[2 /* Fixed */ + 8 /* Extended payload length */ + 4 /* Masking-key */];
    size_t header_size;
    const uint8_t *current_mask_key;

    char *buffer;
    size_t buffer_size;
    size_t current_payload_length;
    size_t recv_payload_length;

    tiny_http_parser_websocket_on_control_fn on_control;
    tiny_http_parser_websocket_on_message_fn on_message;
    tiny_http_parser_websocket_on_error_fn on_error;
    tiny_http_parser_websocket_realloc_buffer_fn realloc_buffer;
    tiny_http_parser_websocket_free_buffer_fn free_buffer;
  };

  /**
   * @brief init the `TinyHttpParserWebsocketContext_t`
   *
   * @param context `TinyHttpParserWebsocketContext_t`
   * @param user_context nullable
   * @param on_control nullable `void (TinyHttpParserWebsocketContext_t *context, uint8_t opcode, const char *data, size_t size)`
   * @param on_message nullable `void (TinyHttpParserWebsocketContext_t *context, uint8_t opcode, const char *data, size_t size)`
   * @param on_error nullable `void *(TinyHttpParserWebsocketContext_t *context, void *buffer, size_t new_size)`
   * @param realloc_buffer nullable if tiny_http_parser_hook_init is called
   * @param free_buffer nullable if tiny_http_parser_hook_init is called
   */
  void tiny_http_parser_websocket_init(TinyHttpParserWebsocketContext_t *context,
                                       void *user_context,
                                       tiny_http_parser_websocket_on_control_fn on_control,
                                       tiny_http_parser_websocket_on_message_fn on_message,
                                       tiny_http_parser_websocket_on_error_fn on_error,
                                       tiny_http_parser_websocket_realloc_buffer_fn realloc_buffer,
                                       tiny_http_parser_websocket_free_buffer_fn free_buffer);
  size_t tiny_http_parser_websocket_input(TinyHttpParserWebsocketContext_t *context, const char *data /* input http "BODY" data */, size_t size, size_t *remain_not_recv_size /* nullable */);
  void tiny_http_parser_websocket_deinit(TinyHttpParserWebsocketContext_t *context);

  const uint8_t *tiny_http_parser_websocket_get_mask(TinyHttpParserWebsocketContext_t *context);

#endif
#endif

#ifdef __cplusplus
}
#endif

#endif
