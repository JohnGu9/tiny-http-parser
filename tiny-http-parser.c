#include "tiny-http-parser.h"
#include "kmp.h"
#include <math.h>

#define find_sub_string kmp_find_sub_string

#define GET_LITERAL_SIZE(literal) (sizeof(literal) - 1)
#define FMT_LITERAL(literal) literal, GET_LITERAL_SIZE(literal)

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

enum
{
  _parser_status_header,
  _parser_status_header_half_way,
  _parser_status_body,
};

static TINY_HTTP_PARSER_SIZE_T _ignore_tiny_http_parser_on_header(TinyHttpParserContext_t *context, const TinyHttpParserHeader_t *header)
{
  return tiny_http_parser_default_on_header(context, header);
}

static TINY_HTTP_PARSER_SIZE_T _ignore_tiny_http_parser_on_body_stream_fn(TinyHttpParserContext_t *context, const char *data, TINY_HTTP_PARSER_SIZE_T size, TINY_HTTP_PARSER_SIZE_T *remain_not_recv_size)
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
  context->on_header = on_header != NULL ? on_header : _ignore_tiny_http_parser_on_header;
  context->on_body_stream = on_body_stream != NULL ? on_body_stream : _ignore_tiny_http_parser_on_body_stream_fn;
  context->on_body_completed = on_body_completed != NULL ? on_body_completed : _ignore_tiny_http_parser_on_body_completed_fn;
  context->on_error = on_error != NULL ? on_error : _ignore_tiny_http_parser_on_error_fn;
  context->realloc_buffer = realloc_buffer;
  context->free_buffer = free_buffer;
  context->header_buffer = NULL;
  context->header_buffer_size = 0;
  context->expected_body_size = 0;
  context->parser_status = _parser_status_header;
}

static const char *_find_header_end(const char *data, TINY_HTTP_PARSER_SIZE_T size)
{
  const char *end = find_sub_string(data, size, FMT_LITERAL("\r\n\r\n"));
  if (end != NULL)
  {
    return end + GET_LITERAL_SIZE("\r\n\r\n");
  }

  return NULL;
}

static const char *_goto_next_not_char(char c, const char *data, const char *data_end)
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

static const char *_goto_next_char(char c, const char *data, const char *data_end)
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
  const char *start = _goto_next_not_char(' ', data, data_end);
  const char *end = _goto_next_char(' ', start, data_end);
  *string_view = (TinyHttpParserStringView_t){
      .data = start,
      .size = (TINY_HTTP_PARSER_SIZE_T)(end - start),
  };
}

static const char _line_end_symbol[] = "\r\n";

static TINY_HTTP_PARSER_SIZE_T _get_headers_list_size(const char *data, const char *data_end)
{
  TINY_HTTP_PARSER_SIZE_T headers_list_size = 0;
  for (const char *line_start = data; line_start < data_end;)
  {
    const char *line_end = find_sub_string(line_start, (TINY_HTTP_PARSER_SIZE_T)(data_end - line_start), FMT_LITERAL(_line_end_symbol));
    if (line_start == line_end)
    {
      // empty line
      break;
    }
    headers_list_size++;
    line_start = line_end + GET_LITERAL_SIZE(_line_end_symbol);
  }
  return headers_list_size;
}

static TINY_HTTP_PARSER_SIZE_T _parser_optional_headers(TinyHttpParserPair_t *headers_list,
                                                        TINY_HTTP_PARSER_SIZE_T headers_list_max_size,
                                                        const char *data,
                                                        const char *data_end)
{
  TINY_HTTP_PARSER_SIZE_T headers_list_size = 0;
  for (const char *line_start = data; line_start < data_end;)
  {
    const char *line_end = find_sub_string(line_start, (TINY_HTTP_PARSER_SIZE_T)(data_end - line_start), FMT_LITERAL(_line_end_symbol));
    if (line_start == line_end)
    {
      break;
    }
    if (headers_list_size < headers_list_max_size)
    {
      const char *key_end = _goto_next_char(':', line_start, line_end);
      const char *value_start;
      if (key_end >= line_end - 1)
      {
        // @TODO: error handle
        value_start = line_end;
      }
      else
      {
        const char *value_search_start = key_end + 1;
        value_start = _goto_next_not_char(' ', value_search_start, line_end);
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
    line_start = line_end + GET_LITERAL_SIZE(_line_end_symbol);
  }
  return headers_list_size;
}

static TINY_HTTP_PARSER_SIZE_T _parser_response_headers(TinyHttpParserContext_t *context,
                                                        TinyHttpParserHeader_t *header,
                                                        const char *data,
                                                        TINY_HTTP_PARSER_SIZE_T size)
{
  TINY_HTTP_PARSER_SIZE_T expected_body_size = 0;
  TinyHttpParserResponseHeader_t response = {0};
  header->response_header = &response;

  const char *line_end = find_sub_string(data, size, FMT_LITERAL(_line_end_symbol));

  _get_chunk(data, line_end, &response.protocol);
  _get_chunk(response.protocol.data + response.protocol.size, line_end, &response.status_code);
  _get_chunk(response.status_code.data + response.status_code.size, line_end, &response.reason_phrase);

  const char *line_start = line_end + GET_LITERAL_SIZE(_line_end_symbol);

  const char *data_end = data + size;
  response.headers_list_size = _get_headers_list_size(line_start, data_end);
  response.headers_list = context->realloc_buffer(context, NULL, sizeof(TinyHttpParserPair_t) * response.headers_list_size);
  if (response.headers_list != NULL)
  {
    _parser_optional_headers(response.headers_list, response.headers_list_size, line_start, data_end);
    expected_body_size = context->on_header(context, header);
    context->free_buffer(context, response.headers_list, sizeof(TinyHttpParserPair_t) * response.headers_list_size);
  }
  else
  {
    context->on_error(context, "http alloc header buffer failed. ");
    expected_body_size = context->on_header(context, header);
  }
  return expected_body_size;
}

static TINY_HTTP_PARSER_SIZE_T _parser_request_headers(TinyHttpParserContext_t *context,
                                                       TinyHttpParserHeader_t *header,
                                                       const char *data,
                                                       TINY_HTTP_PARSER_SIZE_T size)
{
  TINY_HTTP_PARSER_SIZE_T expected_body_size = 0;
  TinyHttpParserRequestHeader_t request = {0};
  header->request_header = &request;

  const char *line_end = find_sub_string(data, size, FMT_LITERAL(_line_end_symbol));

  _get_chunk(data, line_end, &request.method);
  _get_chunk(request.method.data + request.method.size, line_end, &request.path);
  _get_chunk(request.path.data + request.path.size, line_end, &request.protocol);

  const char *line_start = line_end + GET_LITERAL_SIZE(_line_end_symbol);

  const char *data_end = data + size;
  request.headers_list_size = _get_headers_list_size(line_start, data_end);
  request.headers_list = context->realloc_buffer(context, NULL, sizeof(TinyHttpParserPair_t) * request.headers_list_size);
  if (request.headers_list != NULL)
  {
    _parser_optional_headers(request.headers_list, request.headers_list_size, line_start, data_end);
    expected_body_size = context->on_header(context, header);
    context->free_buffer(context, request.headers_list, sizeof(TinyHttpParserPair_t) * request.headers_list_size);
  }
  else
  {
    context->on_error(context, "http alloc header buffer failed. ");
    expected_body_size = context->on_header(context, header);
  }
  return expected_body_size;
}

static TINY_HTTP_PARSER_SIZE_T _parser_headers(TinyHttpParserContext_t *context, const char *data, TINY_HTTP_PARSER_SIZE_T size)
{
  TinyHttpParserHeader_t header = {0};
  header.raw = (TinyHttpParserStringView_t){
      .data = data,
      .size = size,
  };
  if (size < 4)
  {
    // @TODO: error handle
    return 0;
  }
  if (strncmp(data, "HTTP", 4) == 0)
  {
    return _parser_response_headers(context, &header, data, size);
  }
  else
  {
    return _parser_request_headers(context, &header, data, size);
  }
}

void tiny_http_parser_input(TinyHttpParserContext_t *context, const char *data, TINY_HTTP_PARSER_SIZE_T full_size)
{
  const char *const data_end = data + full_size;
  for (; data != NULL && data < data_end;)
  {
    TINY_HTTP_PARSER_SIZE_T size = data_end - data;
    switch (context->parser_status)
    {
    case _parser_status_body:
    {
      TINY_HTTP_PARSER_SIZE_T remain_not_recv_size;
      TINY_HTTP_PARSER_SIZE_T body_size;
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
      TINY_HTTP_PARSER_SIZE_T override_body_size = context->on_body_stream(context, data, body_size, &remain_not_recv_size);
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
      const char *header_end = _find_header_end(data, size);
      if (header_end == NULL)
      {
        void *new_buffer = context->realloc_buffer(context, context->header_buffer, size);
        if (new_buffer == NULL)
        {
          context->on_error(context, "http alloc header buffer failed. ");
          return; // critical error, can't continue
        }
        context->header_buffer = (char *)new_buffer;
        memcpy(context->header_buffer, data, size);
        context->header_buffer_size = size;
        context->parser_status = _parser_status_header_half_way;
        data = data_end;
        break;
      }

      TINY_HTTP_PARSER_SIZE_T header_size = (TINY_HTTP_PARSER_SIZE_T)(header_end - data);
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
      TINY_HTTP_PARSER_SIZE_T new_size = context->header_buffer_size + size;
      void *new_buffer = context->realloc_buffer(context, context->header_buffer, new_size);
      if (new_buffer == NULL)
      {
        context->on_error(context, "http alloc header buffer failed. ");
        return; // critical error, can't continue
      }
      context->header_buffer = (char *)new_buffer;
      memcpy((context->header_buffer + context->header_buffer_size), data, size);
      TINY_HTTP_PARSER_SIZE_T old_size = context->header_buffer_size;
      context->header_buffer_size = new_size;

      const char *header_end = _find_header_end(context->header_buffer, context->header_buffer_size); // @TODO: not to find sub string from the start
      if (header_end == NULL)                                                                         // not completed header
      {
        data = data_end;
        break;
      }

      TINY_HTTP_PARSER_SIZE_T header_size = (TINY_HTTP_PARSER_SIZE_T)(header_end - context->header_buffer);
      context->expected_body_size = _parser_headers(context, context->header_buffer, header_size);
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

      TINY_HTTP_PARSER_SIZE_T consume_data_size = header_size - old_size;
      data = data + consume_data_size;
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

// this function ignore overflow
static TINY_HTTP_PARSER_SIZE_T _to_size_t(const char *str, TINY_HTTP_PARSER_SIZE_T len)
{
  TINY_HTTP_PARSER_SIZE_T mult = 1;
  TINY_HTTP_PARSER_SIZE_T re = 0;
  for (TINY_HTTP_PARSER_SIZE_T i = len - 1;; i--)
  {
    re = re + ((int)str[i] - 0x30) * mult;
    mult = mult * 10;
    if (i == 0)
    {
      break;
    }
  }
  return re;
}

TINY_HTTP_PARSER_SIZE_T tiny_http_parser_default_on_header(TinyHttpParserContext_t *context, const TinyHttpParserHeader_t *header)
{
  UNUSED(context);
  TinyHttpParserPair_t *headers_list = NULL;
  TINY_HTTP_PARSER_SIZE_T headers_list_size = 0;
  if (header->response_header != NULL)
  {
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

  TINY_HTTP_PARSER_SIZE_T chunked_length = 0;
  TINY_HTTP_PARSER_SIZE_T unknown_body_size = 0;
  TINY_HTTP_PARSER_SIZE_T content_length = 0;

  for (TINY_HTTP_PARSER_SIZE_T i = 0; i < headers_list_size; i++)
  {
    TinyHttpParserPair_t *el = &headers_list[i];

#define is_string_view_equal_to(string_view, literal) (string_view.size == (sizeof(literal) - 1) && strncmp(string_view.data, literal, sizeof(literal) - 1) == 0)

    if (is_string_view_equal_to(el->key, "Transfer-Encoding") && is_string_view_equal_to(el->value, "chunked"))
    {
      memset(&chunked_length, 0xFF, sizeof(chunked_length)); // max value of TINY_HTTP_PARSER_SIZE_T
    }
    if (is_string_view_equal_to(el->key, "Content-Length"))
    {
      content_length = _to_size_t(el->value.data, el->value.size);
    }
    else if (find_sub_string(el->key.data, el->key.size, FMT_LITERAL("Content-")) != NULL)
    {
      memset(&unknown_body_size, 0xFF, sizeof(unknown_body_size)); // max value of TINY_HTTP_PARSER_SIZE_T
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
  if (unknown_body_size != 0)
  {
    return unknown_body_size;
  }
  return 0;
}

// extension

static void _ignore_tiny_http_parser_multi_part_on_part(TinyHttpParserMultiPartContext_t *context,
                                                        TinyHttpParserPair_t *headers_list,
                                                        TINY_HTTP_PARSER_SIZE_T headers_list_size,
                                                        const char *data, TINY_HTTP_PARSER_SIZE_T size)
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

void tiny_http_parser_multi_part_init(TinyHttpParserMultiPartContext_t *context,
                                      const TinyHttpParserStringView_t *boundary,
                                      void *user_context,
                                      tiny_http_parser_multi_part_on_part_fn on_part,
                                      tiny_http_parser_multi_part_on_completed_fn on_completed,
                                      tiny_http_parser_multi_part_on_error_fn on_error,
                                      tiny_http_parser_multi_part_realloc_buffer_fn realloc_buffer,
                                      tiny_http_parser_multi_part_free_buffer_fn free_buffer)
{
  context->user_context = user_context;
  context->boundary = *boundary;

  context->on_part = on_part != NULL ? on_part : _ignore_tiny_http_parser_multi_part_on_part;
  context->on_completed = on_completed != NULL ? on_completed : _ignore_tiny_http_parser_multi_part_on_completed;
  context->on_error = on_error != NULL ? on_error : _ignore_tiny_http_parser_multi_part_on_error;
  context->realloc_buffer = realloc_buffer;
  context->free_buffer = free_buffer;

  context->parser_status = _parser_multi_part;
  context->buffer = NULL;
  context->buffer_size = 0;
  context->headers_list = NULL;
  context->headers_list_size = 0;
}

static void _thp_multi_part_parser_part(TinyHttpParserMultiPartContext_t *context, const char *data, TINY_HTTP_PARSER_SIZE_T size)
{
  const char *part_header_start = data + 2 + context->boundary.size + 2; // skip --${boundary}\r\n
  const char *part_header_end = _find_header_end(data, size);
  const char *body_start;
  if (part_header_end != NULL) // has headers
  {
    context->headers_list_size = _get_headers_list_size(part_header_start, part_header_end);
    context->headers_list = context->realloc_buffer(context, context->headers_list, sizeof(TinyHttpParserPair_t) * context->headers_list_size);
    if (context->headers_list != NULL)
    {
      _parser_optional_headers(context->headers_list, context->headers_list_size, part_header_start, part_header_end);
    }
    else
    {
      context->on_error(context, "http multi part parser alloc buffer failed. ");
    }
    body_start = part_header_end;
  }
  else
  {
    body_start = part_header_start;
  }
  context->on_part(context, context->headers_list, context->headers_list_size, body_start, size - (TINY_HTTP_PARSER_SIZE_T)(body_start - data));

  if (context->headers_list != NULL)
  {
    context->free_buffer(context, context->headers_list, sizeof(TinyHttpParserPair_t) * context->headers_list_size);
    context->headers_list = NULL;
  }
}

static const char *_tiny_http_parser_multi_part_input(TinyHttpParserMultiPartContext_t *context, const char *data, const char *data_end)
{
  const TINY_HTTP_PARSER_SIZE_T size = (TINY_HTTP_PARSER_SIZE_T)(data_end - data);
  if (size < (2 + context->boundary.size))
  {
    // not completed data
    return data;
  }

  if (strncmp(data, "--", 2) != 0 ||
      strncmp(data + 2, context->boundary.data, context->boundary.size) != 0)
  {
    // failed to match
    // --${boundary}
    context->on_error(context, "http multi part parser failed. ");
    return NULL;
  }
  if (size >= (6 + context->boundary.size) &&
      strncmp(data + 2 + context->boundary.size, "--", 2) == 0 &&
      strncmp(data + 2 + context->boundary.size + 2, FMT_LITERAL(_line_end_symbol)) == 0)
  {
    // end of part
    // --${boundary}--\r\n
    context->parser_status = _parser_multi_part_completed;
    return data + 6 + context->boundary.size;
  }
  if (size >= (4 + context->boundary.size) &&
      strncmp(data + 2 + context->boundary.size, FMT_LITERAL(_line_end_symbol)) != 0)
  {
    // failed to match
    // --${boundary}\r\n
    context->on_error(context, "http multi part parser failed. ");
    return NULL;
  }

  const char *part_end = find_sub_string(data, size, FMT_LITERAL("\r\n--"));
  if (part_end != NULL &&
      size - (part_end - data) >= context->boundary.size &&
      strncmp(part_end + GET_LITERAL_SIZE("\r\n--"), context->boundary.data, context->boundary.size) == 0)
  {
    // next --${boundary} found
    TINY_HTTP_PARSER_SIZE_T consume_size = (TINY_HTTP_PARSER_SIZE_T)(part_end - data);
    _thp_multi_part_parser_part(context, data, consume_size);
    return data + consume_size + GET_LITERAL_SIZE(_line_end_symbol);
  }

  // not completed data
  return data;
}

TINY_HTTP_PARSER_SIZE_T tiny_http_parser_multi_part_input(TinyHttpParserMultiPartContext_t *context,
                                                          const char *data,
                                                          TINY_HTTP_PARSER_SIZE_T full_size,
                                                          TINY_HTTP_PARSER_SIZE_T *remain_not_recv_size)
{
  const char *data_start = data;
  const char *data_end = data + full_size;
  for (; data != NULL && data < data_end;)
  {
    const TINY_HTTP_PARSER_SIZE_T size = data_end - data;
    switch (context->parser_status)
    {
    case _parser_multi_part:
    {
      const char *next_data = _tiny_http_parser_multi_part_input(context, data, data_end);
      if (next_data == data) // not completed message
      {
        void *new_buffer = context->realloc_buffer(context, context->buffer, size);
        if (new_buffer == NULL)
        {
          context->on_error(context, "http multi part parser alloc buffer failed. ");
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
        data = next_data;
      }
      break;
    }
    case _parser_multi_part_half_way:
    {
      const TINY_HTTP_PARSER_SIZE_T old_size = context->buffer_size;
      const TINY_HTTP_PARSER_SIZE_T new_size = context->buffer_size + size;
      void *new_buffer = context->realloc_buffer(context, context->buffer, new_size);
      if (new_buffer == NULL)
      {
        context->on_error(context, "http multi part parser alloc buffer failed. ");
        data = NULL;
        break;
      }
      context->buffer = (char *)new_buffer;
      memcpy(context->buffer + old_size, data, size);
      context->buffer_size = new_size;
      const char *next_data = _tiny_http_parser_multi_part_input(context, context->buffer, context->buffer + new_size);
      if (next_data == context->buffer) // not completed message
      {
        data = data_end;
      }
      else if (next_data != NULL)
      {
        const TINY_HTTP_PARSER_SIZE_T consume_size = (TINY_HTTP_PARSER_SIZE_T)(next_data - context->buffer);
        const TINY_HTTP_PARSER_SIZE_T consume_data_size = consume_size - old_size;

        context->free_buffer(context, context->buffer, context->buffer_size);
        context->buffer = NULL;
        context->buffer_size = 0;
        context->parser_status = _parser_multi_part;

        data = data + consume_data_size;
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
      return (TINY_HTTP_PARSER_SIZE_T)(data - data_start);
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

static void _ignore_tiny_http_parser_chunk_on_chunk_size(TinyHttpParserChunkContext_t *context, TINY_HTTP_PARSER_SIZE_T size)
{
  UNUSED(context);
  UNUSED(size);
}

static void _ignore_tiny_http_parser_chunk_on_chunk(TinyHttpParserChunkContext_t *context, const char *data, TINY_HTTP_PARSER_SIZE_T size)
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

void tiny_http_parser_chunk_init(TinyHttpParserChunkContext_t *context,
                                 void *user_context,
                                 tiny_http_parser_chunk_on_chunk_size_fn on_chunk_size,
                                 tiny_http_parser_chunk_on_chunk_fn on_chunk,
                                 tiny_http_parser_chunk_on_completed_fn on_completed,
                                 tiny_http_parser_chunk_on_error_fn on_error,
                                 tiny_http_parser_chunk_realloc_buffer_fn realloc_buffer,
                                 tiny_http_parser_chunk_free_buffer_fn free_buffer)
{
  context->user_context = user_context;
  context->on_chunk_size = on_chunk_size != NULL ? on_chunk_size : _ignore_tiny_http_parser_chunk_on_chunk_size;
  context->on_chunk = on_chunk != NULL ? on_chunk : _ignore_tiny_http_parser_chunk_on_chunk;
  context->on_completed = on_completed != NULL ? on_completed : _ignore_tiny_http_parser_chunk_on_completed;
  context->on_error = on_error != NULL ? on_error : _ignore_tiny_http_parser_chunk_on_error;
  context->realloc_buffer = realloc_buffer;
  context->free_buffer = free_buffer;

  context->parser_status = _parser_status_chunk_header;
  context->buffer = NULL;
  context->buffer_size = 0;
  context->expected_chunk_size = 0;
}

static TINY_HTTP_PARSER_SIZE_T _hex_char_to_number(char c)
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
  default:
    return c - 0x30;
  }
}

static TINY_HTTP_PARSER_SIZE_T _hex_string_to_number(const char *str, const char *str_end)
{
  TINY_HTTP_PARSER_SIZE_T result = 0;
  for (; str < str_end; str++)
  {
    TINY_HTTP_PARSER_SIZE_T position = str_end - str - 1;
    TINY_HTTP_PARSER_SIZE_T n = _hex_char_to_number(*str);
    result += n * pow(16, position);
  }
  return result;
}

static const char *_skip_to_chunk_end(const char *data, TINY_HTTP_PARSER_SIZE_T size, TINY_HTTP_PARSER_SIZE_T expected_size)
{
  if (size < expected_size)
  {
    return NULL;
  }
  TINY_HTTP_PARSER_SIZE_T remain_size = size - expected_size;
  const char *line_end = find_sub_string(data + expected_size, remain_size, FMT_LITERAL(_line_end_symbol));
  if (line_end == NULL)
  {
    return NULL;
  }
  return line_end + GET_LITERAL_SIZE(_line_end_symbol);
}

TINY_HTTP_PARSER_SIZE_T tiny_http_parser_chunk_input(TinyHttpParserChunkContext_t *context, const char *data, TINY_HTTP_PARSER_SIZE_T full_size, TINY_HTTP_PARSER_SIZE_T *remain_not_recv_size /* nullable */)
{
  const char *const data_start = data;
  const char *const data_end = data + full_size;
  for (; data != NULL && data < data_end;)
  {
    TINY_HTTP_PARSER_SIZE_T size = data_end - data;
    switch (context->parser_status)
    {
    case _parser_status_chunk_header:
    {
      const char *line_end = find_sub_string(data, size, FMT_LITERAL(_line_end_symbol));
      if (line_end == NULL)
      {
        context->buffer = (char *)context->realloc_buffer(context, context->buffer, size);
        if (context->buffer == NULL)
        {
          context->on_error(context, "http multi part parser alloc buffer failed. ");
          data = NULL;
          break;
        }
        memcpy(context->buffer, data, size);
        context->buffer_size = size;
        context->parser_status = _parser_status_chunk_header_half_way;
        data = data_end;
        break;
      }
      context->expected_chunk_size = _hex_string_to_number(data, line_end);
      context->on_chunk_size(context, context->expected_chunk_size);
      context->parser_status = _parser_status_chunk_body;
      data = line_end + GET_LITERAL_SIZE(_line_end_symbol);
      break;
    }
    case _parser_status_chunk_header_half_way:
    {
      const TINY_HTTP_PARSER_SIZE_T old_size = context->buffer_size;
      const TINY_HTTP_PARSER_SIZE_T new_size = context->buffer_size + size;
      void *new_buffer = context->realloc_buffer(context, context->buffer, new_size);
      if (new_buffer == NULL)
      {
        context->on_error(context, "http multi part parser alloc buffer failed. ");
        data = NULL;
        break;
      }
      context->buffer = new_buffer;
      memcpy(context->buffer + old_size, data, size);
      context->buffer_size = new_size;
      const char *line_end = find_sub_string(context->buffer, context->buffer_size, FMT_LITERAL(_line_end_symbol));
      if (line_end == NULL)
      {
        data = data_end;
        break;
      }
      context->expected_chunk_size = _hex_string_to_number(context->buffer, line_end);
      context->on_chunk_size(context, context->expected_chunk_size);
      const TINY_HTTP_PARSER_SIZE_T consume_size = line_end - context->buffer + GET_LITERAL_SIZE(_line_end_symbol);
      context->free_buffer(context, context->buffer, context->buffer_size);
      context->buffer = NULL;
      context->buffer_size = 0;
      context->parser_status = _parser_status_chunk_body;
      const TINY_HTTP_PARSER_SIZE_T consume_data_size = consume_size - old_size;
      data = data + consume_data_size;
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
          context->on_error(context, "http multi part parser alloc buffer failed. ");
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
      const TINY_HTTP_PARSER_SIZE_T old_size = context->buffer_size;
      const TINY_HTTP_PARSER_SIZE_T new_size = context->buffer_size + size;
      void *new_buffer = context->realloc_buffer(context, context->buffer, new_size);
      if (new_buffer == NULL)
      {
        context->on_error(context, "http multi part parser alloc buffer failed. ");
        data = NULL;
        break;
      }
      context->buffer = (char *)new_buffer;
      memcpy(context->buffer + context->buffer_size, data, size);
      context->buffer_size = new_size;

      const char *chunk_end = _skip_to_chunk_end(context->buffer, context->buffer_size, context->expected_chunk_size);
      if (chunk_end == NULL)
      {
        data = data_end;
        break;
      }

      context->on_chunk(context, context->buffer, context->expected_chunk_size);
      if (context->expected_chunk_size == 0)
      {
        context->parser_status = _parser_status_chunk_completed;
      }
      else
      {
        context->parser_status = _parser_status_chunk_header;
      }

      const TINY_HTTP_PARSER_SIZE_T consume_size = (TINY_HTTP_PARSER_SIZE_T)(chunk_end - context->buffer);
      const TINY_HTTP_PARSER_SIZE_T consume_data_size = consume_size - old_size;
      data = data + consume_data_size;
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
      return (TINY_HTTP_PARSER_SIZE_T)(data - data_start);
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
