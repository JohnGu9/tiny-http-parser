#include "tiny-http-parser.h"

enum
{
  parser_status_header,
  parser_status_header_half_way,
  parser_status_body,
};

void tiny_http_parser_init(TinyHttpParserContext_t *context,
                           void *user_context,
                           TinyHttpParserPair_t *headers_list,
                           TINY_HTTP_PARSER_SIZE_T headers_list_max_size,
                           tiny_http_parser_on_header_fn on_header,
                           tiny_http_parser_on_body_stream_fn on_body_stream,
                           tiny_http_parser_on_error_fn on_error,
                           tiny_http_parser_alloc_header_buffer_fn alloc_header_buffer,
                           tiny_http_parser_free_header_buffer_fn free_header_buffer)
{
  context->user_context = user_context;
  context->headers_list = headers_list;
  context->header_buffer_max_size = headers_list_max_size;
  context->on_header = on_header;
  context->on_body_stream = on_body_stream;
  context->on_error = on_error;
  context->alloc_header_buffer = alloc_header_buffer;
  context->free_header_buffer = free_header_buffer;
  context->header_buffer = NULL;
  context->header_buffer_size = 0;
  context->header_buffer_max_size = 0;
  context->expected_body_size = 0;
  context->parser_status = parser_status_header;
}

static const char *find_sub_string(const char *data, TINY_HTTP_PARSER_SIZE_T size, const char *target, TINY_HTTP_PARSER_SIZE_T target_size)
{
  if (target_size <= size)
  {
    for (TINY_HTTP_PARSER_SIZE_T i = 0; i <= size - target_size; i++)
    {
      if (strncmp(target, data + i, target_size) == 0)
      {
        return data + i;
      }
    }
  }
  return NULL;
}

static const char *find_header_end(const char *data, TINY_HTTP_PARSER_SIZE_T size)
{
  const char header_end_symbol[] = "\r\n\r\n";
  const char *end = find_sub_string(data, size, header_end_symbol, sizeof(header_end_symbol) - 1);
  if (end != NULL)
  {
    return end + sizeof(header_end_symbol) - 1;
  }

  return NULL;
}

static const char *goto_next_not_char(char c, const char *data, TINY_HTTP_PARSER_SIZE_T size)
{
  for (TINY_HTTP_PARSER_SIZE_T i = 0; i < size; i++)
  {
    if (data[i] != c)
      return &data[i];
  }
  return &data[size];
}

static const char *goto_next_char(char c, const char *data, TINY_HTTP_PARSER_SIZE_T size)
{
  for (TINY_HTTP_PARSER_SIZE_T i = 0; i < size; i++)
  {
    if (data[i] == c)
      return &data[i];
  }
  return &data[size];
}

static TINY_HTTP_PARSER_SIZE_T thp_min(TINY_HTTP_PARSER_SIZE_T a, TINY_HTTP_PARSER_SIZE_T b)
{
  return a < b ? a : b;
}

static void get_chunk(const char *data, const char *data_end, TinyHttpParserStringView_t *string_view)
{
  const char *start = goto_next_not_char(' ', data, (TINY_HTTP_PARSER_SIZE_T)(data_end - data));
  const char *end = goto_next_char(' ', start, (TINY_HTTP_PARSER_SIZE_T)(data_end - start));
  *string_view = (TinyHttpParserStringView_t){
      .data = start,
      .size = (TINY_HTTP_PARSER_SIZE_T)(end - start),
  };
}

static const char line_end_symbol[] = "\r\n";

static TINY_HTTP_PARSER_SIZE_T parser_optional_headers(TinyHttpParserPair_t *headers_list,
                                                       TINY_HTTP_PARSER_SIZE_T headers_list_max_size,
                                                       const char *data,
                                                       const char *data_end)
{
  TINY_HTTP_PARSER_SIZE_T headers_list_size = 0;
  for (const char *line_start = data; line_start < data_end;)
  {
    const char *line_end = find_sub_string(line_start, (TINY_HTTP_PARSER_SIZE_T)(data_end - line_start), line_end_symbol, sizeof(line_end_symbol) - 1);
    if (line_start == line_end)
    {
      break;
    }
    const char *key_end = goto_next_char(':', line_start, (TINY_HTTP_PARSER_SIZE_T)(line_end - line_start));
    if (headers_list_size < headers_list_max_size)
    {
      TinyHttpParserPair_t *pair = &headers_list[headers_list_size++];
      if (key_end >= line_end - 1)
      {
        // @TODO: error handle
        *pair = (TinyHttpParserPair_t){
            .raw = {
                .data = line_start,
                .size = (TINY_HTTP_PARSER_SIZE_T)(line_end - line_start),
            },
            .key = {
                .data = line_start,
                .size = (TINY_HTTP_PARSER_SIZE_T)(key_end - line_start),
            },
            .value = {
                .data = key_end,
                .size = 0,
            },
        };
      }
      else
      {
        const char *value_search_start = key_end + 1;
        const char *value_start = goto_next_not_char(' ', value_search_start, (TINY_HTTP_PARSER_SIZE_T)(line_end - value_search_start));
        *pair = (TinyHttpParserPair_t){
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
    }
    line_start = line_end + sizeof(line_end_symbol) - 1;
  }
  return headers_list_size;
}

static int parser_response_headers(TinyHttpParserResponseHeader_t *response,
                                   TINY_HTTP_PARSER_SIZE_T headers_list_max_size,
                                   const char *data,
                                   TINY_HTTP_PARSER_SIZE_T size)
{
  const char *line_end = find_sub_string(data, size, line_end_symbol, sizeof(line_end_symbol) - 1);

  get_chunk(data, line_end, &response->protocol);
  get_chunk(response->protocol.data + response->protocol.size, line_end, &response->status_code);
  get_chunk(response->status_code.data + response->status_code.size, line_end, &response->reason_phrase);

  const char *line_start = line_end + sizeof(line_end_symbol) - 1;

  const char *data_end = data + size;
  response->headers_list_size = parser_optional_headers(response->headers_list, headers_list_max_size, line_start, data_end);
  return 0;
}

static int parser_request_headers(TinyHttpParserRequestHeader_t *response,
                                  TINY_HTTP_PARSER_SIZE_T headers_list_max_size,
                                  const char *data,
                                  TINY_HTTP_PARSER_SIZE_T size)
{
  const char *line_end = find_sub_string(data, size, line_end_symbol, sizeof(line_end_symbol) - 1);

  get_chunk(data, line_end, &response->method);
  get_chunk(response->method.data + response->method.size, line_end, &response->path);
  get_chunk(response->path.data + response->path.size, line_end, &response->protocol);

  const char *line_start = line_end + sizeof(line_end_symbol) - 1;

  const char *data_end = data + size;
  response->headers_list_size = parser_optional_headers(response->headers_list, headers_list_max_size, line_start, data_end);
  return 0;
}

static int parser_headers(TinyHttpParserContext_t *context, TINY_HTTP_PARSER_SIZE_T *expected_body_size, const char *data, TINY_HTTP_PARSER_SIZE_T size)
{
  TinyHttpParserHeader_t header = {0};
  header.raw = (TinyHttpParserStringView_t){
      .data = data,
      .size = size,
  };
  int status = 0;
  if (strncmp(data, "HTTP", thp_min(size, 4)) == 0)
  {
    TinyHttpParserResponseHeader_t response;
    response.headers_list = context->headers_list;
    response.headers_list_size = 0;
    header.response_header = &response;
    status = parser_response_headers(&response, context->headers_list_max_size, data, size);
    if (status == 0)
    {
      *expected_body_size = context->on_header(context, &header);
    }
  }
  else
  {
    TinyHttpParserRequestHeader_t request;
    request.headers_list = context->headers_list;
    request.headers_list_size = 0;
    header.request_header = &request;
    status = parser_request_headers(&request, context->headers_list_max_size, data, size);
    if (status == 0)
    {
      *expected_body_size = context->on_header(context, &header);
    }
  }
  return status;
}

void tiny_http_parser_input(TinyHttpParserContext_t *context, const char *const data, TINY_HTTP_PARSER_SIZE_T size)
{
  if (size == 0)
  {
    return;
  }
  TinyHttpParserStringView_t remain_data = {0};
  switch (context->parser_status)
  {
  case parser_status_body:
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
    TINY_HTTP_PARSER_SIZE_T override_body_size = context->on_body_stream(context, data, body_size, remain_not_recv_size);
    context->expected_body_size = remain_not_recv_size;
    if (override_body_size < body_size)
    {
      body_size = override_body_size;
    }
    if (body_size < size)
    {
      remain_data = (TinyHttpParserStringView_t){
          .data = data + body_size,
          .size = size - body_size,
      };
      context->expected_body_size = 0;
      context->parser_status = parser_status_header;
    }

    break;
  }
  case parser_status_header:
  {
    const char *header_end = find_header_end(data, size);
    if (header_end == NULL)
    {
      context->header_buffer = context->alloc_header_buffer(context, &context->header_buffer_max_size);
      if (context->header_buffer == NULL)
      {
        context->on_error(context, "http alloc header buffer failed. ");
        break;
      }
      if (context->header_buffer_max_size <= size)
      {
        context->on_error(context, "http alloc header buffer size is too small. ");
        break;
      }
      memcpy(context->header_buffer, data, size);
      context->header_buffer_size = size;
      context->parser_status = parser_status_header_half_way;
      break;
    }

    TINY_HTTP_PARSER_SIZE_T header_size = (TINY_HTTP_PARSER_SIZE_T)(header_end - data);
    TINY_HTTP_PARSER_SIZE_T body_size = 0;
    int status = parser_headers(context, &body_size, data, header_size);
    if (status == 0)
    {
      context->expected_body_size = body_size;
      if (body_size == 0)
      {
        context->parser_status = parser_status_header;
      }
      else
      {
        context->parser_status = parser_status_body;
      }
      if (header_size < size)
      {
        remain_data = (TinyHttpParserStringView_t){
            .data = header_end,
            .size = (TINY_HTTP_PARSER_SIZE_T)(size - header_size),
        };
      }
    }
    else
    {
      context->on_error(context, "http header parser failed. ");
    }
    break;
  }
  case parser_status_header_half_way:
  {
    TINY_HTTP_PARSER_SIZE_T new_size = context->header_buffer_size + size;
    if (new_size > context->header_buffer_max_size)
    {
      context->on_error(context, "http alloc header buffer size is too small. ");
      break;
    }
    TINY_HTTP_PARSER_SIZE_T old_size = context->header_buffer_size;
    memcpy((context->header_buffer + context->header_buffer_size), data, size);
    context->header_buffer_size = new_size;

    const char *header_end = find_header_end(context->header_buffer, context->header_buffer_size); // @TODO: not to find sub string from the start
    if (header_end == NULL)
    {
      break;
    }

    TINY_HTTP_PARSER_SIZE_T header_size = (TINY_HTTP_PARSER_SIZE_T)(header_end - context->header_buffer);
    TINY_HTTP_PARSER_SIZE_T body_size = 0;
    int status = parser_headers(context, &body_size, context->header_buffer, header_size);
    if (status == 0)
    {
      context->expected_body_size = body_size;
      if (body_size == 0)
      {
        context->parser_status = parser_status_header;
      }
      else
      {
        context->parser_status = parser_status_body;
      }
      if (header_size < context->header_buffer_size)
      {
        TINY_HTTP_PARSER_SIZE_T start = header_size - old_size;
        remain_data = (TinyHttpParserStringView_t){
            .data = data + start,
            .size = size - start,
        };
      }
    }
    else
    {
      context->on_error(context, "http header parser failed. ");
    }

    context->free_header_buffer(context, context->header_buffer, context->header_buffer_max_size);
    context->header_buffer = NULL;
    context->header_buffer_max_size = 0;
    context->header_buffer_size = 0;
    break;
  }

  default:
    break;
  }
  if (remain_data.size != 0)
  {
    tiny_http_parser_input(context, remain_data.data, remain_data.size);
  }
}

void tiny_http_parser_deinit(TinyHttpParserContext_t *context)
{
  if (context->header_buffer != NULL)
  {
    context->free_header_buffer(context, context->header_buffer, context->header_buffer_max_size);
    context->header_buffer = NULL;
  }
}

// this function ignore overflow
TINY_HTTP_PARSER_SIZE_T to_size_t(const char *str, TINY_HTTP_PARSER_SIZE_T len)
{
  TINY_HTTP_PARSER_SIZE_T mult = 1;
  TINY_HTTP_PARSER_SIZE_T re = 0;
  for (TINY_HTTP_PARSER_SIZE_T i = len - 1;; i--)
  {
    re = re + ((int)str[i] - 48) * mult;
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
  const char content_string[] = "Content-";
  const char content_length_string[] = "Content-Length";
  TINY_HTTP_PARSER_SIZE_T body_size = 0;
  for (TINY_HTTP_PARSER_SIZE_T i = 0; i < headers_list_size; i++)
  {
    TinyHttpParserPair_t *el = &headers_list[i];
    if (el->key.size == (sizeof(content_length_string) - 1) &&
        strncmp(el->key.data, content_length_string, sizeof(content_length_string) - 1) == 0)
    {
      return to_size_t(el->value.data, el->value.size);
    }
    if (find_sub_string(el->key.data, el->key.size, content_string, sizeof(content_string) - 1) != NULL)
    {
      memset(&body_size, 0xFF, sizeof(body_size)); // max value of TINY_HTTP_PARSER_SIZE_T
    }
  }
  return body_size;
}

// extension

enum
{
  parser_multi_part,
  parser_multi_part_half_way,
  parser_multi_part_completed,
};

void tiny_http_parser_multi_part_init(TinyHttpParserMultiPartContext_t *context, const TinyHttpParserStringView_t *boundary,
                                      void *user_context,
                                      TinyHttpParserPair_t *headers_list,
                                      TINY_HTTP_PARSER_SIZE_T headers_list_max_size,
                                      tny_http_parser_multi_part_on_part_fn on_part,
                                      tny_http_parser_multi_part_on_error_fn on_error,
                                      tny_http_parser_multi_part_alloc_buffer_fn alloc_buffer,
                                      tny_http_parser_multi_part_free_buffer_fn free_buffer)
{
  context->user_context = user_context;
  context->boundary = *boundary;
  context->headers_list = headers_list;
  context->headers_list_max_size = headers_list_max_size;
  context->on_part = on_part;
  context->on_error = on_error;
  context->alloc_buffer = alloc_buffer;
  context->free_buffer = free_buffer;

  context->parser_status = parser_multi_part;
  context->buffer = NULL;
  context->buffer_size = 0;
  context->buffer_max_size = 0;
}

static const char part_end_symbol[] = "\r\n--";

static void thp_multi_part_parser_part(TinyHttpParserMultiPartContext_t *context, const char *data, TINY_HTTP_PARSER_SIZE_T size)
{
  const char *part_header_start = data + 2 + context->boundary.size + 2; // skip --${boundary}\r\n
  const char *part_header_end = find_header_end(data, size);
  const char *body_start = part_header_start;
  TINY_HTTP_PARSER_SIZE_T headers_list_size = 0;
  if (part_header_end != NULL) // has headers
  {
    headers_list_size = parser_optional_headers(context->headers_list, context->headers_list_max_size, part_header_start, part_header_end);
    body_start = part_header_end;
  }
  context->on_part(context, context->headers_list, headers_list_size, body_start, size - (TINY_HTTP_PARSER_SIZE_T)(body_start - data));
}

static const char *tiny_http_parser_multi_part_input_internal(TinyHttpParserMultiPartContext_t *context, const char *data, const char *data_end)
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
      strncmp(data + 2 + context->boundary.size + 2, line_end_symbol, sizeof(line_end_symbol) - 1) == 0)
  {
    // --${boundary}--\r\n
    // end of part
    context->parser_status = parser_multi_part_completed;
    return data + 6 + context->boundary.size;
  }
  if (size >= (4 + context->boundary.size) &&
      strncmp(data + 2 + context->boundary.size, line_end_symbol, sizeof(line_end_symbol) - 1) != 0)
  {
    // failed to match
    // --${boundary}\r\n
    context->on_error(context, "http multi part parser failed. ");
    return NULL;
  }

  const char *part_end = find_sub_string(data, size, part_end_symbol, sizeof(part_end_symbol) - 1);
  if (part_end != NULL &&
      size - (part_end - data) >= context->boundary.size &&
      strncmp(part_end + sizeof(part_end_symbol) - 1, context->boundary.data, context->boundary.size) == 0)
  {
    // next --${boundary} found
    TINY_HTTP_PARSER_SIZE_T consume_size = (TINY_HTTP_PARSER_SIZE_T)(part_end - data);
    thp_multi_part_parser_part(context, data, consume_size);
    return data + consume_size + sizeof(line_end_symbol) - 1;
  }

  // not completed data
  return data;
}

TINY_HTTP_PARSER_SIZE_T tiny_http_parser_multi_part_input(TinyHttpParserMultiPartContext_t *context, const char *data, TINY_HTTP_PARSER_SIZE_T size)
{
  const char *data_start = data;
  const char *data_end = data + size;
  for (; data != NULL && data < data_end;)
  {
    const TINY_HTTP_PARSER_SIZE_T size = data_end - data;
    switch (context->parser_status)
    {
    case parser_multi_part_completed:
    {
      return (TINY_HTTP_PARSER_SIZE_T)(data - data_start);
    }
    case parser_multi_part:
    {
      const char *next_data = tiny_http_parser_multi_part_input_internal(context, data, data_end);
      if (next_data == data) // not completed message
      {
        context->buffer = context->alloc_buffer(context, &context->buffer_max_size);
        if (context->buffer == NULL)
        {
          context->on_error(context, "http multi part parser alloc buffer failed. ");
          data = NULL;
          break;
        }
        if (context->buffer_max_size <= size)
        {
          context->on_error(context, "http multi part parser alloc buffer is too small. ");
          data = NULL;
          break;
        }
        memcpy(context->buffer, data, size);
        context->buffer_size = size;
        context->parser_status = parser_multi_part_half_way;

        data = data_end;
      }
      else
      {
        data = next_data;
      }
      break;
    }
    case parser_multi_part_half_way:
    {
      const TINY_HTTP_PARSER_SIZE_T old_size = context->buffer_size;
      const TINY_HTTP_PARSER_SIZE_T new_size = context->buffer_size + size;
      if (new_size > context->buffer_max_size)
      {
        context->on_error(context, "http multi part parser alloc buffer is too small. ");
        data = NULL;
        break;
      }
      memcpy(context->buffer + old_size, data, size);
      context->buffer_size = new_size;
      const char *next_data = tiny_http_parser_multi_part_input_internal(context, context->buffer, context->buffer + new_size);
      if (next_data == context->buffer) // not completed message
      {
        data = data_end;
      }
      else if (next_data != NULL)
      {
        const TINY_HTTP_PARSER_SIZE_T consume_size = (TINY_HTTP_PARSER_SIZE_T)(next_data - context->buffer);
        const TINY_HTTP_PARSER_SIZE_T consume_data_size = consume_size - old_size;

        context->free_buffer(context, context->buffer, context->buffer_max_size);
        context->buffer = NULL;
        context->buffer_size = 0;
        context->buffer_max_size = 0;
        context->parser_status = parser_multi_part;

        data = data + consume_data_size;
      }
      break;
    }
    default:
      break;
    }
  }
  return size;
}

void tiny_http_parser_multi_part_deinit(TinyHttpParserMultiPartContext_t *context)
{
  if (context->buffer != NULL)
  {
    context->free_buffer(context, context->buffer, context->buffer_max_size);
    context->buffer = NULL;
  }
}
