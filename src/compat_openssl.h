#pragma once

#include <stdlib.h>

#include <openssl/ssl.h>


SSL_CTX *      openssl_initialise(const char *certificate_chain_path, const char *private_key_path, const char *dh_params_path, const char *ssl_ciphers);
void           openssl_destroy(SSL_CTX *ssl_ctx);

const char *   openssl_ERR_reason_error_string(unsigned long error);
const char *   openssl_ERR_lib_error_string(unsigned long error);
const char *   openssl_ERR_func_error_string(unsigned long error);
unsigned char *openssl_SHA1(const unsigned char *d, size_t n, unsigned char *md);
void           openssl_SSL_free(SSL *ssl);
SSL *          openssl_SSL_new(SSL_CTX *ssl_ctx);
