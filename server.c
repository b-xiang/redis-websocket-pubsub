#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include <event.h>
#include <event2/event.h>

#include "lexer.h"
#include "http.h"
#include "websocket.h"


struct client_connection {
  // The file descriptor of the socket.
  int fd;
  // Whether or not the socket has been shutdown.
  bool is_shutdown;
  // The address of the client.
  struct sockaddr_in addr;
  // The libevent bufferevent for the socket.
  struct bufferevent *bev;
  // The libevent output buffer.
  struct evbuffer *buf_out;
  // If the client has established a websocket connection, this points to the websocket state.
  struct websocket *ws;
  // Linked list.
  struct client_connection *next;
};

static struct client_connection *client_connection_create(int fd, const struct sockaddr_in *addr);
static void                      client_connection_destroy(struct client_connection *target);
static void                      client_connection_destroy_all(void);


static const struct option ARGV_OPTIONS[] = {
  {"bind_host", required_argument, NULL, 'h'},
  {"bind_port", required_argument, NULL, 'p'},
  {"redis_host", required_argument, NULL, 'H'},
  {"redis_port", required_argument, NULL, 'P'},
  {NULL, 0, NULL, 0},
};

static const char *bind_host = "0.0.0.0";
static uint16_t bind_port = 9999;
static const char *redis_host = "127.0.0.1";
static uint16_t redis_port = 6379;

// Static variable so it can be accessed by the signal handler.
static struct event_base *server_loop = NULL;

static struct client_connection *clients = NULL;


// ================================================================================================
// Command-line argument parsing.
// ================================================================================================
static void
print_usage(FILE *const f) {
  fprintf(f, "Usage:\n");
  for (size_t i = 0; ; ++i) {
    const struct option *opt = &ARGV_OPTIONS[i];
    if (opt->name == NULL) {
      break;
    }
    fprintf(f, "  -%c --%s", (char)opt->val, opt->name);
    if (opt->has_arg) {
      fprintf(f, " arg");
    }
    fprintf(f, "\n");
  }
}


static bool
parse_argv(int argc, char *const *argv) {
  int index, c, tmp;
  while (true) {
    c = getopt_long(argc, argv, "h:p:H:P:", ARGV_OPTIONS, &index);
    switch (c) {
    case -1:  // Finished processing.
      return true;
    case 'h':
      bind_host = optarg;
      break;
    case 'p':
      tmp = atoi(optarg);
      if (tmp < 0 || tmp > UINT16_MAX) {
        fprintf(stderr, "Invalid bind port number %d. Not in the range [0, %u]\n", tmp, UINT16_MAX);
        print_usage(stderr);
        return false;
      }
      bind_port = (uint16_t)tmp;
      break;
    case 'H':
      redis_host = optarg;
      break;
    case 'P':
      tmp = atoi(optarg);
      if (tmp < 0 || tmp > UINT16_MAX) {
        fprintf(stderr, "Invalid redis port number %d. Not in the range [0, %u]\n", tmp, UINT16_MAX);
        print_usage(stderr);
        return false;
      }
      redis_port = (uint16_t)tmp;
      break;
    case '?':  // Unknown option.
      print_usage(stderr);
      return false;
    default:
      print_usage(stderr);
      return false;
    }
  }
}


// ================================================================================================
// Signal handling.
// ================================================================================================
static void
sighandler(const int signal) {
  fprintf(stderr, "[INFO] Received signal %d. Shutting down...\n", signal);
  if (event_base_loopexit(server_loop, NULL) == -1) {
    fprintf(stderr, "[ERROR] Error shutting down server\n");
  }
}


// ================================================================================================
// Utils.
// ================================================================================================
static int
set_nonblocking(const int fd) {
  int flags;

  flags = fcntl(fd, F_GETFL);
  if (flags == -1) {
    return -1;
  }
  flags |= O_NONBLOCK;
  if (fcntl(fd, F_SETFL, flags) == -1) {
    return -1;
  }

  return 0;
}


// ================================================================================================
// Listening socket's libevent callbacks.
// ================================================================================================
static void
client_connection_error(struct bufferevent *const bev, const short events, void *const arg) {
  (void)bev;
  struct client_connection *const client = arg;

  if (events & EVBUFFER_EOF) {
    fprintf(stderr, "[INFO] Remote host disconnected on fd=%d\n", client->fd);
    client->is_shutdown = true;
  }
  else if (events & EVBUFFER_TIMEOUT) {
    fprintf(stderr, "[INFO] Remote host timed out on fd=%d\n", client->fd);
  }
  else {
    fprintf(stderr, "[INFO] Remote host experienced an unknown error (0x%08x) on fd=%d\n", events, client->fd);
  }
  client_connection_destroy(client);
}


static void
client_connection_read_initial(struct client_connection *const client, const char *const buf, const size_t nbytes) {
  struct lexer lex;
  struct http_request *req = NULL;
  struct websocket *ws = NULL;
  enum status status;
  bool accepted = false;

  // Initialise our required data structures.
  if (!lexer_init(&lex, &buf[0], &buf[0] + nbytes)) {
    fprintf(stderr, "[ERROR] failed to construct lexer instance (`lexer_init` failed)\n");
    return;
  }
  if ((req = http_request_init()) == NULL) {
    fprintf(stderr, "[ERROR] failed to construct http_request instance\n");
    lexer_destroy(&lex);
    return;
  }
  if ((ws = websocket_init()) == NULL) {
    fprintf(stderr, "[ERROR] failed to construct websocket instance\n");
    http_request_destroy(req);
    lexer_destroy(&lex);
    return;
  }

  // Try and parse the HTTP request.
  if ((status = http_request_parse(req, &lex)) != STATUS_OK) {
    fprintf(stderr, "[ERROR] failed to parse the HTTP request. status=%d\n", status);
    websocket_destroy(ws);
    http_request_destroy(req);
    lexer_destroy(&lex);
    return;
  }

  // TODO ensure that the host matches what we think we're serving.
  fprintf(stdout, "Request is for host '%s'\n", req->host);

  // See if the HTTP request is accepted by the websocket protocol.
  status = websocket_accept_http_request(req, client->buf_out, &accepted);
  if (status != STATUS_OK) {
    fprintf(stderr, "[ERROR] websocket_accept_http_request failed. status=%d\n", status);
  }
  else {
    // If we accepted the connection, keep the websocket struct around.
    fprintf(stderr, "[INFO] accepted? %d\n", accepted);
    if (accepted) {
      client->ws = ws;
    }
  }

  // Flush the output buffer.
  if (bufferevent_write_buffer(client->bev, client->buf_out) == -1) {
    fprintf(stderr, "[ERROR] failed to flush output buffer (`bufferevent_write_buffer` failed)\n");
  }

  // Free up resources.
  if (!accepted) {
    if ((status = websocket_destroy(ws)) != STATUS_OK) {
      fprintf(stderr, "[ERROR] failed to destroy websocket instance. status=%d\n", status);
    }
  }
  if ((status = http_request_destroy(req)) != STATUS_OK) {
    fprintf(stderr, "[ERROR] failed to destroy HTTP request instance. status=%d\n", status);
  }
  if (!lexer_destroy(&lex)) {
    fprintf(stderr, "[ERROR] failed to destroy lexer instance (`lexer_destroy` failed)\n");
  }
  return;
}


static void
client_connection_read_websocket(struct client_connection *const client, const char *const buf, const size_t nbytes) {
  (void)client;
  // TODO send the data to be processed by the websocket logic.
  fprintf(stderr, "[DEBUG] data for websocket logic:");
  for (size_t i = 0; i != nbytes; ++i) {
    if (isprint(buf[i]) && !isspace(buf[i])) {
      fprintf(stderr, "  %c", buf[i]);
    }
    else {
      fprintf(stderr, " %02x", (unsigned int)buf[i]);
    }
  }
  fprintf(stderr, "\n");
}


static void
client_connection_read(struct bufferevent *const bev, void *const arg) {
  (void)bev;
  struct client_connection *const client = arg;
  char buf[4096];

  // Read the data.
  const size_t nbytes = bufferevent_read(bev, buf, sizeof(buf));
  fprintf(stderr, "[DEBUG] bufferevent_read read in %zu bytes from fd=%d\n", nbytes, client->fd);
  if (nbytes == 0) {
    return;
  }

  // If the client hasn't tried to establish a websocket connection yet, this must be the inital
  // HTTP `Upgrade` request.
  if (client->ws == NULL) {
    client_connection_read_initial(client, buf, nbytes);
    // If we failed to process the HTTP request as a websocket establishing connection, drop the client.
    if (client->ws == NULL) {
      client_connection_destroy(client);
    }
  }
  else {
    client_connection_read_websocket(client, buf, nbytes);
  }
}


static struct client_connection *
client_connection_create(const int fd, const struct sockaddr_in *const addr) {
  struct client_connection *const client = malloc(sizeof(struct client_connection));
  if (client == NULL) {
    fprintf(stderr, "[ERROR] failed to malloc: %s\n", strerror(errno));
    return NULL;
  }

  // Construct the client_connection instance and insert it into the list of all clients.
  memset(client, 0, sizeof(struct client_connection));
  client->fd = fd;
  client->is_shutdown = false;
  client->addr = *addr;
  client->bev = bufferevent_new(fd, &client_connection_read, NULL, &client_connection_error, client);
  client->buf_out = evbuffer_new();
  client->ws = NULL;
  client->next = clients;
  clients = client;

  // Configure the buffered I/O event.
  if (bufferevent_base_set(server_loop, client->bev) == -1) {
    fprintf(stderr, "[ERROR] `bufferevent_base_set` failed on fd=%d\n", client->fd);
    client_connection_destroy(client);
    return NULL;
  }
  bufferevent_settimeout(client->bev, 60, 0);
  if (bufferevent_enable(client->bev, EV_READ) == -1) {
    fprintf(stderr, "[ERROR] failed to enable the client read bufferevent (`bufferevent_enable` failed) on fd=%d\n", client->fd);
    client_connection_destroy(client);
    return NULL;
  }

  // Configure the output buffer.
  if (client->buf_out == NULL) {
    client_connection_destroy(client);
    return NULL;
  }

  return client;
}


static void
_client_connection_destroy(struct client_connection *const client) {
  int ret;
  enum status status;
  if (client->buf_out != NULL) {
    evbuffer_free(client->buf_out);
  }
  if (client->bev != NULL) {
    bufferevent_free(client->bev);
  }
  if (client->ws != NULL) {
    if ((status = websocket_destroy(client->ws)) != STATUS_OK) {
      fprintf(stderr, "[WARNING] `websocket_destroy` failed with status=%d\n", status);
    }
  }
  if (!client->is_shutdown) {
    ret = shutdown(client->fd, SHUT_RDWR);
    if (ret != 0) {
      fprintf(stderr, "[WARNING] `shutdown` on fd=%d failed: %s\n", client->fd, strerror(errno));
    }
  }
  if (client->fd >= 0) {
    ret = close(client->fd);
    fprintf(stderr, "[WARNING] `close` on fd=%d failed: %s\n", client->fd, strerror(errno));
  }
  free(client);
}


static void
client_connection_destroy(struct client_connection *const target) {
  struct client_connection *client, *prev = NULL;
  for (client = clients; client != NULL; client = client->next) {
    if (client != target) {
      // Update the linked list.
      if (prev == NULL) {
        clients = client->next;
      }
      else {
        prev->next = client->next;
      }

      // Free up the client's resources.
      _client_connection_destroy(client);
      return;
    }
    prev = client;
  }
}


static void
client_connection_destroy_all(void) {
  struct client_connection *client, *next;
  for (client = clients; client != NULL; ) {
    next = client->next;
    _client_connection_destroy(client);
    client = next;
  }
}


static void
setup_connection(const int fd, const struct sockaddr_in *const addr) {
  struct client_connection *client;

  if (set_nonblocking(fd) != 0) {
    fprintf(stderr, "[ERROR] failed to set fd=%d to non-blocking: %s\n", fd, strerror(errno));
    return;
  }

  client = client_connection_create(fd, addr);
  if (client == NULL) {
    fprintf(stderr, "[ERROR] failed to create client connection object\n");
    return;
  }
}


static void
listen_socket_callback(const int listen_fd, const short events, void *const arg) {
  fprintf(stderr, "[DEBUG] [listen_socket_callback] %d %d %p\n", listen_fd, events, arg);

  // Ensure we have a read event.
  if (!(events & EV_READ)) {
    fprintf(stderr, "[ERROR] [listen_socket_callback] Received unexpected event %u\n", events);
    return;
  }

  // Accept (in bulk) client connections.
  for (unsigned int i = 0; i != 10; ++i) {
    // Accept the child connection.
    struct sockaddr_in in_addr;
    socklen_t in_addr_nbytes = sizeof(in_addr);
    const int in_fd = accept(listen_fd, (struct sockaddr *)&in_addr, &in_addr_nbytes);
    if (in_fd == -1) {
      if (errno != EWOULDBLOCK && errno != EAGAIN) {
        perror("Failed to read from main listening socket");
      }
      break;
    }

    fprintf(stderr, "[INFO] [listen_socket_callback] Accepted child connection on fd %d\n", in_fd);
    setup_connection(in_fd, &in_addr);
  }
}


// ================================================================================================
// main.
// ================================================================================================
int
main(int argc, char **argv) {
  struct event connect_event;
  int listen_fd;
  sigset_t sigset;
  struct sigaction siginfo;

  // Parse argv.
  if (!parse_argv(argc, argv)) {
    return 1;
  }

  // Ignore SIGPIPE and die gracefully on SIGINT and SIGTERM.
  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
    perror("call to signal() failed");
    return 1;
  }
  sigemptyset(&sigset);
  siginfo.sa_handler = sighandler;
  siginfo.sa_mask = sigset;
  siginfo.sa_flags = SA_RESTART;
  if (sigaction(SIGINT, &siginfo, NULL) == -1) {
    perror("sigaction(SIGINT) failed");
    return 1;
  }
  if (sigaction(SIGTERM, &siginfo, NULL) == -1) {
    perror("sigaction(SIGTERM) failed");
    return 1;
  }

  // Create a libevent base object.
  fprintf(stderr, "[INFO] libevent version: %s\n", event_get_version());
  server_loop = event_base_new();
  if (server_loop == NULL) {
    fprintf(stderr, "[ERROR] Failed to create libevent event loop\n");
    return 1;
  }
  fprintf(stderr, "[INFO] libevent is using %s for events.\n", event_base_get_method(server_loop));

  // Create the input socket address structure.
  struct sockaddr_in bind_addr;
  memset(&bind_addr, 0, sizeof(struct sockaddr_in));
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_port = htons(bind_port);
  if (inet_pton(AF_INET, bind_host, &bind_addr.sin_addr.s_addr) != 1) {
    perror("Failed to convert bind host to an IPv4 address");
    return 1;
  }

  // Create a socket connection to listen on.
  listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd == -1) {
    perror("Failed to create a socket");
    return 1;
  }
  {
    int tmp = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &tmp, sizeof(tmp)) == -1) {
      perror("Failed to enable socket address reuse on listening socket");
      return 1;
    }
  }
  if (bind(listen_fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) == -1) {
    perror("Failed to bind the listening socket to the address");
    return 1;
  }

  // Set the connection queue size.
  if (listen(listen_fd, 8) == -1) {
    perror("Failed to create a socket listening backlog queue");
    return 1;
  }

  // Set the socket to be non-blocking.
  if (set_nonblocking(listen_fd) == -1) {
    perror("Failed to set the listening socket to be non-blocking");
    return 1;
  }

  // Bind callbacks to the libevent loop for activity on the listening socket's file descriptor.
  event_set(&connect_event, listen_fd, EV_READ | EV_PERSIST, listen_socket_callback, NULL);
  event_base_set(server_loop, &connect_event);
  if (event_add(&connect_event, NULL) == -1) {
    fprintf(stderr, "[ERROR] Failed to schedule a socket listen into the main event loop\n");
    return 1;
  }

  // Run the libevent event loop.
  fprintf(stderr, "[INFO] Starting libevent event loop, listening on %s:%u\n", bind_host, bind_port);
  if (event_base_dispatch(server_loop) == -1) {
    fprintf(stderr, "[ERROR] Failed to run libevent event loop\n");
  }

  // Free up the connections.
  client_connection_destroy_all();

  // Free up the libevent event loop.
  event_base_free(server_loop);

  // Close the main socket.
  if (close(listen_fd) == -1) {
    perror("close(listen_fd) failed");
  }

  return 0;
}
