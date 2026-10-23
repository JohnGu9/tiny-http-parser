# Intro

Pure C implement http parser.

No std/system required.

Design for async programming and handle TCP stick package problem.

Only support CRLF.

# Requirement

`string.h` (for `size_t`/`memcmp()`/`memcpy()`/`memset()`)

# Example

```c
// init
TinyHttpParserContext_t context;
tiny_http_parser_init(&context, ...); // register your callbacks here

// feed the context with fragment data
// tiny-http-parser will invoke your callbacks automatically
tiny_http_parser_input(&context, data, size);
tiny_http_parser_input(&context, data, size);
tiny_http_parser_input(&context, data, size);

// deinit
tiny_http_parser_deinit(&context);
```

Check out [test.c](./test.c) for detail.

# Extension

```c
TinyHttpParserBodyContext_t
```

Handle http mono body. Check out [body.c](test/body.c) for detail.

```c
TinyHttpParserMultiPartContext_t
```

Handle http [multi part](https://developer.mozilla.org/en-US/docs/Web/HTTP/Guides/MIME_types) body. Check out [multi-part.c](test/multi-part.c) for detail.

```c
TinyHttpParserChunkContext_t
```

Handle http [chunk](https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/Transfer-Encoding) body. Check out [chunk.c](test/chunk.c) for detail.

```c
TinyHttpParserWebsocketContext_t
```

Handle http [websocket](https://developer.mozilla.org/en-US/docs/Web/API/WebSocket). Check out [websocket-unix.cpp](test/websocket-unix.cpp) for detail.
