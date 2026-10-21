#include "tiny-http-parser.h"
#include "kmp.h"

#include <assert.h>

#define GET_LITERAL_SIZE(literal) (sizeof(literal) - 1)
#define FMT_LITERAL(literal) literal, GET_LITERAL_SIZE(literal)

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

#ifndef MAX
#define MAX(a, b) (a) > (b) ? (a) : (b)
#endif

#ifndef MIN
#define MIN(a, b) (a) < (b) ? (a) : (b)
#endif

#define _find_sub_string find_sub_string

#define _invoke_on_error(context, reason)                    \
  do                                                         \
  {                                                          \
    (context)->error_reason = (reason);                      \
    (context)->on_error((context), (context)->error_reason); \
  } while (0)

int tiny_http_parser_string_view_compare(const TinyHttpParserStringView_t *string_view, const char *data, size_t size)
{
  if (string_view->size < size)
  {
    return -1;
  }
  else if (string_view->size > size)
  {
    return 1;
  }
  return memcmp(string_view->data, data, size);
}

static tiny_http_parser_realloc_fn _tiny_http_parser_realloc = NULL;
static tiny_http_parser_free_fn _tiny_http_parser_free = NULL;

void tiny_http_parser_hook_init(tiny_http_parser_realloc_fn realloc, tiny_http_parser_free_fn free)
{
  _tiny_http_parser_realloc = realloc;
  _tiny_http_parser_free = free;
}

#define DEFAULT_REALLOC(function_name, context_type)                               \
  static void *function_name(context_type *context, void *buffer, size_t new_size) \
  {                                                                                \
    UNUSED(context);                                                               \
    if (_tiny_http_parser_realloc != NULL)                                         \
    {                                                                              \
      return _tiny_http_parser_realloc(buffer, new_size);                          \
    }                                                                              \
    return NULL;                                                                   \
  }

#define DEFAULT_FREE(function_name, context_type)                             \
  static void function_name(context_type *context, void *buffer, size_t size) \
  {                                                                           \
    UNUSED(context);                                                          \
    UNUSED(size);                                                             \
    if (_tiny_http_parser_free != NULL)                                       \
    {                                                                         \
      _tiny_http_parser_free(buffer);                                         \
    }                                                                         \
  }

static size_t _char_to_number(char c)
{
  switch (c)
  {
  case 'A':
  case 'a':
    return 10;
  case 'B':
  case 'b':
    return 11;
  case 'C':
  case 'c':
    return 12;
  case 'D':
  case 'd':
    return 13;
  case 'E':
  case 'e':
    return 14;
  case 'F':
  case 'f':
    return 15;
  case '1':
    return 1;
  case '2':
    return 2;
  case '3':
    return 3;
  case '4':
    return 4;
  case '5':
    return 5;
  case '6':
    return 6;
  case '7':
    return 7;
  case '8':
    return 8;
  case '9':
    return 9;
  case '0':
  default:
  {
    return 0;
  }
  }
}

#define HEX_BASE 16
#define OCTAL_BASE 10

static size_t _pow_unsigned(size_t base, size_t exp)
{
  size_t result = 1;
  while (exp)
  {
    if (exp % 2)
      result *= base;
    exp /= 2;
    base *= base;
  }
  return result;
}

/**
 * @brief simple version of strtol()
 *        strtol() available in <stdlib.h> after C99
 */
static size_t _string_to_unsigned(const char *str, const char *str_end, const int base)
{
  size_t result = 0;
  for (; str < str_end; str++)
  {
    size_t position = str_end - str - 1;
    size_t n = _char_to_number(*str);
    result += n * _pow_unsigned(base, position);
  }
  return result;
}

#define _FIND_SUB_STRING_FROM_TWO_PART_BUFFER_SIZE(literal) 2 * (sizeof(literal) - 2)

/**
 * @brief
 *
 * @param part0 the sub string that can't found the pattern
 * @param part1
 * @param pattern
 * @param position
 * @return `const TinyHttpParserStringView_t*` `part0` or `part1`, if not found, return NULL
 */
static const TinyHttpParserStringView_t *_find_sub_string_from_two_part(const TinyHttpParserStringView_t *part0,
                                                                        const TinyHttpParserStringView_t *part1,
                                                                        const TinyHttpParserStringView_t *pattern,
                                                                        char *buffer,
                                                                        size_t buffer_size,
                                                                        size_t *position)
{
  const size_t expected_buffer_size = 2 * (pattern->size - 1);
  assert(expected_buffer_size <= buffer_size); // !important

  const size_t part0_copy_size = MIN(part0->size, (pattern->size - 1));
  const size_t part0_copy_start_position = part0->size - part0_copy_size;
  memcpy(buffer, part0->data + part0_copy_start_position, part0_copy_size);

  const size_t part1_copy_size = MIN(part1->size, (pattern->size - 1));
  memcpy(buffer + part0_copy_size, part1->data, part1_copy_size);

  const size_t buffer_used_size = part0_copy_size + part1_copy_size;
  const char *result = _find_sub_string(buffer, buffer_used_size, pattern->data, pattern->size);
  if (result != NULL)
  {
    size_t result_position = (result - buffer);
    *position = part0_copy_start_position + result_position;
    return part0;
  }

  // result == NULL (pattern not found in the buffer)
  result = _find_sub_string(part1->data, part1->size, pattern->data, pattern->size);
  if (result == NULL)
  {
    return NULL;
  }
  *position = (result - part1->data);
  return part1;
}

enum
{
  _parser_status_header,
  _parser_status_header_half_way,
  _parser_status_body,
};

static size_t _ignore_tiny_http_parser_on_body_stream_fn(TinyHttpParserContext_t *context, const char *data, size_t size, size_t *remain_not_recv_size)
{
  UNUSED(context);
  UNUSED(data);
  UNUSED(remain_not_recv_size);
  return size;
}

static void _ignore_tiny_http_parser_on_body_completed_fn(TinyHttpParserContext_t *context)
{
  UNUSED(context);
}

static void _ignore_tiny_http_parser_on_error_fn(TinyHttpParserContext_t *context, const char *reason)
{
  UNUSED(context);
  UNUSED(reason);
}

DEFAULT_REALLOC(_default_tiny_http_parser_realloc_buffer, TinyHttpParserContext_t);
DEFAULT_FREE(_default_tiny_http_parser_free_buffer, TinyHttpParserContext_t);

void tiny_http_parser_init(TinyHttpParserContext_t *context,
                           void *user_context,
                           tiny_http_parser_on_header_fn on_header,
                           tiny_http_parser_on_body_stream_fn on_body_stream,
                           tiny_http_parser_on_body_completed_fn on_body_completed,
                           tiny_http_parser_on_error_fn on_error,
                           tiny_http_parser_realloc_buffer_fn realloc_buffer,
                           tiny_http_parser_free_buffer_fn free_buffer)
{
  context->user_context = user_context;
  context->on_header = on_header != NULL ? on_header : tiny_http_parser_default_on_header;
  context->on_body_stream = on_body_stream != NULL ? on_body_stream : _ignore_tiny_http_parser_on_body_stream_fn;
  context->on_body_completed = on_body_completed != NULL ? on_body_completed : _ignore_tiny_http_parser_on_body_completed_fn;
  context->on_error = on_error != NULL ? on_error : _ignore_tiny_http_parser_on_error_fn;
  context->realloc_buffer = realloc_buffer != NULL ? realloc_buffer : _default_tiny_http_parser_realloc_buffer;
  context->free_buffer = free_buffer != NULL ? free_buffer : _default_tiny_http_parser_free_buffer;

  context->error_reason = NULL;
  context->parser_status = _parser_status_header;
  context->header_buffer = NULL;
  context->header_buffer_size = 0;
  context->expected_body_size = 0;
}

static const TinyHttpParserStringView_t _header_end_symbol = StaticTinyHttpParserStringView("\r\n\r\n");
static const TinyHttpParserStringView_t _line_end_symbol = StaticTinyHttpParserStringView("\r\n");

static const char *_go_to_next_not_char(char c, const char *data, const char *data_end)
{
  for (; data < data_end; data++)
  {
    if (*data != c)
    {
      break;
    }
  }
  return data;
}

static const char *_go_to_next_char(char c, const char *data, const char *data_end)
{
  for (; data < data_end; data++)
  {
    if (*data == c)
    {
      break;
    }
  }
  return data;
}

static void _get_chunk(const char *data, const char *data_end, TinyHttpParserStringView_t *string_view)
{
  const char *start = _go_to_next_not_char(' ', data, data_end);
  const char *end = _go_to_next_char(' ', start, data_end);
  *string_view = (TinyHttpParserStringView_t){
      .data = start,
      .size = (TINY_HTTP_PARSER_SIZE_T)(end - start),
  };
}

static size_t _get_headers_list_size(const char *data, const char *data_end)
{
  size_t headers_list_size = 0;
  for (const char *line_start = data; line_start < data_end;)
  {
    const char *line_end = _find_sub_string(line_start, data_end - line_start, _line_end_symbol.data, _line_end_symbol.size);
    if (line_start == line_end)
    {
      // empty line
      break;
    }
    headers_list_size++;
    line_start = line_end + _line_end_symbol.size;
  }
  return headers_list_size;
}

static size_t _parser_optional_headers(TinyHttpParserPair_t *headers_list,
                                       size_t headers_list_max_size,
                                       const char *data,
                                       const char *data_end)
{
  size_t headers_list_size = 0;
  for (const char *line_start = data; line_start < data_end;)
  {
    const char *line_end = _find_sub_string(line_start, data_end - line_start, _line_end_symbol.data, _line_end_symbol.size);
    if (line_start == line_end)
    {
      break;
    }
    if (headers_list_size < headers_list_max_size)
    {
      const char *key_end = _go_to_next_char(':', line_start, line_end);
      const char *value_start;
      if (key_end >= line_end - 1)
      {
        // @TODO: error handle
        value_start = line_end;
      }
      else
      {
        const char *value_search_start = key_end + 1;
        value_start = _go_to_next_not_char(' ', value_search_start, line_end);
      }
      headers_list[headers_list_size++] = (TinyHttpParserPair_t){
          .raw = {
              .data = line_start,
              .size = (TINY_HTTP_PARSER_SIZE_T)(line_end - line_start),
          },
          .key = {
              .data = line_start,
              .size = (TINY_HTTP_PARSER_SIZE_T)(key_end - line_start),
          },
          .value = {
              .data = value_start,
              .size = (TINY_HTTP_PARSER_SIZE_T)(line_end - value_start),
          },
      };
    }
    line_start = line_end + _line_end_symbol.size;
  }
  return headers_list_size;
}

static size_t _parser_response_headers(TinyHttpParserContext_t *context,
                                       TinyHttpParserHeader_t *header,
                                       const char *data,
                                       size_t size)
{
  size_t expected_body_size = 0;
  TinyHttpParserResponseHeader_t response = {0};
  header->response_header = &response;

  const char *line_end = _find_sub_string(data, size, _line_end_symbol.data, _line_end_symbol.size);

  _get_chunk(data, line_end, &response.protocol);
  _get_chunk(response.protocol.data + response.protocol.size, line_end, &response.status_code);
  _get_chunk(response.status_code.data + response.status_code.size, line_end, &response.reason_phrase);

  const char *line_start = line_end + _line_end_symbol.size;

  const char *data_end = data + size;
  response.headers_list_size = _get_headers_list_size(line_start, data_end);
  TinyHttpParserPair_t *headers_list = context->realloc_buffer(context, NULL, sizeof(TinyHttpParserPair_t) * response.headers_list_size);
  if (response.headers_list_size == 0 || headers_list != NULL)
  {
    _parser_optional_headers(headers_list, response.headers_list_size, line_start, data_end);
    response.headers_list = headers_list;
    expected_body_size = context->on_header(context, header);
    context->free_buffer(context, headers_list, sizeof(TinyHttpParserPair_t) * response.headers_list_size);
  }
  else
  {
    _invoke_on_error(context, "http alloc headers list buffer failed");
    expected_body_size = context->on_header(context, header);
  }
  return expected_body_size;
}

static size_t _parser_request_headers(TinyHttpParserContext_t *context,
                                      TinyHttpParserHeader_t *header,
                                      const char *data,
                                      size_t size)
{
  size_t expected_body_size = 0;
  TinyHttpParserRequestHeader_t request = {0};
  header->request_header = &request;

  const char *line_end = _find_sub_string(data, size, _line_end_symbol.data, _line_end_symbol.size);

  _get_chunk(data, line_end, &request.method);
  _get_chunk(request.method.data + request.method.size, line_end, &request.path);
  _get_chunk(request.path.data + request.path.size, line_end, &request.protocol);

  const char *line_start = line_end + _line_end_symbol.size;

  const char *data_end = data + size;
  request.headers_list_size = _get_headers_list_size(line_start, data_end);
  TinyHttpParserPair_t *headers_list = context->realloc_buffer(context, NULL, sizeof(TinyHttpParserPair_t) * request.headers_list_size);
  if (request.headers_list_size == 0 || headers_list != NULL)
  {
    _parser_optional_headers(headers_list, request.headers_list_size, line_start, data_end);
    request.headers_list = headers_list;
    expected_body_size = context->on_header(context, header);
    context->free_buffer(context, headers_list, sizeof(TinyHttpParserPair_t) * request.headers_list_size);
  }
  else
  {
    _invoke_on_error(context, "http alloc headers list buffer failed");
    expected_body_size = context->on_header(context, header);
  }
  return expected_body_size;
}

static size_t _parser_headers(TinyHttpParserContext_t *context, const char *data, size_t size)
{
  TinyHttpParserHeader_t header = {
      .raw = {
          .data = data,
          .size = (TINY_HTTP_PARSER_SIZE_T)size,
      },
      .request_header = NULL,
      .response_header = NULL,
  };
  if (size < 4) // unlikely, \r\n\r\n already 4 bytes
  {
    return 0;
  }
  if (memcmp(data, "HTTP", 4) == 0)
  {
    return _parser_response_headers(context, &header, data, size);
  }
  else
  {
    return _parser_request_headers(context, &header, data, size);
  }
}

void tiny_http_parser_input(TinyHttpParserContext_t *context, const char *data, size_t full_size)
{
  const char *const data_end = data + full_size;
  for (; data != NULL && data < data_end;)
  {
    size_t size = data_end - data;
    switch (context->parser_status)
    {
    case _parser_status_body:
    {
      size_t remain_not_recv_size;
      size_t body_size;
      if (context->expected_body_size < size)
      {
        remain_not_recv_size = 0;
        body_size = context->expected_body_size;
      }
      else
      {
        remain_not_recv_size = context->expected_body_size - size;
        body_size = size;
      }
      size_t override_body_size = context->on_body_stream(context, data, body_size, &remain_not_recv_size);
      context->expected_body_size = remain_not_recv_size;
      if (override_body_size < body_size)
      {
        body_size = override_body_size;
      }
      if (context->expected_body_size == 0)
      {
        context->on_body_completed(context);
        context->parser_status = _parser_status_header;
      }
      data = data + body_size;
      break;
    }
    case _parser_status_header:
    {
      const char *header_end = _find_sub_string(data, size, _header_end_symbol.data, _header_end_symbol.size);
      if (header_end == NULL)
      {
        void *new_buffer = context->realloc_buffer(context, context->header_buffer, size);
        if (new_buffer == NULL)
        {
          _invoke_on_error(context, "http alloc header buffer failed");
          return; // critical error, can't continue
        }
        context->header_buffer = (char *)new_buffer;
        memcpy(context->header_buffer, data, size);
        context->header_buffer_size = size;
        context->parser_status = _parser_status_header_half_way;
        data = data_end;
        break;
      }
      header_end = header_end + _header_end_symbol.size;
      size_t header_size = header_end - data;
      context->expected_body_size = _parser_headers(context, data, header_size);
      if (context->expected_body_size == 0)
      {
        context->parser_status = _parser_status_header;
      }
      else
      {
        context->parser_status = _parser_status_body;
      }
      data = header_end;
      break;
    }
    case _parser_status_header_half_way:
    {
      const TinyHttpParserStringView_t part0 = {
          .data = context->header_buffer,
          .size = context->header_buffer_size,
      };
      const TinyHttpParserStringView_t part1 = {
          .data = data,
          .size = size,
      };
      const TinyHttpParserStringView_t header_end_symbol = StaticTinyHttpParserStringView("\r\n\r\n");
      char search_buffer[8];
      size_t position = 0;
      const TinyHttpParserStringView_t *result =
          _find_sub_string_from_two_part(&part0, &part1, &header_end_symbol, search_buffer, sizeof(search_buffer), &position);
      if (result == NULL)
      {
        size_t new_size = context->header_buffer_size + size;
        void *new_buffer = context->realloc_buffer(context, context->header_buffer, new_size);
        if (new_buffer == NULL)
        {
          _invoke_on_error(context, "http alloc header buffer failed");
          return; // critical error, can't continue
        }
        context->header_buffer = (char *)new_buffer;
        memcpy(context->header_buffer + context->header_buffer_size, data, size);
        context->header_buffer_size = new_size;

        data = data + size;
        break;
      }

      size_t copy_size;
      if (result == &part0)
      {
        copy_size = position + header_end_symbol.size - part0.size;
      }
      else if (result == &part1)
      {
        copy_size = position + header_end_symbol.size;
      }
      else
      {
        // never branch
        _invoke_on_error(context, "Internal error");
        return; // critical error, can't continue
      }

      size_t new_size = context->header_buffer_size + copy_size;

      void *new_buffer = context->realloc_buffer(context, context->header_buffer, new_size);
      if (new_buffer == NULL)
      {
        _invoke_on_error(context, "http alloc header buffer failed");
        return; // critical error, can't continue
      }
      context->header_buffer = (char *)new_buffer;
      memcpy((context->header_buffer + context->header_buffer_size), data, copy_size);
      context->header_buffer_size = new_size;

      context->expected_body_size = _parser_headers(context, context->header_buffer, context->header_buffer_size);
      if (context->expected_body_size == 0)
      {
        context->parser_status = _parser_status_header;
      }
      else
      {
        context->parser_status = _parser_status_body;
      }

      context->free_buffer(context, context->header_buffer, context->header_buffer_size);
      context->header_buffer = NULL;
      context->header_buffer_size = 0;

      data = data + copy_size;
      break;
    }

    default:
      break;
    }
  }
}

void tiny_http_parser_deinit(TinyHttpParserContext_t *context)
{
  if (context->header_buffer != NULL)
  {
    context->free_buffer(context, context->header_buffer, context->header_buffer_size);
    context->header_buffer = NULL;
  }
}

#define _is_string_view_equal_to(string_view, literal) (tiny_http_parser_string_view_compare(string_view, FMT_LITERAL(literal)) == 0)

static int _is_no_content_status_code(const TinyHttpParserStringView_t *status_code)
{
  if (_is_string_view_equal_to(status_code, "204")) // 204 No Content
  {
    return 1;
  }
  if (_is_string_view_equal_to(status_code, "304")) // 304 Not Modified
  {
    return 1;
  }

  if (status_code->size == 3 && status_code->data[0] == '1') // 1xx
  {
    return 1;
  }

  return 0;
}

size_t tiny_http_parser_default_on_header(TinyHttpParserContext_t *context, const TinyHttpParserHeader_t *header)
{
  UNUSED(context);

  const TinyHttpParserPair_t *headers_list = NULL;
  const TinyHttpParserStringView_t *status_code;
  size_t headers_list_size = 0;
  if (header->response_header != NULL)
  {
    status_code = &header->response_header->status_code;
    headers_list = header->response_header->headers_list;
    headers_list_size = header->response_header->headers_list_size;
  }
  else if (header->request_header != NULL)
  {
    headers_list = header->request_header->headers_list;
    headers_list_size = header->request_header->headers_list_size;
  }
  if (headers_list == NULL)
  {
    return 0;
  }

  size_t chunked_length = 0;
  size_t unknown_body_size = 0;
  size_t content_length = 0;

  for (size_t i = 0; i < headers_list_size; i++)
  {
    const TinyHttpParserPair_t *el = &headers_list[i];

    if (_is_string_view_equal_to(&el->key, "Transfer-Encoding"))
    {
      if (_is_string_view_equal_to(&el->value, "chunked"))
      {
        memset(&chunked_length, 0xFF, sizeof(chunked_length)); // max value of size_t
      }
    }
    else if (_is_string_view_equal_to(&el->key, "Content-Length"))
    {
      content_length = _string_to_unsigned(el->value.data, el->value.data + el->value.size, OCTAL_BASE);
    }
    else if (_find_sub_string(el->key.data, el->key.size, FMT_LITERAL("Content-")) != NULL)
    {
      memset(&unknown_body_size, 0xFF, sizeof(unknown_body_size)); // max value of size_t
    }
  }
  if (chunked_length != 0)
  {
    return chunked_length;
  }
  if (content_length != 0)
  {
    return content_length;
  }
  if (status_code != NULL &&
      _is_no_content_status_code(status_code) == 1)
  {
    return 0;
  }
  return unknown_body_size;
}

// extension

static void _ignore_tiny_http_parser_body_on_completed(TinyHttpParserBodyContext_t *context, const char *data, size_t size)
{
  UNUSED(context);
  UNUSED(data);
  UNUSED(size);
}

static void _ignore_tiny_http_parser_body_on_error(TinyHttpParserBodyContext_t *context, const char *reason)
{
  UNUSED(context);
  UNUSED(reason);
}

DEFAULT_REALLOC(_default_tiny_http_parser_body_realloc_buffer, TinyHttpParserBodyContext_t);
DEFAULT_FREE(_default_tiny_http_parser_body_free_buffer, TinyHttpParserBodyContext_t);

void tiny_http_parser_body_init(TinyHttpParserBodyContext_t *context,
                                void *user_context,
                                tiny_http_parser_body_on_completed_fn on_completed,
                                tiny_http_parser_body_on_error_fn on_error,
                                tiny_http_parser_body_realloc_buffer_fn realloc_buffer,
                                tiny_http_parser_body_free_buffer_fn free_buffer)
{
  context->user_context = user_context;
  context->on_completed = on_completed != NULL ? on_completed : _ignore_tiny_http_parser_body_on_completed;
  context->on_error = on_error != NULL ? on_error : _ignore_tiny_http_parser_body_on_error;
  context->realloc_buffer = realloc_buffer != NULL ? realloc_buffer : _default_tiny_http_parser_body_realloc_buffer;
  context->free_buffer = free_buffer != NULL ? free_buffer : _default_tiny_http_parser_body_free_buffer;

  context->error_reason = NULL;
  context->buffer = NULL;
  context->buffer_size = 0;
}

size_t tiny_http_parser_body_input(TinyHttpParserBodyContext_t *context, const char *data /* input http body data */, size_t size, size_t *remain_not_recv_size /* nullable */)
{
  if (data == NULL) // force to complete the body message
  {
    context->on_completed(context, context->buffer, context->buffer_size);
    context->free_buffer(context, context->buffer, context->buffer_size);
    context->buffer = NULL;
    context->buffer_size = 0;
    if (remain_not_recv_size != NULL)
    {
      *remain_not_recv_size = 0;
    }
    return 0;
  }
  if (context->buffer == NULL &&
      remain_not_recv_size != NULL &&
      *remain_not_recv_size == 0)
  {
    context->on_completed(context, data, size);
    return size;
  }

  const size_t new_size = context->buffer_size + size;
  void *new_buffer = context->realloc_buffer(context, context->buffer, new_size);
  if (new_size != 0 && new_buffer == NULL)
  {
    _invoke_on_error(context, "http body parser alloc buffer failed");
    return 0;
  }

  context->buffer = (char *)new_buffer;
  memcpy(context->buffer + context->buffer_size, data, size);
  context->buffer_size = new_size;

  if (remain_not_recv_size != NULL &&
      *remain_not_recv_size == 0)
  {
    context->on_completed(context, context->buffer, context->buffer_size);
    context->free_buffer(context, context->buffer, context->buffer_size);
    context->buffer = NULL;
    context->buffer_size = 0;
  }

  return size;
}

void tiny_http_parser_body_deinit(TinyHttpParserBodyContext_t *context)
{
  if (context->buffer != NULL)
  {
    context->free_buffer(context, context->buffer, context->buffer_size);
  }
}

static void _ignore_tiny_http_parser_multi_part_on_part(TinyHttpParserMultiPartContext_t *context,
                                                        const TinyHttpParserPair_t *headers_list,
                                                        size_t headers_list_size,
                                                        const char *data, size_t size)
{
  UNUSED(context);
  UNUSED(headers_list);
  UNUSED(headers_list_size);
  UNUSED(data);
  UNUSED(size);
}

static void _ignore_tiny_http_parser_multi_part_on_completed(TinyHttpParserMultiPartContext_t *context)
{
  UNUSED(context);
}

static void _ignore_tiny_http_parser_multi_part_on_error(TinyHttpParserMultiPartContext_t *context, const char *reason)
{
  UNUSED(context);
  UNUSED(reason);
}

enum
{
  _parser_multi_part,
  _parser_multi_part_half_way,
  _parser_multi_part_completed,
};

DEFAULT_REALLOC(_default_tiny_http_parser_multi_part_realloc_buffer, TinyHttpParserMultiPartContext_t);
DEFAULT_FREE(_default_tiny_http_parser_multi_part_free_buffer, TinyHttpParserMultiPartContext_t);

void tiny_http_parser_multi_part_init(TinyHttpParserMultiPartContext_t *context,
                                      const TinyHttpParserStringView_t *boundary,
                                      void *user_context,
                                      tiny_http_parser_multi_part_on_part_fn on_part,
                                      tiny_http_parser_multi_part_on_completed_fn on_completed,
                                      tiny_http_parser_multi_part_on_error_fn on_error,
                                      tiny_http_parser_multi_part_realloc_buffer_fn realloc_buffer,
                                      tiny_http_parser_multi_part_free_buffer_fn free_buffer)
{
  assert(boundary != NULL);
  assert(boundary->size < TINY_HTTP_PARSER_MULTI_PART_BOUNDARY_MAX_SIZE);

  context->user_context = user_context;
  context->boundary_size = MIN(boundary->size, TINY_HTTP_PARSER_MULTI_PART_BOUNDARY_MAX_SIZE);

  memcpy(context->boundary, FMT_LITERAL("\r\n--"));
  memcpy(context->boundary + GET_LITERAL_SIZE("\r\n--"), boundary->data, context->boundary_size);
  // \r\n--${boundary}

  context->on_part = on_part != NULL ? on_part : _ignore_tiny_http_parser_multi_part_on_part;
  context->on_completed = on_completed != NULL ? on_completed : _ignore_tiny_http_parser_multi_part_on_completed;
  context->on_error = on_error != NULL ? on_error : _ignore_tiny_http_parser_multi_part_on_error;
  context->realloc_buffer = realloc_buffer != NULL ? realloc_buffer : _default_tiny_http_parser_multi_part_realloc_buffer;
  context->free_buffer = free_buffer != NULL ? free_buffer : _default_tiny_http_parser_multi_part_free_buffer;

  context->error_reason = NULL;
  context->parser_status = _parser_multi_part;
  context->buffer = NULL;
  context->buffer_size = 0;
}

static void _parser_single_part(TinyHttpParserMultiPartContext_t *context, const char *data, size_t size)
{
  size_t headers_list_size = 0;
  TinyHttpParserPair_t *headers_list = NULL;

  const char *part_header_start = data; // skip --${boundary}\r\n
  const char *part_header_end = _find_sub_string(data, size, _header_end_symbol.data, _header_end_symbol.size);

  const char *body_start;
  if (part_header_end != NULL) // has headers
  {
    part_header_end = part_header_end + _header_end_symbol.size;
    headers_list_size = _get_headers_list_size(part_header_start, part_header_end);
    headers_list = context->realloc_buffer(context, headers_list, sizeof(TinyHttpParserPair_t) * headers_list_size);
    if (headers_list_size == 0 || headers_list != NULL)
    {
      _parser_optional_headers(headers_list, headers_list_size, part_header_start, part_header_end);
    }
    else
    {
      _invoke_on_error(context, "http multi part parser alloc headers list buffer failed");
    }
    body_start = part_header_end;
  }
  else
  {
    body_start = part_header_start;
  }
  context->on_part(context, headers_list, headers_list_size, body_start, size - (body_start - data));

  if (headers_list != NULL)
  {
    context->free_buffer(context, headers_list, sizeof(TinyHttpParserPair_t) * headers_list_size);
  }
}

static const char *_go_to_next_part(TinyHttpParserMultiPartContext_t *context, const char *const data, const char *const data_end)
{
  const size_t size = data_end - data;
  const size_t part_start_symbol_size = GET_LITERAL_SIZE("--") + context->boundary_size + GET_LITERAL_SIZE("\r\n");
  if (size < part_start_symbol_size)
  {
    // not completed data
    return data;
  }

  // position: boundary_start    boundary_end
  //                   |            |
  // source:       \r\n--${boundary}
  const char *const boundary_start = context->boundary + GET_LITERAL_SIZE("\r\n");
  char *const boundary_end = context->boundary + GET_LITERAL_SIZE("\r\n--") + context->boundary_size;
  memcpy(boundary_end, FMT_LITERAL("--"));
  // now buffer: \r\n--${boundary}--

  const size_t part_completed_symbol_size = GET_LITERAL_SIZE("--") + context->boundary_size + GET_LITERAL_SIZE("--\r\n");
  if (memcmp(data, boundary_start, part_start_symbol_size) == 0)
  {
    if (size < part_completed_symbol_size)
    {
      // not completed data
      return data;
    }
    if (memcmp(data + part_start_symbol_size, FMT_LITERAL("\r\n")) != 0)
    {
      // failed to match
      // --${boundary}--\r\n
      _invoke_on_error(context, "http multi part parser failed");
      return NULL;
    }
    // end of part, match
    // --${boundary}--\r\n
    context->parser_status = _parser_multi_part_completed;
    return data + part_completed_symbol_size;
  }

  memcpy(boundary_end, FMT_LITERAL("\r\n"));
  // now buffer: \r\n--${boundary}\r\n

  if (memcmp(data, boundary_start, part_start_symbol_size) != 0)
  {
    // failed to match
    // --${boundary}\r\n
    _invoke_on_error(context, "http multi part parser failed");
    return NULL;
  }

  const char *part_start = data + part_start_symbol_size;
  const size_t search_size = data_end - part_start;
  // search \r\n--${boundary}\r\n
  const char *next = _find_sub_string(part_start, search_size, context->boundary, GET_LITERAL_SIZE("\r\n--") + context->boundary_size + GET_LITERAL_SIZE("\r\n"));
  if (next == NULL)
  {
    memcpy(boundary_end, FMT_LITERAL("--\r\n"));
    // search \r\n--${boundary}--\r\n
    next = _find_sub_string(part_start, search_size, context->boundary, GET_LITERAL_SIZE("\r\n--") + context->boundary_size + GET_LITERAL_SIZE("--\r\n"));
    if (next == NULL)
    {
      // not completed data
      return data;
    }
  }

  // next \r\n--${boundary} found
  size_t part_size = next - part_start;
  _parser_single_part(context, part_start, part_size);
  return next + GET_LITERAL_SIZE("\r\n");
}

size_t tiny_http_parser_multi_part_input(TinyHttpParserMultiPartContext_t *context,
                                         const char *data,
                                         size_t full_size,
                                         size_t *remain_not_recv_size)
{

  assert(context->parser_status != _parser_multi_part_completed);

  const char *const data_start = data;
  const char *const data_end = data + full_size;
  for (; data != NULL && data < data_end;)
  {
    const size_t size = data_end - data;
    switch (context->parser_status)
    {
    case _parser_multi_part:
    {
      const char *const next_part_start = _go_to_next_part(context, data, data_end);
      if (next_part_start == data) // not completed message
      {
        void *new_buffer = context->realloc_buffer(context, context->buffer, size);
        if (new_buffer == NULL)
        {
          _invoke_on_error(context, "http multi part parser alloc buffer failed");
          data = NULL;
          break;
        }
        context->buffer = (char *)new_buffer;
        memcpy(context->buffer, data, size);
        context->buffer_size = size;
        context->parser_status = _parser_multi_part_half_way;

        data = data_end;
      }
      else
      {
        data = next_part_start; // NULL or true next_part_start
      }
      break;
    }
    case _parser_multi_part_half_way:
    {
      // @TODO: optimize memory usage
      const size_t old_size = context->buffer_size;
      const size_t new_size = context->buffer_size + size;
      void *new_buffer = context->realloc_buffer(context, context->buffer, new_size);
      if (new_buffer == NULL)
      {
        _invoke_on_error(context, "http multi part parser alloc buffer failed");
        data = NULL;
        break;
      }
      context->buffer = (char *)new_buffer;
      memcpy(context->buffer + old_size, data, size);
      context->buffer_size = new_size;
      const char *next_data = _go_to_next_part(context, context->buffer, context->buffer + new_size);
      if (next_data == context->buffer) // not completed message
      {
        data = data_end;
      }
      else if (next_data != NULL)
      {
        const size_t consume_size = next_data - context->buffer;
        const size_t consume_data_size = consume_size - old_size;

        context->free_buffer(context, context->buffer, context->buffer_size);
        context->buffer = NULL;
        context->buffer_size = 0;
        context->parser_status = _parser_multi_part;

        data = data + consume_data_size;
      }
      else // next_data == NULL means error
      {
        data = NULL;
      }
      break;
    }
    default:
      break;
    }

    if (context->parser_status == _parser_multi_part_completed)
    {
      context->on_completed(context);
      if (remain_not_recv_size != NULL)
      {
        *remain_not_recv_size = 0;
      }
      return data - data_start;
    }
  }
  if (remain_not_recv_size != NULL)
  {
    memset(remain_not_recv_size, 0xFF, sizeof(*remain_not_recv_size));
  }
  return full_size;
}

void tiny_http_parser_multi_part_deinit(TinyHttpParserMultiPartContext_t *context)
{
  if (context->buffer != NULL)
  {
    context->free_buffer(context, context->buffer, context->buffer_size);
    context->buffer = NULL;
  }
}

static void _ignore_tiny_http_parser_chunk_on_chunk(TinyHttpParserChunkContext_t *context, const char *data, size_t size)
{
  UNUSED(context);
  UNUSED(data);
  UNUSED(size);
}

static void _ignore_tiny_http_parser_chunk_on_completed(TinyHttpParserChunkContext_t *context)
{
  UNUSED(context);
}

static void _ignore_tiny_http_parser_chunk_on_error(TinyHttpParserChunkContext_t *context, const char *reason)
{
  UNUSED(context);
  UNUSED(reason);
}

enum
{
  _parser_status_chunk_header,
  _parser_status_chunk_header_half_way,
  _parser_status_chunk_body,
  _parser_status_chunk_body_half_way,
  _parser_status_chunk_completed,
};

DEFAULT_REALLOC(_default_tiny_http_parser_chunk_realloc_buffer, TinyHttpParserChunkContext_t);
DEFAULT_FREE(_default_tiny_http_parser_chunk_free_buffer, TinyHttpParserChunkContext_t);

void tiny_http_parser_chunk_init(TinyHttpParserChunkContext_t *context,
                                 void *user_context,
                                 tiny_http_parser_chunk_on_chunk_fn on_chunk,
                                 tiny_http_parser_chunk_on_completed_fn on_completed,
                                 tiny_http_parser_chunk_on_error_fn on_error,
                                 tiny_http_parser_chunk_realloc_buffer_fn realloc_buffer,
                                 tiny_http_parser_chunk_free_buffer_fn free_buffer)
{
  context->user_context = user_context;
  context->on_chunk = on_chunk != NULL ? on_chunk : _ignore_tiny_http_parser_chunk_on_chunk;
  context->on_completed = on_completed != NULL ? on_completed : _ignore_tiny_http_parser_chunk_on_completed;
  context->on_error = on_error != NULL ? on_error : _ignore_tiny_http_parser_chunk_on_error;
  context->realloc_buffer = realloc_buffer != NULL ? realloc_buffer : _default_tiny_http_parser_chunk_realloc_buffer;
  context->free_buffer = free_buffer != NULL ? free_buffer : _default_tiny_http_parser_chunk_free_buffer;

  context->error_reason = NULL;
  context->parser_status = _parser_status_chunk_header;
  context->buffer = NULL;
  context->buffer_size = 0;
  context->expected_chunk_size = 0;
}

static const char *_skip_to_chunk_end(const char *data, size_t size, size_t expected_size)
{
  if (size < expected_size)
  {
    return NULL;
  }
  size_t remain_size = size - expected_size;
  const char *line_end = _find_sub_string(data + expected_size, remain_size, _line_end_symbol.data, _line_end_symbol.size);
  if (line_end == NULL)
  {
    return NULL;
  }
  return line_end + _line_end_symbol.size;
}

size_t tiny_http_parser_chunk_input(TinyHttpParserChunkContext_t *context, const char *data, size_t full_size, size_t *remain_not_recv_size /* nullable */)
{

  assert(context->parser_status != _parser_status_chunk_completed);

  const char *const data_start = data;
  const char *const data_end = data + full_size;
  for (; data != NULL && data < data_end;)
  {
    size_t size = data_end - data;
    switch (context->parser_status)
    {
    case _parser_status_chunk_header:
    {
      const char *line_end = _find_sub_string(data, size, _line_end_symbol.data, _line_end_symbol.size);
      if (line_end == NULL)
      {
        void *new_buffer = context->realloc_buffer(context, context->buffer, size);
        if (new_buffer == NULL)
        {
          _invoke_on_error(context, "http multi part parser alloc buffer failed");
          data = NULL;
          break;
        }
        context->buffer = (char *)new_buffer;
        memcpy(context->buffer, data, size);
        context->buffer_size = size;
        context->parser_status = _parser_status_chunk_header_half_way;
        data = data_end;
        break;
      }
      context->expected_chunk_size = _string_to_unsigned(data, line_end, HEX_BASE);
      context->parser_status = _parser_status_chunk_body;
      data = line_end + _line_end_symbol.size;
      break;
    }
    case _parser_status_chunk_header_half_way:
    {
      const TinyHttpParserStringView_t part0 = {
          .data = context->buffer,
          .size = context->buffer_size,
      };
      const TinyHttpParserStringView_t part1 = {
          .data = data,
          .size = size,
      };
      char search_buffer[4];
      size_t position = 0;
      const TinyHttpParserStringView_t *result = _find_sub_string_from_two_part(&part0, &part1, &_line_end_symbol, search_buffer, sizeof(search_buffer), &position);
      if (result == NULL)
      {
        const size_t new_size = context->buffer_size + size;
        void *new_buffer = context->realloc_buffer(context, context->buffer, new_size);
        if (new_buffer == NULL)
        {
          _invoke_on_error(context, "http multi part parser alloc buffer failed");
          data = NULL;
          break;
        }
        context->buffer = (char *)new_buffer;
        memcpy(context->buffer + context->buffer_size, data, size);
        context->buffer_size = new_size;

        data = data_end;
        break;
      }

      size_t copy_size;
      if (result == &part0)
      {
        copy_size = position + _line_end_symbol.size - part0.size;
      }
      else if (result == &part1)
      {
        copy_size = position + _line_end_symbol.size;
      }
      else
      {
        // never branch
        _invoke_on_error(context, "Internal error");
        data = NULL;
        break;
      }

      const size_t new_size = context->buffer_size + copy_size;
      void *new_buffer = context->realloc_buffer(context, context->buffer, new_size);
      if (new_buffer == NULL)
      {
        _invoke_on_error(context, "http multi part parser alloc buffer failed");
        data = NULL;
        break;
      }
      context->buffer = (char *)new_buffer;
      memcpy(context->buffer + context->buffer_size, data, copy_size);
      context->buffer_size = new_size;
      const char *line_end = context->buffer + context->buffer_size - _line_end_symbol.size;

      context->expected_chunk_size = _string_to_unsigned(context->buffer, line_end, HEX_BASE);
      context->free_buffer(context, context->buffer, context->buffer_size);
      context->buffer = NULL;
      context->buffer_size = 0;
      context->parser_status = _parser_status_chunk_body;
      data = data + copy_size;
      break;
    }
    case _parser_status_chunk_body:
    {
      const char *chunk_end = _skip_to_chunk_end(data, size, context->expected_chunk_size);
      if (chunk_end == NULL) // not completed message
      {
        void *new_buffer = context->realloc_buffer(context, context->buffer, size);
        if (new_buffer == NULL)
        {
          _invoke_on_error(context, "http multi part parser alloc buffer failed");
          data = NULL;
          break;
        }
        context->buffer = (char *)new_buffer;
        memcpy(context->buffer, data, size);
        context->buffer_size = size;
        context->parser_status = _parser_status_chunk_body_half_way;
        data = data_end;
        break;
      }
      context->on_chunk(context, data, context->expected_chunk_size);
      if (context->expected_chunk_size == 0)
      {
        context->parser_status = _parser_status_chunk_completed;
      }
      else
      {
        context->parser_status = _parser_status_chunk_header;
      }
      data = chunk_end;
      break;
    }
    case _parser_status_chunk_body_half_way:
    {
      const size_t expected_recv_size = context->expected_chunk_size + GET_LITERAL_SIZE("\r\n") - context->buffer_size;
      const size_t recv_size = MIN(expected_recv_size, size);

      const size_t new_size = context->buffer_size + recv_size;
      void *new_buffer = context->realloc_buffer(context, context->buffer, new_size);
      if (new_buffer == NULL)
      {
        _invoke_on_error(context, "http multi part parser alloc buffer failed");
        data = NULL;
        break;
      }
      context->buffer = (char *)new_buffer;
      memcpy(context->buffer + context->buffer_size, data, recv_size);
      context->buffer_size = new_size;

      if (recv_size < expected_recv_size)
      {
        data = data + recv_size;
        break;
      }

      context->on_chunk(context, context->buffer, context->expected_chunk_size);

      if (memcmp(context->buffer + context->buffer_size - GET_LITERAL_SIZE("\r\n"),
                 FMT_LITERAL("\r\n")) != 0)
      {
        _invoke_on_error(context, "chunk format error");
      }

      if (context->expected_chunk_size == 0)
      {
        context->parser_status = _parser_status_chunk_completed;
      }
      else
      {
        context->parser_status = _parser_status_chunk_header;
      }

      data = data + recv_size;
      break;
    }
    default:
      break;
    }

    if (context->parser_status == _parser_status_chunk_completed)
    {
      context->on_completed(context);
      if (remain_not_recv_size != NULL)
      {
        *remain_not_recv_size = 0;
      }
      return data - data_start;
    }
  }

  if (remain_not_recv_size != NULL)
  {
    memset(remain_not_recv_size, 0xFF, sizeof(*remain_not_recv_size));
  }
  return full_size;
}

void tiny_http_parser_chunk_deinit(TinyHttpParserChunkContext_t *context)
{
  if (context->buffer != NULL)
  {
    context->free_buffer(context, context->buffer, context->buffer_size);
    context->buffer = NULL;
  }
}

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L // C99

static void _ignore_tiny_http_parser_websocket_on_control(TinyHttpParserWebsocketContext_t *context, uint8_t opcode, const char *data, size_t size)
{
  UNUSED(context);
  UNUSED(opcode);
  UNUSED(data);
  UNUSED(size);
}
static void _ignore_tiny_http_parser_websocket_on_message(TinyHttpParserWebsocketContext_t *context, uint8_t opcode, const char *data, size_t size)
{
  UNUSED(context);
  UNUSED(opcode);
  UNUSED(data);
  UNUSED(size);
}

static void _ignore_tiny_http_parser_websocket_on_error(TinyHttpParserWebsocketContext_t *context, const char *reason)
{
  UNUSED(context);
  UNUSED(reason);
}

enum
{
  _parser_status_websocket_data_frame_header,
  _parser_status_websocket_data_frame_payload,
  _parser_status_websocket_data_frame_payload_half_way,
  _parser_status_websocket_data_frame_closed,
};

DEFAULT_REALLOC(_default_tiny_http_parser_websocket_realloc_buffer, TinyHttpParserWebsocketContext_t);
DEFAULT_FREE(_default_tiny_http_parser_websocket_free_buffer, TinyHttpParserWebsocketContext_t);

void tiny_http_parser_websocket_init(TinyHttpParserWebsocketContext_t *context,
                                     void *user_context,
                                     tiny_http_parser_websocket_on_control_fn on_control,
                                     tiny_http_parser_websocket_on_message_fn on_message,
                                     tiny_http_parser_websocket_on_error_fn on_error,
                                     tiny_http_parser_websocket_realloc_buffer_fn realloc_buffer,
                                     tiny_http_parser_websocket_free_buffer_fn free_buffer)
{
  context->user_context = user_context;
  context->on_control = on_control != NULL ? on_control : _ignore_tiny_http_parser_websocket_on_control;
  context->on_message = on_message != NULL ? on_message : _ignore_tiny_http_parser_websocket_on_message;
  context->on_error = on_error != NULL ? on_error : _ignore_tiny_http_parser_websocket_on_error;
  context->realloc_buffer = realloc_buffer != NULL ? realloc_buffer : _default_tiny_http_parser_websocket_realloc_buffer;
  context->free_buffer = free_buffer != NULL ? free_buffer : _default_tiny_http_parser_websocket_free_buffer;

  context->error_reason = NULL;
  context->parser_status = _parser_status_websocket_data_frame_header;
  context->buffer = NULL;
  context->buffer_size = 0;
  context->header_size = 0;
}

#define _websocket_fin(c) (c & 0b10000000)
#define _websocket_opcode(c) (c & 0b00001111)
#define _websocket_mask_flag(c) (c & 0b10000000)
#define _websocket_payload_length(c) (c & 0b01111111)

// gcc and clang support `__BYTE_ORDER__`

#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

// nothing

#else

static uint16_t _swap_endian_16(uint16_t val)
{
  return (val << 8) | (val >> 8);
}

static uint64_t _swap_endian_64(uint64_t val)
{
  val = ((val << 8) & 0xFF00FF00FF00FF00ULL) |
        ((val >> 8) & 0x00FF00FF00FF00FFULL);
  val = ((val << 16) & 0xFFFF0000FFFF0000ULL) |
        ((val >> 16) & 0x0000FFFF0000FFFFULL);
  return (val << 32) | (val >> 32);
}

#endif

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

static uint16_t _ntohs(uint16_t val)
{
  return _swap_endian_16(val);
}

static uint64_t _ntohll(uint64_t val)
{
  return _swap_endian_64(val);
}

#elif defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

static uint16_t _ntohs(uint16_t val)
{
  return val;
}

static uint64_t _ntohll(uint64_t val)
{
  return val;
}

#else // fallback runtime check

static int _is_big_endian_raw()
{
  const uint32_t x = 1;
  const uint8_t *byte_ptr = (const uint8_t *)&x;

  if (*byte_ptr == 0)
  {
    return 1; // True: Big Endian
  }
  else
  {
    return 0; // False: Little Endian
  }
}

static int _is_big_endian()
{
  static int result = -1;
  if (result == -1) // cache result
  {
    result = _is_big_endian_raw();
  }
  return result;
}

static uint16_t _ntohs(uint16_t val)
{
  if (_is_big_endian())
  {
    return val;
  }
  return _swap_endian_16(val);
}

static uint64_t _ntohll(uint64_t val)
{
  if (_is_big_endian())
  {
    return val;
  }
  return _swap_endian_64(val);
}

#endif

#define WEBSOCKET_MASK_KEY_SIZE 4

static int _parser_websocket_header(TinyHttpParserWebsocketContext_t *context)
{
  size_t expected_header_size = 2;
  if (context->header_size < expected_header_size)
  {
    return 1; // data frame header not completed
  }
  uint8_t fin = _websocket_fin(context->header[0]);
  uint8_t opcode = _websocket_opcode(context->header[0]);
  uint8_t mask_flag = _websocket_mask_flag(context->header[1]);
  uint8_t payload_length = _websocket_payload_length(context->header[1]);

  if (opcode >= 8 && fin == 0)
  {
    _invoke_on_error(context, "Control frames should be unfragmetable");
    return -1;
  }

  switch (payload_length)
  {
  case 126:
    expected_header_size += 2;
    break;
  case 127:
    expected_header_size += 8;
    break;
  default:
    break;
  }

  if (mask_flag != 0)
  {
    context->current_mask_key = context->header + expected_header_size;
    expected_header_size += WEBSOCKET_MASK_KEY_SIZE;
  }
  else
  {
    context->current_mask_key = NULL;
  }

  if (context->header_size < expected_header_size)
  {
    return 1; // data frame header not completed
  }

  uint64_t extend_payload_length;
#define WEBSOCKET_EXTEND_PAYLOAD_LENGTH_OFFSET 2
  switch (payload_length)
  {
  case 126:
  {
    uint16_t length;
    memcpy(&length, context->header + WEBSOCKET_EXTEND_PAYLOAD_LENGTH_OFFSET, sizeof(length));
    extend_payload_length = _ntohs(length);
    if (extend_payload_length < 126)
    {
      _invoke_on_error(context, "The minimum number of bits must be used");
      return -1;
    }
    break;
  }
  case 127:
  {
    uint64_t length;
    memcpy(&length, context->header + WEBSOCKET_EXTEND_PAYLOAD_LENGTH_OFFSET, sizeof(length));
    extend_payload_length = _ntohll(length);
    if (extend_payload_length < _pow_unsigned(2, 16))
    {
      _invoke_on_error(context, "The minimum number of bits must be used");
      return -1;
    }
    if (extend_payload_length >> 63)
    {
      _invoke_on_error(context, "The most significant bit must be zero");
      return -1;
    }
    break;
  }
  default:
    extend_payload_length = payload_length;
    break;
  }
  if (opcode >= 8 && extend_payload_length > 125)
  {
    _invoke_on_error(context, "Control frames must have up to 125 bytes");
    return -1;
  }
  if (extend_payload_length > SIZE_MAX)
  {
    _invoke_on_error(context, "Data frame is too big");
    return -1;
  }

  context->header_size = expected_header_size;
  context->current_payload_length = extend_payload_length;
  context->recv_payload_length = 0;
  return 0;
}

static void _mask_websocket_payload(char *data, size_t size, const uint8_t *mask_key, size_t mask_key_size)
{
  if (mask_key == NULL)
  {
    return;
  }
  for (size_t i = 0; i < size; i++)
  {
    data[i] ^= mask_key[i % mask_key_size];
  }
}

size_t tiny_http_parser_websocket_input(TinyHttpParserWebsocketContext_t *context, const char *data, size_t full_size, size_t *remain_not_recv_size)
{
  assert(context->parser_status != _parser_status_websocket_data_frame_closed);
  const char *const data_end = data + full_size;
  for (; data != NULL && data < data_end;)
  {
    switch (context->parser_status)
    {
    case _parser_status_websocket_data_frame_header:
    {
      const size_t size = data_end - data;
      size_t consume_data_size = sizeof(context->header) - context->header_size;
      consume_data_size = MIN(consume_data_size, size);
      memcpy(context->header + context->header_size, data, consume_data_size);
      const size_t old_header_size = context->header_size;
      context->header_size = old_header_size + consume_data_size;
      const int status = _parser_websocket_header(context);
      if (status < 0) // error
      {
        data = NULL;
        break;
      }
      if (status > 0) // header not completed
      {
        data = data + consume_data_size;
        break;
      }
      // status == 0 ok
      context->parser_status = _parser_status_websocket_data_frame_payload;
      consume_data_size = context->header_size - old_header_size;
      data = data + consume_data_size;
      break;
    }
    default:
      break;
    }

    // the payload length maybe 0
    // put the branch(_parser_status_websocket_data_frame_payload) end of the loop step
    // to ensure the callbacks would be called in time

    const size_t size = data_end - data;
    const uint8_t opcode = _websocket_opcode(context->header[0]);
    const uint8_t fin = _websocket_opcode(context->header[0]);
    const tiny_http_parser_websocket_on_message_fn callback =
        opcode < 8 ? context->on_message : context->on_control;
    int8_t is_callback_invoked = 0;
    int8_t is_payload_completed = 0;
    switch (context->parser_status)
    {
    case _parser_status_websocket_data_frame_payload:
    {
      const size_t expected_recv_length = context->current_payload_length;
      if (context->buffer == NULL && fin != 0) // first frame and fin
      {
        if ((expected_recv_length == 0) ||                                       // 0 size
            (size >= expected_recv_length && context->current_mask_key == NULL)) // no mask and completed  frame
        {
          // skip memory allocation
          callback(context, opcode, data, expected_recv_length);
          is_callback_invoked = 1;
          is_payload_completed = 1;

          data = data + expected_recv_length;
          break;
        }
      }

      if (expected_recv_length == 0) // 0 size
      {
        is_payload_completed = 1;
        break;
      }

      // alloc memory
      size_t new_size = context->buffer_size + expected_recv_length;
      void *new_buffer = context->realloc_buffer(context, context->buffer, new_size);
      if (new_size != 0 && new_buffer == NULL)
      {
        _invoke_on_error(context, "http websocket parser alloc buffer failed");
        data = NULL;
        break;
      }
      context->buffer = (char *)new_buffer;
      context->buffer_size = new_size;

      context->parser_status = _parser_status_websocket_data_frame_payload_half_way;
      break;
    }
    case _parser_status_websocket_data_frame_payload_half_way:
    {
      size_t expected_recv_length = context->current_payload_length - context->recv_payload_length;
      char *const payload_start = context->buffer + context->buffer_size - context->current_payload_length;
      if (size < expected_recv_length)
      {
        memcpy(payload_start + context->recv_payload_length, data, size);
        context->recv_payload_length += size;
        data = data + size;
        break;
      }
      memcpy(payload_start + context->recv_payload_length, data, expected_recv_length);
      _mask_websocket_payload(payload_start, context->current_payload_length, context->current_mask_key, WEBSOCKET_MASK_KEY_SIZE);

      data = data + expected_recv_length;

      if (fin != 0)
      {
        callback(context, opcode, context->buffer, context->buffer_size);
        is_callback_invoked = 1;

        context->free_buffer(context, context->buffer, context->buffer_size);
        context->buffer = NULL;
        context->buffer_size = 0;
      }

      is_payload_completed = 1;
      break;
    }

    default:
      break;
    }

    if (is_payload_completed != 0)
    {
      // reset header
      context->header_size = 0;
      context->parser_status = _parser_status_websocket_data_frame_header;
    }

    if (is_callback_invoked != 0 && opcode == 8) // close
    {
      context->parser_status = _parser_status_websocket_data_frame_closed;
      if (remain_not_recv_size)
      {
        *remain_not_recv_size = 0;
      }
      const size_t consume_data_size = data_end - data;
      return consume_data_size;
    }
  }
  if (remain_not_recv_size)
  {
    memset(remain_not_recv_size, 0xFF, sizeof(*remain_not_recv_size));
  }
  return full_size;
}

void tiny_http_parser_websocket_deinit(TinyHttpParserWebsocketContext_t *context)
{
  if (context->buffer != NULL)
  {
    context->free_buffer(context, context->buffer, context->buffer_size);
    context->buffer = NULL;
    context->buffer_size = 0;
  }
}

const uint8_t *tiny_http_parser_websocket_get_mask(TinyHttpParserWebsocketContext_t *context)
{
  return context->current_mask_key;
}

#endif
