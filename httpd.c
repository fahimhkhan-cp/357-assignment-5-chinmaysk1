#define _GNU_SOURCE
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

void handle_cgi_request(int nfd, char *filename);

void handle_signal(int signo)
{
   while (waitpid(-1, NULL, WNOHANG) > 0)
      ;
}

void send_error(int fd, int code, const char *message)
{
   dprintf(fd,
           "HTTP/1.0 %d %s\r\n"
           "Content-Type: text/html\r\n"
           "Content-Length: %zu\r\n"
           "\r\n"
           "<html><body><h1>%d %s</h1></body></html>\r\n",
           code, message, strlen(message) + 20, code, message);
}

void send_file(int fd, const char *path, int send_contents)
{
   struct stat file_stat;

   // Get file data
   if (stat(path, &file_stat) == -1)
   {
      send_error(fd, 404, "Not Found");
      return;
   }

   // Check permissions and if the file is regular
   if (!S_ISREG(file_stat.st_mode))
   {
      send_error(fd, 403, "Permission Denied");
      return;
   }

   // Check read permissions
   if ((file_stat.st_mode & S_IROTH) == 0)
   {
      send_error(fd, 403, "Permission Denied");
      return;
   }

   // Try to open the file
   int file_fd = open(path, O_RDONLY);
   if (file_fd == -1)
   {
      if (errno == EACCES) // Permission denied
         send_error(fd, 403, "Permission Denied");
      else
         send_error(fd, 500, "Internal Server Error");
      return;
   }

   // Send HTTP header
   dprintf(fd,
           "HTTP/1.0 200 OK\r\n"
           "Content-Type: text/html\r\n"
           "Content-Length: %ld\r\n"
           "\r\n",
           file_stat.st_size);

   // Send file contents if requested
   if (send_contents)
   {
      char buffer[1024];
      ssize_t bytes;
      while ((bytes = read(file_fd, buffer, sizeof(buffer))) > 0)
      {
         write(fd, buffer, bytes);
      }
   }

   close(file_fd);
}

int parse_request(FILE *network, char *type, char *filename, char *http_version)
{
   char *line = NULL;
   size_t size = 0;
   if (getline(&line, &size, network) <= 0)
   {
      free(line);
      return -1;
   }

   int matched = sscanf(line, "%15s %255s %15s", type, filename, http_version);
   free(line);

   return (matched == 3 && strlen(filename) > 0) ? 0 : -1;
}

void handle_request(int nfd)
{
   FILE *network = fdopen(nfd, "r+");
   if (!network)
   {
      perror("fdopen");
      close(nfd);
      return;
   }

   char type[16], filename[256], http_version[16];

   if (parse_request(network, type, filename, http_version) == -1)
   {
      send_error(nfd, 400, "Bad Request");
      fclose(network);
      return;
   }

   printf("Received request: %s %s %s\n", type, filename, http_version);

   // Parse input
   if (strstr(filename, "..") || strstr(filename, "~"))
   {
      send_error(nfd, 403, "Permission Denied");
   }
   else if (strncmp(filename, "/cgi-like/", 10) == 0) // Check if it's a CGI-like request
   {
      handle_cgi_request(nfd, filename);
   }
   else if (strcmp(type, "HEAD") == 0)
   {
      send_file(nfd, filename + 1, 0); // Send only the header
   }
   else if (strcmp(type, "GET") == 0)
   {
      send_file(nfd, filename + 1, 1); // Send the file contents
   }
   else
   {
      send_error(nfd, 501, "Not Implemented");
   }

   fclose(network);
}

void handle_cgi_request(int nfd, char *filename)
{
   char *program = filename + 10;         // Skip /cgi-like/
   char *args_str = strchr(program, '?'); // Check if arguments exist
   char *args[256];
   int arg_count = 0;

   if (args_str)
   {
      *args_str = '\0';
      args_str++; // Skip the '?'

      // Split arguments by '&'
      char *arg = strtok(args_str, "&");
      while (arg != NULL)
      {
         args[arg_count++] = arg;
         arg = strtok(NULL, "&");
      }
   }
   args[arg_count] = NULL;

   // Fork to execute the program
   pid_t pid = fork();
   if (pid == -1)
   {
      send_error(nfd, 500, "Internal Server Error");
      return;
   }
   else if (pid == 0)
   {
      // Child process
      char temp_filename[256];
      snprintf(temp_filename, sizeof(temp_filename), "/tmp/cgi_output_%d.txt", getpid());
      int temp_fd = open(temp_filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);

      if (temp_fd == -1)
      {
         perror("Error opening temp file");
         exit(1);
      }

      // Redirect stdout to temp file
      if (dup2(temp_fd, STDOUT_FILENO) == -1)
      {
         perror("Error redirecting stdout");
         exit(1);
      }
      close(temp_fd);

      // Execute
      if (execvp(program, args) == -1)
      {
         perror("Error executing program");
         exit(1);
      }
   }
   else
   {
      // Parent process
      int status;
      waitpid(pid, &status, 0);

      // Read output from temp file and send
      char temp_filename[256];
      snprintf(temp_filename, sizeof(temp_filename), "/tmp/cgi_output_%d.txt", pid);

      struct stat file_stat;
      if (stat(temp_filename, &file_stat) == -1)
      {
         send_error(nfd, 500, "Internal Server Error");
         return;
      }

      // Send HTTP header
      dprintf(nfd,
              "HTTP/1.0 200 OK\r\n"
              "Content-Type: text/plain\r\n"
              "Content-Length: %ld\r\n"
              "\r\n",
              file_stat.st_size);

      // Send contents of the file
      int file_fd = open(temp_filename, O_RDONLY);
      if (file_fd == -1)
      {
         send_error(nfd, 500, "Internal Server Error");
         return;
      }

      char buffer[1024];
      ssize_t bytes;
      while ((bytes = read(file_fd, buffer, sizeof(buffer))) > 0)
      {
         write(nfd, buffer, bytes);
      }

      close(file_fd);
      remove(temp_filename); // Remove temp file
   }
}

void run_service(int fd)
{
   struct sigaction sa;
   sa.sa_handler = handle_signal;
   sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
   sigaction(SIGCHLD, &sa, NULL);

   while (1)
   {
      int nfd = accept_connection(fd);
      if (nfd != -1)
      {
         if (fork() == 0)
         {
            close(fd);
            handle_request(nfd);
            close(nfd);
            exit(0);
         }
         close(nfd);
      }
   }
}

int main(int argc, char *argv[])
{
   if (argc != 2)
   {
      fprintf(stderr, "Usage: %s <port>\n", argv[0]);
      exit(1);
   }

   int port = atoi(argv[1]);
   int fd = create_service(port);

   if (fd == -1)
   {
      perror("Failed to create service");
      exit(1);
   }

   printf("Listening on port: %d\n", port);
   run_service(fd);
   close(fd);

   return 0;
}
