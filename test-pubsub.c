#include <getopt.h>
#include <inttypes.h>
#include <regex.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>

#include <hiredis/hiredis.h>
#include <hiredis/async.h>
#include <hiredis/adapters/libevent.h>

static const struct option ARGV_OPTIONS[] = {
  {"bind_host", required_argument, NULL, 'h'},
  {"bind_port", required_argument, NULL, 'p'},
  {"redis_host", required_argument, NULL, 'H'},
  {"redis_port", required_argument, NULL, 'P'},
  {NULL, 0, NULL, 0},
};

static const char *bind_host = "0.0.0.0";
static int bind_port = 9999;
static const char *redis_host = "127.0.0.1";
static int redis_port = 6379;

static void on_http_course_thread_request(struct evhttp_request *, redisAsyncContext *, uint64_t);
static void on_http_thread_request(struct evhttp_request *, redisAsyncContext *, uint64_t);
static void on_http_user_thread_request(struct evhttp_request *, redisAsyncContext *, uint64_t);

struct url_handler {
  const char *regex_str;
  void (*handler)(struct evhttp_request *, redisAsyncContext *, uint64_t id);
  regex_t *regex;
};

static struct url_handler URL_HANDLERS[] = {
  {"^/pubsub/course-thread/([0-9]+)/sub$", &on_http_course_thread_request, NULL},
  {"^/pubsub/thread/([0-9]+)/sub$", &on_http_thread_request, NULL},
  {"^/pubsub/user-thread/([0-9]+)/sub$", &on_http_user_thread_request, NULL},
  {NULL, NULL, NULL},
};



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
  int index, c;
  while (true) {
    c = getopt_long(argc, argv, "h:p:H:P:", ARGV_OPTIONS, &index);
    switch (c) {
    case -1:  // Finished processing.
      return true;
    case 'h':
      bind_host = optarg;
      break;
    case 'p':
      bind_port = atoi(optarg);
      break;
    case 'H':
      redis_host = optarg;
      break;
    case 'P':
      redis_port = atoi(optarg);
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


static void
init(void) {
  int ret;

  // Initialise the regular expressions.
  for (size_t i = 0; ; ++i) {
    struct url_handler *const handler = &URL_HANDLERS[i];
    if (handler->handler == NULL) {
      break;
    }

    handler->regex = malloc(sizeof(regex_t));
    if (handler->regex == NULL) {
      perror("malloc failed");
      exit(1);
    }
    ret = regcomp(handler->regex, handler->regex_str, REG_EXTENDED);
    if (ret != 0) {
      fprintf(stderr, "Error: regcomp for '%s' failed (%d)\n", handler->regex_str, ret);
      exit(1);
    }
  }

  // Ignore SIGPIPE.
  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
    perror("call to signal() failed");
    exit(1);
  }
}


// ================================================================================================
// hiredis pubsub callbacks
// ================================================================================================
static void
onmessage_sub_testtopic(redisAsyncContext *const ctx, void *const _reply, void *const data) {
  (void)ctx;
  const redisReply *const reply = _reply;
  (void)data;

  // Ensure we actually have a reply.
  if (reply == NULL) {
    return;
  }

  if (reply->type == REDIS_REPLY_ARRAY) {
    printf("[onmessage_sub_testtopic] ctx=%p reply=%p data=%p\n", ctx, reply, data);
    for (size_t j = 0; j != reply->elements; j++) {
      printf("[onmessage_sub_testtopic]  %zu) %s\n", j, reply->element[j]->str);
    }
  }
}


// ================================================================================================
// evhttp HTTP callbacks
// ================================================================================================
static void
on_http_course_thread_request(struct evhttp_request *const req, redisAsyncContext *const ctx, const uint64_t id) {
#if 0
  struct evbuffer *response_buffer = NULL;
  // Create a buffer to hold the response.
  response_buffer = evbuffer_new();
  if (response_buffer == NULL) {
    fprintf(stderr, "Call to `evbuffer_new` failed\n");
    goto err500;
  }

  // Send the HTTP response.
  evhttp_send_reply(req, 200, "OK", response_buffer);

  if (response_buffer != NULL) {
    evbuffer_free(response_buffer);
  }
#endif
  printf("[on_http_course_thread_request] req=%p ctx=%p id=%" PRIu64 "\n", req, ctx, id);
  evhttp_send_error(req, 501, NULL);  // 501: Not Implemented.
}


static void
on_http_thread_request(struct evhttp_request *const req, redisAsyncContext *const ctx, const uint64_t id) {
  printf("[on_http_course_thread_request] req=%p ctx=%p id=%" PRIu64 "\n", req, ctx, id);
  evhttp_send_error(req, 501, NULL);  // 501: Not Implemented.
}


static void
on_http_user_thread_request(struct evhttp_request *const req, redisAsyncContext *const ctx, const uint64_t id) {
  printf("[on_http_course_thread_request] req=%p ctx=%p id=%" PRIu64 "\n", req, ctx, id);
  evhttp_send_error(req, 501, NULL);  // 501: Not Implemented.
}


static void
on_http_request(struct evhttp_request *const req, void *const data) {
  int ret;
  redisAsyncContext *const ctx = data;
  struct evhttp_uri *uri = NULL;
  const char *uri_path = NULL;
  const size_t regex_ngroups = 2;
  regmatch_t regex_groups[regex_ngroups];

  printf("[on_http_request] req=%p data=%p\n", req, ctx);

  // Ensure we have a GET request.
  if (evhttp_request_get_command(req) != EVHTTP_REQ_GET) {
    fprintf(stderr, "Got a non-GET request\n");
    goto err405;
  }

  // Attempt to parse the URI.
  uri = evhttp_uri_parse(evhttp_request_get_uri(req));
  if (uri == NULL) {
    fprintf(stderr, "Failed to parse the URI\n");
    goto err400;
  }

  // Grab the path from the URI, and decode it.
  const char *uri_path_encoded = evhttp_uri_get_path(uri);
  if (uri_path_encoded == NULL) {
    uri_path_encoded = "/";
  }
  uri_path = evhttp_uridecode(uri_path_encoded, 0, NULL);
  if (uri_path == NULL) {
    goto err400;
  }

  // Does the path match one of our expected paths?
  const struct url_handler *handler = NULL;
  for (size_t i = 0; ; ++i) {
    handler = &URL_HANDLERS[i];
    if (handler->regex_str == NULL) {
      handler = NULL;
      break;
    }

    // Does the URL match the regex handler?
    ret = regexec(handler->regex, uri_path, regex_ngroups, regex_groups, 0);
    if (ret == 0) {
      break;
    }
  }

  // We iterated through each of the handlers and did not find a match. We should 404.
  if (handler == NULL) {
    goto err404;
  }

  // Convert the matched string ID to an integer.
  uint64_t id = 0, prev_id = 0;
  for (regoff_t i = regex_groups[1].rm_so; i != regex_groups[1].rm_eo; ++i) {
    prev_id = id;
    id = (10 * id) + (uri_path[i] - '0');

    // Check for overflow.
    if (id < prev_id) {
      goto err404;
    }
  }

  // Invoke the handler and then bail.
  handler->handler(req, ctx, id);
  goto exit;

err400:
  evhttp_send_error(req, 400, NULL);  // 400: Bad Request.
  goto exit;

err404:
  evhttp_send_error(req, 404, NULL);  // 404: Not Found.
  goto exit;

err405:
  evhttp_send_error(req, 405, NULL);  // 405: Method Not Allowed.
  goto exit;

exit:
  // Release resources.
  if (uri_path != NULL) {
    free((void *)uri_path);
  }
  if (uri != NULL) {
    evhttp_uri_free(uri);
  }
}


// ================================================================================================
// main
// ================================================================================================
int
main(int argc, char **argv) {
  // Parse argv.
  if (!parse_argv(argc, argv)) {
    return 1;
  }

  // Perform some once-off global initialisation.
  init();

  // Create a libevent base object.
  struct event_base *const base = event_base_new();
  if (base == NULL) {
    fprintf(stderr, "Call to `event_base_new` failed\n");
    return 1;
  }

  // Connect to redis.
  redisAsyncContext *ctx = redisAsyncConnect(redis_host, redis_port);
  if (ctx->err) {
    fprintf(stderr, "Failed to connect to redis: %s\n", ctx->errstr);
    return 1;
  }

  // Create a evhttp (libevent) HTTP handler object.
  struct evhttp *const http = evhttp_new(base);
  if (http == NULL) {
    fprintf(stderr, "Call to `evhttp_new` failed\n");
    return 1;
  }

  // Add a "generic" callback hander to the evhttp handler since we want to act upon the URLs ourselves.
  evhttp_set_gencb(http, on_http_request, (void *)ctx);

  // Bind the evhttp listener.
  struct evhttp_bound_socket *const handle = evhttp_bind_socket_with_handle(http, bind_host, bind_port);
  if (handle == NULL) {
    fprintf(stderr, "Failed to bind evhttp instance to %s:%d\n", bind_host, bind_port);
    return 1;
  }

  // Attach the redis async instance to the livevent event loop.
  redisLibeventAttach(ctx, base);

  redisAsyncCommand(ctx, onmessage_sub_testtopic, NULL, "SUBSCRIBE testtopic");

  // Start the libevent event loop.
  event_base_dispatch(base);

  return 0;
}
