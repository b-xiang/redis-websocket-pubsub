#include <arpa/inet.h>
#include <ctype.h>
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
#include <event2/buffer.h>
#include <event2/event.h>

#include "client_connection.h"
#include "compat_openssl.h"
#include "lexer.h"
#include "logging.h"
#include "http.h"
#include "json.h"
#include "pubsub_manager.h"
#include "websocket.h"

#ifndef SA_RESTART
#define SA_RESTART   0x10000000 /* Restart syscall on signal return.  */
#endif


static const char *bind_host = "0.0.0.0";
static uint16_t bind_port = 9999;

static const char *redis_host = "127.0.0.1";
static uint16_t redis_port = 6379;

static const char *log_path = "/dev/stderr";

static int use_ssl = 0;
static const char *ssl_certificate_chain_path = NULL;
static const char *ssl_dh_params_path = NULL;
static const char *ssl_private_key_path = NULL;
static const char *ssl_ciphers = "ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-SHA384:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA:DHE-RSA-AES256-SHA";

static const struct option ARGV_OPTIONS[] = {
  {"bind_host", required_argument, NULL, 'h'},
  {"bind_port", required_argument, NULL, 'p'},
  {"redis_host", required_argument, NULL, 'H'},
  {"redis_port", required_argument, NULL, 'P'},
  {"log", required_argument, NULL, 'l'},
  {"use_ssl", no_argument, &use_ssl, 1000},
  {"ssl_certificate_chain", required_argument, NULL, 1001},
  {"ssl_dh_params", required_argument, NULL, 1002},
  {"ssl_private_key", required_argument, NULL, 1003},
  {"ssl_ciphers", required_argument, NULL, 1004},
  {NULL, 0, NULL, 0},
};


// Server-level global singletons.
static struct event_base *server_loop = NULL;
static struct pubsub_manager *pubsub_mgr = NULL;
static SSL_CTX *ssl_ctx = NULL;


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
    fprintf(f, "  ");
    if (isalpha(opt->val) || isdigit(opt->val)) {
      fprintf(f, "-%c", (char)opt->val);
    }
    else {
      fprintf(f, "  ");
    }
    fprintf(f, " --%s", opt->name);
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
    c = getopt_long(argc, argv, "h:p:H:P:l:", ARGV_OPTIONS, &index);
    switch (c) {
    case -1:  // Finished processing.
      return true;
    case 0:  // No arguments.
      break;
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
    case 'l':
      log_path = optarg;
      break;
    case 1001:
      ssl_certificate_chain_path = optarg;
      break;
    case 1002:
      ssl_dh_params_path = optarg;
      break;
    case 1003:
      ssl_private_key_path = optarg;
      break;
    case 1004:
      ssl_ciphers = optarg;
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
process_websocket_message(struct websocket *const ws, const struct json_value *const msg) {
  enum status status;
  struct json_value *action, *key, *data;

  // Ensure we have `action`, `key`, and `data` elements.
  action = json_value_get(msg, "action");
  key = json_value_get(msg, "key");
  if (action == NULL || key == NULL || action->type != JSON_VALUE_TYPE_STRING || key->type != JSON_VALUE_TYPE_STRING) {
    WARNING0("process_websocket_message", "`action` or `key` invalid in JSON payload.\n");
    return;
  }

  if (strcmp(action->as.string, "pub") == 0) {
    data = json_value_get(msg, "data");
    if (data == NULL || data->type != JSON_VALUE_TYPE_STRING) {
      WARNING0("process_websocket_message", "`data` invalid in JSON payload.\n");
      return;
    }
    status = pubsub_manager_publish(pubsub_mgr, key->as.string, data->as.string);
    if (status != STATUS_OK && status != STATUS_DISCONNECTED) {
      ERROR("process_websocket_message", "pubsub_manager_publish failed. status=%d\n", status);
    }
  }
  else if (strcmp(action->as.string, "sub") == 0) {
    status = pubsub_manager_subscribe(pubsub_mgr, key->as.string, ws);
    if (status != STATUS_OK && status != STATUS_DISCONNECTED) {
      ERROR("process_websocket_message", "pubsub_manager_subscribe failed. status=%d\n", status);
    }
  }
  else if (strcmp(action->as.string, "unsub") == 0) {
    status = pubsub_manager_unsubscribe(pubsub_mgr, key->as.string, ws);
    if (status != STATUS_OK && status != STATUS_DISCONNECTED) {
      ERROR("process_websocket_message", "pubsub_manager_unsubscribe failed. status=%d\n", status);
    }
  }
  else {
    WARNING("process_websocket_message", "unknown action '%s'\n", action->as.string);
  }
}


static void
handle_websocket_message(struct websocket *const ws) {
  if (ws->in_message_is_binary) {
    WARNING0("handle_websocket_message", "Unexpected binary message. Dropping.\n");
    return;
  }

  const char *const encoded = (char *)evbuffer_pullup(ws->in_message_buffer, -1);
  INFO("handle_websocket_message", "encoded=%p\n", encoded);
  if (encoded == NULL) {
    ERROR0("handle_websocket_message", "evbuffer_pullup returned null.\n");
    return;
  }

  // Parse and process the JSON.
  struct json_value *const msg = json_parse_n(encoded, evbuffer_get_length(ws->in_message_buffer));
  if (msg == NULL) {
    WARNING0("handle_websocket_message", "Failed to parse JSON payload.\n");
    return;
  }
  process_websocket_message(ws, msg);
  json_value_destroy(msg);
}


static void
setup_connection(const int fd) {
  struct client_connection *client;

  if (set_nonblocking(fd) != 0) {
    ERROR("setup_connection", "failed to set fd=%d to non-blocking: %s\n", fd, strerror(errno));
    return;
  }

  client = client_connection_create(server_loop, ssl_ctx, fd, pubsub_mgr, &handle_websocket_message);
  if (client == NULL) {
    ERROR0("setup_connection", "failed to create client connection object\n");
    return;
  }
}


static void
on_accept(const int listen_fd, const short events, void *const arg) {
  (void)arg;

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
    setup_connection(in_fd);
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

  // Setup logging.
  logging_open(log_path);

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

  // Initialise and configure OpenSSL.
  if (use_ssl) {
    if (ssl_certificate_chain_path == NULL) {
      ERROR0("main", "ssl_certificate_chain is unset.\n");
      return 1;
    }
    else if (ssl_dh_params_path == NULL) {
      ERROR0("main", "ssl_dh_params is unset.\n");
      return 1;
    }
    else if (ssl_private_key_path == NULL) {
      ERROR0("main", "ssl_private_key is unset.\n");
      return 1;
    }
    ssl_ctx = openssl_initialise(ssl_certificate_chain_path, ssl_private_key_path, ssl_dh_params_path, ssl_ciphers);
    if (ssl_ctx == NULL) {
      return 1;
    }
  }

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

  // Connect to redis.
  pubsub_mgr = pubsub_manager_create(redis_host, redis_port, server_loop);
  if (pubsub_mgr == NULL) {
    ERROR0("main", "Failed to setup async connection to redis.\n");
    return 1;
  }

  // Run the libevent event loop.
  INFO("main", "Starting libevent event loop, listening on %s:%u\n", bind_host, bind_port);
  if (event_base_dispatch(server_loop) == -1) {
    ERROR0("main", "Failed to run libevent event loop\n");
  }

  // Free up the connections.
  client_connection_destroy_all();

  // Disconnect from redis.
  pubsub_manager_destroy(pubsub_mgr);

  // Free up the libevent event loop.
  event_base_free(server_loop);

  // Close the main socket.
  if (close(listen_fd) == -1) {
    ERROR("main", "close(listen_fd) failed: %s", strerror(errno));
  }

  // Teardown OpenSSL.
  if (use_ssl) {
    openssl_destroy(ssl_ctx);
    ssl_ctx = NULL;
  }

  // Teardown logging.
  logging_close();

  return 0;
}
