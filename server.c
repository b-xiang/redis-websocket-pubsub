#include <arpa/inet.h>
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
#include <unistd.h>

#include <event.h>
#include <event2/event.h>

#include "lexer.h"
#include "logging.h"
#include "http.h"
#include "websocket.h"

#ifndef SA_RESTART
#define SA_RESTART   0x10000000 /* Restart syscall on signal return.  */
#endif

struct client_connection {
  struct websocket *ws;
  struct client_connection *next;
  bool needs_shutdown;
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
  INFO("sighandler", "Received signal %d. Shutting down...\n", signal);
  if (event_base_loopexit(server_loop, NULL) == -1) {
    ERROR0("sighandler", "Error shutting down server\n");
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
client_connection_onerror(struct bufferevent *const bev, const short events, void *const arg) {
  (void)bev;
  struct client_connection *const client = (struct client_connection *)arg;
  DEBUG("client_connection_onerror", "events=%d fd=%d\n", events, client->ws->fd);

  if (events & EVBUFFER_EOF) {
    INFO("client_connection_onerror", "Remote host disconnected on fd=%d\n", client->ws->fd);
    client->ws->is_shutdown = true;
  }
  else if (events & BEV_EVENT_ERROR) {
    INFO("client_connection_onerror", "Got an error on fd=%d: %s\n", client->ws->fd, evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
  }
  else if (events & EVBUFFER_TIMEOUT) {
    INFO("client_connection_onerror", "Remote host timed out on fd=%d\n", client->ws->fd);
  }
  else {
    INFO("client_connection_onerror", "Remote host experienced an unknown error (0x%08x) on fd=%d\n", events, client->ws->fd);
  }
  client_connection_destroy(client);
}


static void
client_connection_onread_initial(struct client_connection *const client, const uint8_t *const buf, const size_t nbytes) {
  struct lexer lex;
  struct http_request *req = NULL;
  enum status status;

  // Initialise our required data structures.
  if (!lexer_init(&lex, (const char *)&buf[0], (const char *)&buf[0] + nbytes)) {
    ERROR0("client_connection_onread_initial", "failed to construct lexer instance (`lexer_init` failed)\n");
    return;
  }
  if ((req = http_request_init()) == NULL) {
    WARNING0("client_connection_onread_initial", "failed to construct http_request instance\n");
    lexer_destroy(&lex);
    return;
  }

  // Try and parse the HTTP request.
  if ((status = http_request_parse(req, &lex)) != STATUS_OK) {
    WARNING("client_connection_onread_initial", "failed to parse the HTTP request. status=%d\n", status);
    http_request_destroy(req);
    lexer_destroy(&lex);
    return;
  }

  // TODO ensure that the host matches what we think we're serving.
  DEBUG("client_connection_onread_initial", "Request is for host '%s'\n", req->host);

  // See if the HTTP request is accepted by the websocket protocol.
  status = websocket_accept_http_request(client->ws, req);
  if (status != STATUS_OK) {
    WARNING("client_connection_onread_initial", "websocket_accept_http_request failed. status=%d\n", status);
  }

  // Flush the output buffer.
  websocket_flush_output(client->ws);

  // Free up resources.
  if ((status = http_request_destroy(req)) != STATUS_OK) {
    ERROR("client_connection_onread_initial", "failed to destroy HTTP request instance. status=%d\n", status);
  }
  if (!lexer_destroy(&lex)) {
    ERROR0("client_connection_onread_initial", "failed to destroy lexer instance (`lexer_destroy` failed)\n");
  }
}


static void
client_connection_onread_websocket(struct client_connection *const client, const uint8_t *const buf, const size_t nbytes) {
  enum status status = websocket_consume(client->ws, buf, nbytes);
  if (status != STATUS_OK) {
    WARNING("client_connection_onread_websocket", "websocket_consume failed. status=%d\n", status);
  }
  else if (client->ws->in_state == WS_CLOSED) {
    client_connection_destroy(client);
  }
}


static void
client_connection_onread(struct bufferevent *const bev, void *const arg) {
  (void)bev;
  static uint8_t buf[4096];
  struct client_connection *const client = (struct client_connection *)arg;

  // Read the data.
  const size_t nbytes = bufferevent_read(bev, buf, sizeof(buf));
  DEBUG("client_connection_onread", "bufferevent_read read in %zu bytes from fd=%d\n", nbytes, client->ws->fd);
  if (nbytes == 0) {
    return;
  }

  // If the client hasn't tried to establish a websocket connection yet, this must be the inital
  // HTTP `Upgrade` request.
  if (client->ws->in_state == WS_NEEDS_HTTP_UPGRADE) {
    client_connection_onread_initial(client, buf, nbytes);
    // If we failed to process the HTTP request as a websocket establishing connection, drop the client.
    if (client->ws->in_state == WS_NEEDS_HTTP_UPGRADE) {
      WARNING("client_connection_onread", "Failed to upgrade to websocket. Aborting connection on client=%p fd=%d\n", client, client->ws->fd);
      client->needs_shutdown = true;
    }
  }
  else {
    client_connection_onread_websocket(client, buf, nbytes);
  }
}


static void
client_connection_onwrite(struct bufferevent *const bev, void *const arg) {
  struct client_connection *const client = (struct client_connection *)arg;
  DEBUG("client_connection_onwrite", "bev=%p client=%p fd=%d needs_shutdown=%d\n", bev, client, client->ws->fd, client->needs_shutdown);
  if (client->needs_shutdown) {
    websocket_shutdown(client->ws);
  }
}


static struct client_connection *
client_connection_create(const int fd, const struct sockaddr_in *const addr) {
  // Construct the websocket connection object.
  struct websocket *const ws = websocket_init(fd, addr);
  if (ws == NULL) {
    return NULL;
  }

  // Construct the client connection object.
  struct client_connection *const client = malloc(sizeof(struct client_connection));
  if (client == NULL) {
    ERROR("client_connection_create", "failed to malloc: %s\n", strerror(errno));
    return NULL;
  }

  // Construct the client_connection instance and insert it into the list of all clients.
  memset(client, 0, sizeof(struct client_connection));
  client->ws = ws;
  client->next = clients;
  client->needs_shutdown = false;
  clients = client;

  // Configure the buffered I/O event.
  ws->bev = bufferevent_socket_new(server_loop, fd, BEV_OPT_CLOSE_ON_FREE);
  if (ws->bev == NULL) {
    ERROR0("client_connection_create", "`bufferevent_socket_new` failed.\n");
    client_connection_destroy(client);
    return NULL;
  }
  bufferevent_setcb(ws->bev, &client_connection_onread, &client_connection_onwrite, &client_connection_onerror, client);
  bufferevent_settimeout(ws->bev, 60, 0);
  if (bufferevent_enable(ws->bev, EV_READ | EV_WRITE) == -1) {
    ERROR("client_connection_create", "failed to enable the client read bufferevent (`bufferevent_enable` failed) on fd=%d\n", ws->fd);
    client_connection_destroy(client);
    return NULL;
  }

  return client;
}


static void
_client_connection_destroy(struct client_connection *const client) {
  websocket_destroy(client->ws);
  free(client);
}


static void
client_connection_destroy(struct client_connection *const target) {
  DEBUG("client_connection_destroy", "target=%p\n", (void *)target);
  struct client_connection *client, *prev = NULL;
  for (client = clients; client != NULL; client = client->next) {
    DEBUG("client_connection_destroy", "target=%p client=%p\n", (void *)target, (void *)client);
    if (client == target) {
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
    ERROR("setup_connection", "failed to set fd=%d to non-blocking: %s\n", fd, strerror(errno));
    return;
  }

  client = client_connection_create(fd, addr);
  if (client == NULL) {
    ERROR0("setup_connection", "failed to create client connection object\n");
    return;
  }
}


static void
on_accept(const int listen_fd, const short events, void *const arg) {
  DEBUG("on_accept", "begin %d %d %p\n", listen_fd, events, arg);

  // Ensure we have a read event.
  if (!(events & EV_READ)) {
    ERROR("on_accept", "Received unexpected event %u\n", events);
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
        WARNING("on_accept", "Failed to read from main listening socket: %s", strerror(errno));
      }
      break;
    }

    INFO("on_accept", "Accepted child connection on fd %d\n", in_fd);
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
  INFO("main", "libevent version: %s\n", event_get_version());
  server_loop = event_base_new();
  if (server_loop == NULL) {
    ERROR0("main", "Failed to create libevent event loop\n");
    return 1;
  }
  INFO("main", "libevent is using %s for events.\n", event_base_get_method(server_loop));

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
  event_set(&connect_event, listen_fd, EV_READ | EV_PERSIST, on_accept, server_loop);
  event_base_set(server_loop, &connect_event);
  if (event_add(&connect_event, NULL) == -1) {
    ERROR0("main", "Failed to schedule a socket listen into the main event loop\n");
    return 1;
  }

  // Run the libevent event loop.
  INFO("main", "Starting libevent event loop, listening on %s:%u\n", bind_host, bind_port);
  if (event_base_dispatch(server_loop) == -1) {
    ERROR0("main", "Failed to run libevent event loop\n");
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
