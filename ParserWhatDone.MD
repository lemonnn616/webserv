# Webserv test commands (based on config/default.conf)

## 1) Build and run

```bash
make
./webserv config/default.conf
```

## 2) Active servers and routes

- Server 1: `localhost:8080`
- Server 2: `localhost:8081`
- `/` -> methods: `GET`
- `/cgi-bin` -> methods: `GET POST`
- `/post_body` -> methods: `POST`
- `/directory` -> methods: `GET HEAD POST`
- `/upload` -> methods: `POST`
- `/uploads` -> methods: `GET HEAD DELETE`

## 3) Basic checks

```bash
curl -i http://localhost:8080/
curl -i http://localhost:8081/
curl -I http://localhost:8080/uploads/
```

## 4) CGI checks

```bash
curl -i http://localhost:8080/cgi-bin/hello.py
curl -i "http://localhost:8080/cgi-bin/echo.py?x=1&y=2"
curl -i -X POST http://localhost:8080/cgi-bin/post.py -d "a=10&b=20"
```

## 5) Upload and delete checks

```bash
echo "hello upload" > /tmp/new.txt
curl -i -X POST http://localhost:8080/upload --data-binary @/tmp/new.txt
curl -i http://localhost:8080/uploads/
curl -i -X DELETE http://localhost:8080/uploads/new.txt
```

## 6) Error code checks

### 400 Bad Request

```bash
printf 'GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' | nc -N localhost 8080
```

### 403 Forbidden

```bash
echo "secret" > www/forbidden.txt
chmod 000 www/forbidden.txt
curl -i http://localhost:8080/forbidden.txt
chmod 644 www/forbidden.txt
rm -f www/forbidden.txt
```

### 404 Not Found

```bash
curl -i http://localhost:8080/no_such_file
```

### 405 Method Not Allowed

```bash
curl -i -X POST --data 'x' http://localhost:8080/
curl -i -X DELETE http://localhost:8080/cgi-bin/hello.py
```

### 413 Payload Too Large

```bash
truncate -s 2M /tmp/big
curl -i -H "Expect:" --data-binary @/tmp/big http://localhost:8080/upload
```

### 500 Internal Server Error

```bash
curl -i http://localhost:8080/errors/500.html
```

### 502 Bad Gateway (CGI failure)

```bash
curl -i http://localhost:8080/cgi-bin/bad.py
```

### 504 Gateway Timeout (CGI timeout)

```bash
curl -i http://localhost:8080/cgi-bin/hang.py
```
