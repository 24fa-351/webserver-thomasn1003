all: server

server: server.c
	gcc -Wall -pthread -o server server.c

clean:
	rm -f server
