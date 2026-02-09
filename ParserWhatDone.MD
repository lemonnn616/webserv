400:
printf 'GET / HTTP/1.1\r\n\r\n' | nc -N localhost 8080

403:
echo "secret" > www/forbidden.txt
chmod 000 www/forbidden.txt
curl -i http://localhost:8080/forbidden.txt
chmod 644 www/forbidden.txt
rm -f www/forbidden.txt

404:
curl -i http://localhost:8080/no_such_file

405:
curl -i -X POST --data 'x' http://localhost:8080/

413:
truncate -s 2M /tmp/big
curl -i -H "Expect:" --data-binary @/tmp/big http://localhost:8080/upload

500:
curl -i http://localhost:8080/errors/500.html

502:
curl -i http://localhost:8080/cgi-bin/bad.py

504:
curl -i http://localhost:8080/cgi-bin/hang.py
