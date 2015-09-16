#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>

#include "compat_openssl.h"
#include "logging.h"


static void *
zeroing_malloc(const size_t nbytes) {
  return calloc(1, nbytes);
}


SSL_CTX *
openssl_initialise(const char *const certificate_chain_path, const char *const private_key_path, const char *const dh_params_path, const char *const ssl_ciphers) {
  int ret;

  // Set a zeroing malloc.
  CRYPTO_set_mem_functions(&zeroing_malloc, &realloc, &free);

  // Initialise OpenSSL.
  ERR_load_crypto_strings();
  SSL_load_error_strings();
  SSL_library_init();
  OpenSSL_add_all_algorithms();
  INFO("OpenSSL version: %s\n", SSLeay_version(SSLEAY_VERSION));

  // Initialise OpenSSL's random.
  ret = RAND_poll();
  if (ret == 0) {
    ERROR0("Call to `RAND_poll` failed.\n");
    ERR_print_errors_fp(stderr);
    return NULL;
  }

  // Create a SSL context object.
  SSL_CTX *const ssl_ctx = SSL_CTX_new(SSLv23_method());
  if (ssl_ctx == NULL) {
    ERROR0("Call to `SSL_CTX_new` failed.\n");
    ERR_print_errors_fp(stderr);
    return NULL;
  }
  SSL_CTX_set_options(ssl_ctx, SSL_OP_SINGLE_DH_USE | SSL_OP_SINGLE_ECDH_USE | SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_CIPHER_SERVER_PREFERENCE);

  // Explicitly set the ciphers.
  ret = SSL_CTX_set_cipher_list(ssl_ctx, ssl_ciphers);
  if (ret == 0) {
    ERROR("Call to `SSL_CTX_set_cipher_list(%s)` failed.\n", ssl_ciphers);
    ERR_print_errors_fp(stderr);
    return NULL;
  }

  // Set the EC parameters for ECDH.
  EC_KEY *const ecdh = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
  if (ecdh == NULL) {
    ERROR0("Call to `EC_KEY_new_by_curve_name` failed.\n");
    ERR_print_errors_fp(stderr);
    return NULL;
  }
  if (SSL_CTX_set_tmp_ecdh(ssl_ctx, ecdh) != 1) {
    ERROR0("Call to `SSL_CTX_set_tmp_ecdh` failed.\n");
    ERR_print_errors_fp(stderr);
    return NULL;
  }

  // Setup the ephimeral DH parameters.
  {
    FILE *const dh_params_file = fopen(dh_params_path, "r");
    if (dh_params_file == NULL) {
      ERROR("Failed to open DH params file '%s' for reading: %s\n", dh_params_path, strerror(errno));
      return NULL;
    }
    DH *const dh_params = PEM_read_DHparams(dh_params_file, NULL, NULL, NULL);
    if (dh_params == NULL) {
      ERROR0("Call to `PEM_read_DHparams` failed.\n");
      ERR_print_errors_fp(stderr);
      return NULL;
    }
    if (SSL_CTX_set_tmp_dh(ssl_ctx, dh_params) != 1) {
      ERROR0("Call to `SSL_CTX_set_tmp_dh` failed.\n");
      ERR_print_errors_fp(stderr);
      return NULL;
    }
    fclose(dh_params_file);
  }

  // Load and check the SSL certificate.
  if (SSL_CTX_use_certificate_chain_file(ssl_ctx, certificate_chain_path) != 1) {
    ERROR0("Call to `SSL_CTX_use_certificate_chain_file` failed.\n");
    ERR_print_errors_fp(stderr);
    return NULL;
  }
  if (SSL_CTX_use_PrivateKey_file(ssl_ctx, private_key_path, SSL_FILETYPE_PEM) != 1) {
    ERROR0("Call to `SSL_CTX_use_PrivateKey_file` failed.\n");
    ERR_print_errors_fp(stderr);
    return NULL;
  }
  if (SSL_CTX_check_private_key(ssl_ctx) != 1) {
    ERROR0("Call to `SSL_CTX_check_private_key` failed.\n");
    ERR_print_errors_fp(stderr);
    return NULL;
  }

  return ssl_ctx;
}


void
openssl_destroy(SSL_CTX *const ssl_ctx) {
  SSL_CTX_free(ssl_ctx);
}


const char *
openssl_ERR_reason_error_string(unsigned long error) {
  return ERR_reason_error_string(error);
}


const char *
openssl_ERR_lib_error_string(unsigned long error) {
  return ERR_lib_error_string(error);
}


const char *
openssl_ERR_func_error_string(unsigned long error) {
  return ERR_func_error_string(error);
}


unsigned char *
openssl_SHA1(const unsigned char *const d, const size_t n, unsigned char *const md) {
  return SHA1(d, n, md);
}


void
openssl_SSL_free(SSL *const ssl) {
  SSL_free(ssl);
}


SSL *
openssl_SSL_new(SSL_CTX *const ssl_ctx) {
  return SSL_new(ssl_ctx);
}


#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic pop
#endif
