CC = gcc
CFLAGS = -Wall -std=c99 -pedantic
SERVER = httpd
SERVER_OBJS = httpd.o net.o

all: $(SERVER)

$(SERVER): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $(SERVER) $(SERVER_OBJS)

httpd.o: httpd.c
	$(CC) $(CFLAGS) -c httpd.c

clean:
	rm -f $(SERVER) *.o
