*This project has been created as part of the 42 curriculum by iriadyns and osivkov.*

# webserv

## Description

webserv is a custom HTTP/1.1 server written in C++17. It accepts client connections, parses incoming HTTP requests, routes them to static content or CGI handlers, and serializes HTTP responses back to the client.

The goal of the project is to implement a small but functional web server that can:

- parse requests correctly,
- serve static files,
- handle redirects and directory listing,
- support uploads and DELETE,
- run CGI scripts,
- and apply configuration rules from a custom config file.

## Instructions

### Compilation

Use the provided Makefile:

```bash
make
```

Useful clean targets:

```bash
make clean
make fclean
make re
```

### Execution

By default, the server reads `config/default.conf`:

```bash
./webserv
```

You can also pass a specific configuration file:

```bash
./webserv config/default.conf
```

### Example usage

The default configuration includes two listening ports and several routes for testing static files, CGI, uploads, redirects, and error pages.

Examples:

```bash
curl -i http://localhost:8080/
curl -i http://localhost:8080/cgi-bin/hello.py
curl -I http://localhost:8080/uploads/
curl -i -X POST http://localhost:8080/upload --data-binary @file.txt
```

See [config/default.conf](config/default.conf) for the exact configuration syntax used by the parser.

## Features

- HTTP request parsing with support for request line, headers, query string, percent-decoding, chunked transfer encoding, and Content-Length bodies.
- HTTP response generation with status codes, headers, custom error pages, and serialization.
- Static file serving.
- Directory handling with redirects, index files, and autoindex generation.
- File upload support.
- DELETE support for removing files.
- CGI execution for configured extensions.
- Multiple server blocks and location-based routing.
- HTTP/1.0 and HTTP/1.1 connection handling.

## Project Structure

- `src/core` - server loop, client handling, connection state, CGI lifecycle.
- `src/http` - HTTP parsing, routing, response serialization, error handling, CGI response parsing.
- `src/cgi` - CGI process spawning and pipe management.
- `config` - configuration lexer, parser, normalization, and config data structures.
- `src/utils` - filesystem and string helpers.
- `www` - sample website content, error pages, and CGI scripts.

## Resources

 References used for the topic:

- RFC 9110: HTTP Semantics
- RFC 9112: HTTP/1.1
- MDN Web Docs: HTTP overview, request methods, response status codes, headers
- Curl documentation for request testing
- POSIX documentation for sockets, `fork`, `execve`, `pipe`, and file descriptors

AI usage:

- AI was used to draft and format this README.
- AI was used to suggest practical request examples and organize the documentation structure.
- All technical claims in this file were verified against the repository source code and configuration files.

## Notes

- The project is compiled with `-std=c++17 -Wall -Wextra -Werror`.
- The server expects a valid configuration file and static content under the configured document root.