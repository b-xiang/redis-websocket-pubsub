#include <arpa/inet.h>
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
listen_socket_cb(const int listen_fd, const short evtype, void *const arg) {
  fprintf(stderr, "[DEBUG] [listen_socket_cb] %d %d %p\n", listen_fd, evtype, arg);
}


// ================================================================================================
// main.
// ================================================================================================
int
main(int argc, char **argv) {
  struct event connect_event;
  int listen_fd;

  // Parse argv.
  if (!parse_argv(argc, argv)) {
    return 1;
  }

  // Ignore SIGPIPE and die gracefully on SIGINT and SIGTERM.
  {
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
      perror("call to signal() failed");
      return 1;
    }

    sigset_t sigset;
    sigemptyset(&sigset);
    const struct sigaction siginfo = {
      .sa_handler = sighandler,
      .sa_mask = sigset,
      .sa_flags = SA_RESTART,
    };
    if (sigaction(SIGINT, &siginfo, NULL) == -1) {
      perror("sigaction(SIGINT) failed");
      return 1;
    }
    if (sigaction(SIGTERM, &siginfo, NULL) == -1) {
      perror("sigaction(SIGTERM) failed");
      return 1;
    }
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
  event_set(&connect_event, listen_fd, EV_READ | EV_PERSIST, listen_socket_cb, server_loop);
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

  // Free up the libevent event loop.
  event_base_free(server_loop);

  // Close the main socket.
  if (close(listen_fd) == -1) {
    perror("close(listen_fd) failed");
  }

  return 0;
}
