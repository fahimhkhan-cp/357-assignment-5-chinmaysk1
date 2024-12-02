#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>

#define DEFAULT_PORT 2828

#define MIN_ARGS 2
#define MAX_ARGS 3
#define SERVER_ARG_IDX 1
#define PORT_ARG_IDX 2

#define USAGE_STRING "usage: %s <server address> [port]\n"

void validate_arguments(int argc, char *argv[])
{
   if (argc < MIN_ARGS || argc > MAX_ARGS)
   {
      fprintf(stderr, USAGE_STRING, argv[0]);
      exit(EXIT_FAILURE);
   }
}

int validate_port(const char *port_str)
{
   char *endptr;
   long port = strtol(port_str, &endptr, 10);

   if (*endptr != '\0' || port < 1024 || port > 65535)
   {
      fprintf(stderr, "Invalid port number: %s\n", port_str);
      exit(EXIT_FAILURE);
   }

   return (int)port;
}

void send_request(int fd)
{
   char *line = NULL;
   size_t size = 0;
   ssize_t num;
   char buffer[1024];

   while ((num = getline(&line, &size, stdin)) >= 0)
   {
      write(fd, line, num);                                      // Send data to the server
      ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1); // Read the echoed data from the server
      if (bytes_read > 0)
      {
         buffer[bytes_read] = '\0'; // Null-terminate the received data
         printf("%s", buffer);      // Print the echoed data
      }
   }

   free(line);
}

int connect_to_server(struct hostent *host_entry, int port)
{
   int fd;
   struct sockaddr_in their_addr;

   if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
   {
      perror("socket");
      return -1;
   }

   their_addr.sin_family = AF_INET;
   their_addr.sin_port = htons(port);
   their_addr.sin_addr = *((struct in_addr *)host_entry->h_addr);

   if (connect(fd, (struct sockaddr *)&their_addr, sizeof(struct sockaddr)) == -1)
   {
      perror("connect");
      close(fd);
      return -1;
   }

   return fd;
}

struct hostent *gethost(char *hostname)
{
   struct hostent *he;

   if ((he = gethostbyname(hostname)) == NULL)
   {
      herror("gethostbyname");
   }

   return he;
}

int main(int argc, char *argv[])
{
   validate_arguments(argc, argv);

   int port = DEFAULT_PORT; // Default port
   if (argc == MAX_ARGS)
   {
      port = validate_port(argv[PORT_ARG_IDX]);
   }

   struct hostent *host_entry = gethost(argv[SERVER_ARG_IDX]);

   if (host_entry)
   {
      int fd = connect_to_server(host_entry, port);
      if (fd != -1)
      {
         send_request(fd);
         close(fd);
      }
   }

   return 0;
}
