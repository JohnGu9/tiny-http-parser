# Intro

Pure C implement http parser.

No std/system required.

Design for async programming and handle TCP stick package problem.

Only support CRLF.

# Requirement

`string.h` (for `size_t`/`memcmp()`/`memcpy()`/`memset()`)
`math.h` (for `pow()`)

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
