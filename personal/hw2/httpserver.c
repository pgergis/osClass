#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>

#include "libhttp.h"
#include "wq.h"

/*
 * Global configuration variables.
 * You need to use these in your implementation of handle_files_request and
 * handle_proxy_request. Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
wq_t work_queue;
int num_threads;
int server_port;
char *server_files_directory;
char *server_proxy_hostname;
int server_proxy_port;


/*
 * Reads an HTTP request from stream (fd), and writes an HTTP response
 * containing:
 *
 *   1) If user requested an existing file, respond with the file
 *   2) If user requested a directory and index.html exists in the directory,
 *      send the index.html file.
 *   3) If user requested a directory and index.html doesn't exist, send a list
 *      of files in the directory with links to each.
 *   4) Send a 404 Not Found response.
 */

void handle_files_request(int fd) {

  /*
   * TODO: Your solution for Task 1 goes here! Feel free to delete/modify *
   * any existing code.
   */

    struct http_request *request = http_request_parse(fd);

    char data_path[strlen(server_files_directory) + strlen(request->path) + 1];
    data_path[0] = '\0';
    strcat(data_path, server_files_directory);
    strcat(data_path, request->path);

    char *requested_content;
    char *mime_type;
    DIR *requested_dir;
    FILE *requested_file;

    if((requested_dir = opendir(data_path)) != NULL) {
        struct dirent *dir;
        requested_file = tmpfile();
        mime_type = "text/html";
        char *back_link = "<a href=\"..\">Back</a>";
        fwrite(back_link, strlen(back_link), 1, requested_file);
        char *dir_head = "<center><h1>Files</h1><hr><br>";
        fwrite(dir_head, strlen(dir_head), 1, requested_file);
        int found_index = 0;
        while((dir = readdir(requested_dir)) != NULL) {
            char *file_name = dir->d_name;
            if(strcmp("index.html", file_name) == 0) {
                found_index = 1;
                char new_file[strlen(data_path) + strlen("/index.html") + 1];
                new_file[0] = '\0';
                strcat(new_file, data_path);
                strcat(new_file, "/index.html");
                requested_file = fopen(new_file, "rb");
                break;
            }
            if(strcmp(".", file_name) != 0 && strcmp("..", file_name) != 0) {
                char *linkstart = "<li><a href=\"";
                fwrite(linkstart, strlen(linkstart), 1, requested_file);
                fwrite(file_name, strlen(file_name), 1, requested_file);
                char *linkend = "\">";
                fwrite(linkend, strlen(linkend), 1, requested_file);
                fwrite(file_name, strlen(file_name), 1, requested_file);
                char *newline = "</a></li><br>";
                fwrite(newline, strlen(newline), 1, requested_file);
            }
        }
        if(!found_index) {
            char *dir_foot = "</ul><br>";
            fwrite(dir_foot, strlen(dir_foot), 1, requested_file);
        }
        closedir(requested_dir);
    } else {
        requested_file = fopen(data_path, "rb");
        mime_type = http_get_mime_type(data_path);
    }

    int file_size = 0;
    if(requested_file != NULL) {
        fseek(requested_file, 0, SEEK_END);
        file_size += ftell(requested_file);
        fseek(requested_file, 0, SEEK_SET);

        requested_content = malloc(file_size+1);
        requested_content[0] = '\0';
        if(requested_content){
            fread(requested_content, file_size, 1, requested_file);
        }

        fclose(requested_file);
    }
    if(requested_content != NULL) {
        http_start_response(fd, 200);
        http_send_header(fd, "Content-Type", mime_type);
        http_end_headers(fd);
        http_send_data(fd, requested_content, file_size);
        free(requested_content);
    } else {
        http_start_response(fd, 404);
        http_send_header(fd, "Content-Type", "text/html");
        http_end_headers(fd);
        http_send_string(fd,
                        "<center>"
                        "<h1>404</h1>"
                        "<hr>"
                        "<p>File not found :(</p>"
                        "</center>");
    }
}

/*
 * Opens a connection to the proxy target (hostname=server_proxy_hostname and
 * port=server_proxy_port) and relays traffic to/from the stream fd and the
 * proxy target. HTTP requests from the client (fd) should be sent to the
 * proxy target, and HTTP responses from the proxy target should be sent to
 * the client (fd).
 *
 *   +--------+     +------------+     +--------------+
 *   | client | <-> | httpserver | <-> | proxy target |
 *   +--------+     +------------+     +--------------+
 */
void handle_proxy_request(int fd) {

  /*
  * The code below does a DNS lookup of server_proxy_hostname and
  * opens a connection to it. Please do not modify.
  */

  struct sockaddr_in target_address;
  memset(&target_address, 0, sizeof(target_address));
  target_address.sin_family = AF_INET;
  target_address.sin_port = htons(server_proxy_port);

  struct hostent *target_dns_entry = gethostbyname2(server_proxy_hostname, AF_INET);

  int client_socket_fd = socket(PF_INET, SOCK_STREAM, 0);
  if (client_socket_fd == -1) {
    fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
    exit(errno);
  }

  if (target_dns_entry == NULL) {
    fprintf(stderr, "Cannot find host: %s\n", server_proxy_hostname);
    exit(ENXIO);
  }

  char *dns_address = target_dns_entry->h_addr_list[0];

  memcpy(&target_address.sin_addr, dns_address, sizeof(target_address.sin_addr));
  int connection_status = connect(client_socket_fd, (struct sockaddr*) &target_address,
      sizeof(target_address));

  if (connection_status < 0) {
    /* Dummy request parsing, just to be compliant. */
    http_request_parse(fd);

    http_start_response(fd, 502);
    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);
    http_send_string(fd, "<center><h1>502 Bad Gateway</h1><hr></center>");
    return;

  }

  /*
  * TODO: Your solution for task 3 belongs here!
  */
}


void init_thread_pool(int num_threads, void (*request_handler)(int)) {
  /*
   * TODO: Part of your solution for Task 2 goes here!
   */
}

/*
 * Opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void serve_forever(int *socket_number, void (*request_handler)(int)) {

  struct sockaddr_in server_address, client_address;
  size_t client_address_length = sizeof(client_address);
  int client_socket_number;

  *socket_number = socket(PF_INET, SOCK_STREAM, 0);
  if (*socket_number == -1) {
    perror("Failed to create a new socket");
    exit(errno);
  }

  int socket_option = 1;
  if (setsockopt(*socket_number, SOL_SOCKET, SO_REUSEADDR, &socket_option,
        sizeof(socket_option)) == -1) {
    perror("Failed to set socket options");
    exit(errno);
  }

  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(server_port);

  if (bind(*socket_number, (struct sockaddr *) &server_address,
        sizeof(server_address)) == -1) {
    perror("Failed to bind on socket");
    exit(errno);
  }

  if (listen(*socket_number, 1024) == -1) {
    perror("Failed to listen on socket");
    exit(errno);
  }

  printf("Listening on port %d...\n", server_port);

  init_thread_pool(num_threads, request_handler);

  while (1) {
    client_socket_number = accept(*socket_number,
        (struct sockaddr *) &client_address,
        (socklen_t *) &client_address_length);
    if (client_socket_number < 0) {
      perror("Error accepting socket");
      continue;
    }

    printf("Accepted connection from %s on port %d\n",
        inet_ntoa(client_address.sin_addr),
        client_address.sin_port);

    // TODO: Change me?
    request_handler(client_socket_number);
    close(client_socket_number);

    printf("Accepted connection from %s on port %d\n",
        inet_ntoa(client_address.sin_addr),
        client_address.sin_port);
  }

  shutdown(*socket_number, SHUT_RDWR);
  close(*socket_number);
}

int server_fd;
void signal_callback_handler(int signum) {
  printf("Caught signal %d: %s\n", signum, strsignal(signum));
  printf("Closing socket %d\n", server_fd);
  if (close(server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
  exit(0);
}

char *USAGE =
  "Usage: ./httpserver --files www_directory/ --port 8000 [--num-threads 5]\n"
  "       ./httpserver --proxy inst.eecs.berkeley.edu:80 --port 8000 [--num-threads 5]\n";

void exit_with_usage() {
  fprintf(stderr, "%s", USAGE);
  exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
  signal(SIGINT, signal_callback_handler);

  /* Default settings */
  server_port = 8000;
  void (*request_handler)(int) = NULL;

  int i;
  for (i = 1; i < argc; i++) {
    if (strcmp("--files", argv[i]) == 0) {
      request_handler = handle_files_request;
      free(server_files_directory);
      server_files_directory = argv[++i];
      if (!server_files_directory) {
        fprintf(stderr, "Expected argument after --files\n");
        exit_with_usage();
      }
    } else if (strcmp("--proxy", argv[i]) == 0) {
      request_handler = handle_proxy_request;

      char *proxy_target = argv[++i];
      if (!proxy_target) {
        fprintf(stderr, "Expected argument after --proxy\n");
        exit_with_usage();
      }

      char *colon_pointer = strchr(proxy_target, ':');
      if (colon_pointer != NULL) {
        *colon_pointer = '\0';
        server_proxy_hostname = proxy_target;
        server_proxy_port = atoi(colon_pointer + 1);
      } else {
        server_proxy_hostname = proxy_target;
        server_proxy_port = 80;
      }
    } else if (strcmp("--port", argv[i]) == 0) {
      char *server_port_string = argv[++i];
      if (!server_port_string) {
        fprintf(stderr, "Expected argument after --port\n");
        exit_with_usage();
      }
      server_port = atoi(server_port_string);
    } else if (strcmp("--num-threads", argv[i]) == 0) {
      char *num_threads_str = argv[++i];
      if (!num_threads_str || (num_threads = atoi(num_threads_str)) < 1) {
        fprintf(stderr, "Expected positive integer after --num-threads\n");
        exit_with_usage();
      }
    } else if (strcmp("--help", argv[i]) == 0) {
      exit_with_usage();
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
      exit_with_usage();
    }
  }

  if (server_files_directory == NULL && server_proxy_hostname == NULL) {
    fprintf(stderr, "Please specify either \"--files [DIRECTORY]\" or \n"
                    "                      \"--proxy [HOSTNAME:PORT]\"\n");
    exit_with_usage();
  }

  serve_forever(&server_fd, request_handler);

  return EXIT_SUCCESS;
}
