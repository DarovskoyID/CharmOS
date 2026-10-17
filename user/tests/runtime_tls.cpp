// Real BoringSSL, real guest entropy/time, memory BIO transport (no host TLS).
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("tls runtime: FAILED line %d\n", __LINE__); ERR_print_errors_fp(stderr); exit(1); } } while (0)
namespace {
unsigned char input[8192];
size_t load(const char* path) {
  FILE* file = fopen(path, "rb"); CHECK(file != nullptr);
  size_t size = fread(input, 1, sizeof input, file);
  CHECK(size > 0 && size < sizeof input && !ferror(file));
  CHECK(fclose(file) == 0); return size;
}
X509* certificate(const char* path) {
  size_t size = load(path); const unsigned char* p = input;
  X509* cert = d2i_X509(nullptr, &p, static_cast<long>(size));
  CHECK(cert && p == input + size); return cert;
}
void session(int version, bool trusted, const char* host, time_t verify_time, int expected_error) {
  SSL_CTX* client_ctx = SSL_CTX_new(TLS_method());
  SSL_CTX* server_ctx = SSL_CTX_new(TLS_method());
  CHECK(client_ctx && server_ctx);
  CHECK(SSL_CTX_set_min_proto_version(client_ctx, version) == 1);
  CHECK(SSL_CTX_set_max_proto_version(client_ctx, version) == 1);
  SSL_CTX_set_verify(client_ctx, SSL_VERIFY_PEER, nullptr);
  if (trusted) {
    X509* ca = certificate("/etc/tls-ca.der");
    CHECK(X509_STORE_add_cert(SSL_CTX_get_cert_store(client_ctx), ca) == 1);
    X509_free(ca);
  }
  X509* cert = certificate("/etc/tls-server.der");
  CHECK(SSL_CTX_use_certificate(server_ctx, cert) == 1); X509_free(cert);
  size_t size = load("/etc/tls-key.der"); const unsigned char* p = input;
  EVP_PKEY* key = d2i_AutoPrivateKey(nullptr, &p, static_cast<long>(size));
  CHECK(key && p == input + size);
  CHECK(SSL_CTX_use_PrivateKey(server_ctx, key) == 1); EVP_PKEY_free(key);
  CHECK(SSL_CTX_check_private_key(server_ctx) == 1);
  SSL* client = SSL_new(client_ctx); SSL* server = SSL_new(server_ctx);
  CHECK(client && server && SSL_set1_host(client, host) == 1);
  CHECK(SSL_set_tlsext_host_name(client, host) == 1);
  if (verify_time) X509_VERIFY_PARAM_set_time(SSL_get0_param(client), verify_time);
  BIO* client_bio; BIO* server_bio;
  CHECK(BIO_new_bio_pair(&client_bio, 0, &server_bio, 0) == 1);
  SSL_set_bio(client, client_bio, client_bio); SSL_set_bio(server, server_bio, server_bio);
  SSL_set_connect_state(client); SSL_set_accept_state(server);
  bool failed = false;
  for (unsigned i = 0; i < 1000; ++i) {
    int result = SSL_do_handshake(client);
    if (result != 1) {
      int error = SSL_get_error(client, result);
      if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) { failed = true; break; }
    }
    result = SSL_do_handshake(server);
    if (result != 1) {
      int error = SSL_get_error(server, result);
      CHECK(error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE);
    }
    if (SSL_is_init_finished(client) && SSL_is_init_finished(server)) break;
  }
  int verification = SSL_get_verify_result(client);
  if (expected_error == X509_V_OK) {
    CHECK(!failed && SSL_is_init_finished(client) && SSL_is_init_finished(server));
    CHECK(verification == X509_V_OK && SSL_version(client) == version);
    const char request[] = "GET / HTTP/1.0\r\nHost: charmos.test\r\n\r\n";
    const char response[] = "HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\nOK";
    char bytes[128];
    CHECK(SSL_write(client, request, sizeof request - 1) == sizeof request - 1);
    CHECK(SSL_read(server, bytes, sizeof bytes) == sizeof request - 1);
    CHECK(memcmp(bytes, request, sizeof request - 1) == 0);
    CHECK(SSL_write(server, response, sizeof response - 1) == sizeof response - 1);
    CHECK(SSL_read(client, bytes, sizeof bytes) == sizeof response - 1);
    CHECK(memcmp(bytes, response, sizeof response - 1) == 0);
  } else {
    CHECK(failed && verification != X509_V_OK);
    if (expected_error > 0) CHECK(verification == expected_error);
  }
  SSL_free(client); SSL_free(server); SSL_CTX_free(client_ctx); SSL_CTX_free(server_ctx);
  ERR_clear_error();
}
}
extern "C" int main() {
  unsigned char random[32], digest[32];
  CHECK(RAND_bytes(random, sizeof random) == 1);
  static const unsigned char expected[] = {0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
    0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
  CHECK(SHA256(reinterpret_cast<const unsigned char*>("abc"), 3, digest) == digest);
  CHECK(memcmp(digest, expected, sizeof digest) == 0);
  printf("tls runtime: native entropy and SHA-256 ok\n");
  session(TLS1_3_VERSION, true, "charmos.test", 0, X509_V_OK);
  printf("tls runtime: TLS 1.3 verified HTTP memory exchange ok\n");
  session(TLS1_2_VERSION, true, "charmos.test", 0, X509_V_OK);
  printf("tls runtime: TLS 1.2 verified HTTP memory exchange ok\n");
  session(TLS1_3_VERSION, true, "wrong.test", 0, X509_V_ERR_HOSTNAME_MISMATCH);
  session(TLS1_3_VERSION, false, "charmos.test", 0, -1);
  session(TLS1_3_VERSION, true, "charmos.test", 4102444800LL, X509_V_ERR_CERT_HAS_EXPIRED);
  session(TLS1_3_VERSION, true, "charmos.test", 946684800, X509_V_ERR_CERT_NOT_YET_VALID);
  printf("tls runtime: hostname, trust, expired and future certificates rejected\n");
  return 0;
}
