#pragma once

#include <stdlib.h>

#include <openssl/ssl.h>


SSL_CTX *      openssl_initialise(const char *certificate_chain_path, const char *private_key_path, const char *dh_params_path);
void           openssl_destroy(SSL_CTX *ssl_ctx);

unsigned char *openssl_SHA1(const unsigned char *d, size_t n, unsigned char *md);
void           openssl_SSL_free(SSL *ssl);
SSL *          openssl_SSL_new(SSL_CTX *ssl_ctx);
