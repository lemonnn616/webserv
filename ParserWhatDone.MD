Реализована полная базовая HTTP-логика сервера, начиная от чтения данных из сокета и заканчивая отдачей статических файлов в браузер и curl.

Проект на текущий момент представляет собой неблокирующий HTTP/1.1 сервер, работающий поверх poll().

Общая архитектура (кратко)

CoreServer

принимает соединения

хранит клиентов

управляет таймаутами

передаёт данные в HTTP-обработчик

EventLoop

один поток

poll()

реагирует на:

POLLIN (чтение)

POLLOUT (запись)

HttpHandler

парсит HTTP-запрос

выполняет routing

формирует HTTP-ответ

Как работает HTTP-парсер
1. Буферизация входных данных

Каждый клиент имеет inBuffer (std::string), куда накапливаются байты из recv().

Парсер никогда не предполагает, что запрос пришёл целиком.

2. Определение конца HTTP-запроса

HTTP-запрос считается полным, когда найдено:

\r\n\r\n

size_t headersEnd = inBuffer.find("\r\n\r\n");


Если последовательность не найдена — сервер ждёт ещё данных, ничего не ломая.

3. Извлечение одного запроса

Из буфера вырезается ровно один HTTP-запрос:

std::string requestBlock = inBuffer.substr(0, headersEnd);
inBuffer.erase(0, headersEnd + 4);


Это позволяет:

корректно работать с keep-alive

обрабатывать несколько запросов подряд

4. Разбор request-line

Формат:

METHOD PATH HTTP/VERSION


Парсер:

ищет два пробела

извлекает method, path, version

при ошибке → 400 Bad Request

5. Разбор заголовков

Каждая строка вида:

Key: Value


Сохраняется в:

std::map<std::string, std::string> headers;


Пустая строка завершает блок заголовков.

Routing (маршрутизация)

Реализована простая, но корректная логика:

Поддерживаемые методы

GET

HEAD

Все остальные →

405 Method Not Allowed
Allow: GET, HEAD

Пути
Запрос	Результат
/	www/index.html
/index.html	файл
/notfound.html	404 Not Found
/../file	403 Forbidden

Реализована защита от path traversal (..).

Отдача статических файлов

Файлы читаются в бинарном режиме:

std::ifstream(file, std::ios::binary);


Поддерживаются Content-Type:

text/html

text/css

application/javascript

image/png

image/jpeg

fallback → application/octet-stream

Формирование HTTP-ответа

Ответ всегда корректен:

HTTP/1.1 <status> <reason>
Content-Length: <size>
Content-Type: <type>
Connection: close

<body>


Для HEAD:

тело не отправляется

заголовки отправляются

Проверка работоспособности

Сервер успешно протестирован через:

curl
curl http://localhost:8080/
curl -I http://localhost:8080/
curl -X POST http://localhost:8080/

nc (raw TCP)
printf "GET /\r\n\r\n" | nc localhost 8080

Браузер

HTML корректно отображается

соединение закрывается корректно

Текущий статус проекта

✔ HTTP request parsing
✔ Routing
✔ Static files
✔ MIME types
✔ GET / HEAD
✔ Status codes: 200 / 400 / 403 / 404 / 405
✔ Non-blocking poll-based server
✔ Защита от path traversal