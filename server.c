#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <event.h>
#include <event2/event.h>

#include "client_connection.h"
#include "lexer.h"
#include "logging.h"
#include "http.h"
#include "websocket.h"

#ifndef SA_RESTART
#define SA_RESTART   0x10000000 /* Restart syscall on signal return.  */
#endif


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


static void
handle_websocket_message(struct websocket *const ws) {
  DEBUG("handle_websocket_message", "ws=%p ws->in_message_opcode=%d\n", ws, ws->in_message_opcode);
}


static void
setup_connection(const int fd, const struct sockaddr_in *const addr) {
  struct client_connection *client;

  if (set_nonblocking(fd) != 0) {
    ERROR("setup_connection", "failed to set fd=%d to non-blocking: %s\n", fd, strerror(errno));
    return;
  }

  client = client_connection_create(server_loop, fd, addr, &handle_websocket_message);
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
