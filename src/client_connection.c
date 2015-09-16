#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/bufferevent_ssl.h>
#include <event2/event.h>

#include "client_connection.h"
#include "compat_openssl.h"
#include "http.h"
#include "lexer.h"
#include "logging.h"
#include "pubsub_manager.h"
#include "websocket.h"


static struct client_connection *clients = NULL;


// ================================================================================================
// Listening socket's libevent callbacks.
// ================================================================================================
static void
client_connection_onevent(struct bufferevent *const bev, const short events, void *const arg) {
  struct client_connection *const client = (struct client_connection *)arg;

  if (events & BEV_EVENT_EOF) {
    INFO("Remote host disconnected on fd=%d\n", client->fd);
    client->is_shutdown = true;
  }
  else if (events & BEV_EVENT_ERROR) {
    WARNING("Got an error on fd=%d: %s\n", client->fd, evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
    unsigned long err;
    while ((err = bufferevent_get_openssl_error(bev))) {
      WARNING("bufferevent_get_openssl_error:%s:%s:%s\n", openssl_ERR_reason_error_string(err), openssl_ERR_lib_error_string(err), openssl_ERR_func_error_string(err));
    }
  }
  else if (events & EVBUFFER_TIMEOUT) {
    INFO("Remote host timed out on fd=%d\n", client->fd);
  }
  else {
    WARNING("Remote host experienced an unknown error (0x%08x) on fd=%d\n", events, client->fd);
  }
  client_connection_destroy(client);
}


static void
on_read_initial(struct client_connection *const client, const uint8_t *const buf, const size_t nbytes) {
  struct lexer lex;
  enum status status;
  struct http_header *header;

  // Initialise our required data structures.
  if (!lexer_init(&lex, (const char *)&buf[0], (const char *)&buf[0] + nbytes)) {
    ERROR0("failed to construct lexer instance (`lexer_init` failed)\n");
    return;
  }

  // Try and parse the HTTP request.
  if ((status = http_request_parse(client->request, &lex)) != STATUS_OK) {
    WARNING("failed to parse the HTTP request. status=%d\n", status);
    lexer_destroy(&lex);
    return;
  }
  // Copy across the `Cookie` header.
  header = http_request_find_header(client->request, "Cookie");
  if (header != NULL) {
    http_response_add_header(client->response, header->name, header->value);
  }

  // TODO ensure that the host matches what we think we're serving.
  //DEBUG("Request is for host '%s'\n", client->request->host);

  // See if the HTTP request is accepted by the websocket protocol.
  status = websocket_accept_http_request(client->ws, client->response, client->request);
  if (status != STATUS_OK) {
    WARNING("websocket_accept_http_request failed. status=%d\n", status);
  }

  // Flush the output buffer.
  status = http_response_write_evbuffer(client->response, client->ws->out);
  if (status != STATUS_OK) {
    WARNING("http_response_write_evbuffer failed. status=%d\n", status);
  }
  status = websocket_flush_output(client->ws);
  if (status != STATUS_OK) {
    WARNING("websocket_flush_output failed. status=%d\n", status);
  }

  // Free up resources.
  if (!lexer_destroy(&lex)) {
    ERROR0("failed to destroy lexer instance (`lexer_destroy` failed)\n");
  }
}


static void
on_read_websocket(struct client_connection *const client, const uint8_t *const buf, const size_t nbytes) {
  enum status status = websocket_consume(client->ws, buf, nbytes);
  if (status != STATUS_OK) {
    WARNING("websocket_consume failed. status=%d\n", status);
  }
  if (client->ws->in_state == WS_CLOSED) {
    client_connection_destroy(client);
  }
}


static void
on_read(struct bufferevent *const bev, void *const arg) {
  (void)bev;
  static uint8_t buf[4096];
  struct client_connection *const client = (struct client_connection *)arg;

  // Read the data.
  const size_t nbytes = bufferevent_read(bev, buf, sizeof(buf));
  if (nbytes == 0) {
    return;
  }

  // If the client hasn't tried to establish a websocket connection yet, this must be the inital
  // HTTP `Upgrade` request.
  if (client->ws->in_state == WS_NEEDS_HTTP_UPGRADE) {
    on_read_initial(client, buf, nbytes);
    // If we failed to process the HTTP request as a websocket establishing connection, drop the client.
    if (client->ws->in_state == WS_NEEDS_HTTP_UPGRADE) {
      WARNING("Failed to upgrade to websocket. Aborting connection on client=%p fd=%d\n", (void *)client, client->fd);
      client->needs_shutdown = true;
      client_connection_destroy(client);
    }
  }
  else {
    on_read_websocket(client, buf, nbytes);
  }
}


static void
on_write(struct bufferevent *const bev, void *const arg) {
  (void)bev;
  struct client_connection *const client = (struct client_connection *)arg;
  if (client->needs_shutdown) {
    client_connection_shutdown(client);
  }
}


struct client_connection *
client_connection_create(struct event_base *const event_loop, SSL_CTX *const ssl_ctx, const int fd, struct pubsub_manager *const pubsub_mgr, websocket_message_callback in_message_cb) {
  // Construct the client connection object.
  struct client_connection *const client = malloc(sizeof(struct client_connection));
  if (client == NULL) {
    ERROR("failed to malloc: %s\n", strerror(errno));
    return NULL;
  }

  // Construct the client_connection instance and insert it into the list of all clients.
  memset(client, 0, sizeof(struct client_connection));
  client->fd = fd;
  client->request = http_request_init();
  client->response = http_response_init();
  client->ws = websocket_init(client, in_message_cb);
  client->event_loop = event_loop;
  if (ssl_ctx == NULL) {
    client->bev = bufferevent_socket_new(event_loop, fd, BEV_OPT_CLOSE_ON_FREE);
  }
  else {
    client->ssl = openssl_SSL_new(ssl_ctx);
    if (client->ssl == NULL) {
      goto fail;
    }
    client->bev = bufferevent_openssl_socket_new(event_loop, fd, client->ssl, BUFFEREVENT_SSL_CONNECTING, BEV_OPT_CLOSE_ON_FREE);
  }
  client->pubsub_mgr = pubsub_mgr;

  // Construct the websocket connection object.
  if (client->request == NULL || client->response == NULL || client->ws == NULL || client->bev == NULL) {
    goto fail;
  }

  // Configure the buffered I/O event.
  bufferevent_setcb(client->bev, &on_read, &on_write, &client_connection_onevent, client);
  bufferevent_settimeout(client->bev, 60, 0);
  bufferevent_enable(client->bev, EV_READ | EV_WRITE);

  // Insert the client into the list of all connected clients.
  client->next = clients;
  clients = client;

  return client;

fail:
  if (client->request != NULL) {
    http_request_destroy(client->request);
  }
  if (client->response != NULL) {
    http_response_destroy(client->response);
  }
  if (client->ws != NULL) {
    websocket_destroy(client->ws);
  }
  free(client);

  return NULL;
}


static void
_client_connection_destroy(struct client_connection *const client) {
  bufferevent_setcb(client->bev, NULL, NULL, NULL, NULL);
  bufferevent_disable(client->bev, EV_READ | EV_WRITE);

  if (client->request != NULL) {
    http_request_destroy(client->request);
  }
  if (client->response != NULL) {
    http_response_destroy(client->response);
  }
  if (client->ws != NULL) {
    pubsub_manager_unsubscribe_all(client->pubsub_mgr, client->ws);
    websocket_destroy(client->ws);
  }
  if (client->bev != NULL) {
    bufferevent_free(client->bev);
    client->bev = NULL;
  }
  if (client->ssl != NULL) {
    openssl_SSL_free(client->ssl);
  }

  if (client->fd >= 0) {
    client_connection_shutdown(client);
    if (close(client->fd) == -1) {
      WARNING("`close` on fd=%d failed: %s\n", client->fd, strerror(errno));
    }
  }

  free(client);
}


void
client_connection_destroy(struct client_connection *const target) {
  struct client_connection *client, *prev = NULL;
  for (client = clients; client != NULL; client = client->next) {
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


void
client_connection_destroy_all(void) {
  struct client_connection *client, *next;
  for (client = clients; client != NULL; ) {
    next = client->next;
    _client_connection_destroy(client);
    client = next;
  }
  clients = NULL;
}


enum status
client_connection_shutdown(struct client_connection *const client) {
  int ret;
  if (client == NULL) {
    return STATUS_EINVAL;
  }

  if (!client->is_shutdown) {
    if (client->bev != NULL) {
      bufferevent_disable(client->bev, EV_READ | EV_WRITE);
    }
    ret = shutdown(client->fd, SHUT_RDWR);
    if (ret != 0) {
      WARNING("`shutdown` on fd=%d failed: %s\n", client->fd, strerror(errno));
    }
  }

  return STATUS_OK;
}
