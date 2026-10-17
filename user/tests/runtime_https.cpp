#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/err.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("https runtime: FAILED line %d errno %d\n", __LINE__, errno); ERR_print_errors_fp(stderr); exit(1); } } while (0)
namespace {
void flush(BIO* outgoing, int fd) {
  char bytes[8192];
  while (BIO_ctrl_pending(outgoing)) {
    int size = BIO_read(outgoing, bytes, sizeof bytes); CHECK(size > 0);
    int sent = 0;
    while (sent < size) {
      ssize_t n = write(fd, bytes + sent, static_cast<size_t>(size - sent));
      if (n < 0 && errno == EINTR) continue;
      CHECK(n > 0); sent += static_cast<int>(n);
    }
  }
}
void receive(BIO* incoming, int fd) {
  char bytes[8192]; ssize_t size;
  do { size = read(fd, bytes, sizeof bytes); } while (size < 0 && errno == EINTR);
  CHECK(size > 0);
  CHECK(BIO_write(incoming, bytes, static_cast<int>(size)) == size);
}
void session(uint16_t version, bool trusted, const char* hostname, time_t verify_time, int expected_error) {
  SSL_CTX* ctx = SSL_CTX_new(TLS_method()); CHECK(ctx);
  CHECK(SSL_CTX_set_min_proto_version(ctx, version) == 1 && SSL_CTX_set_max_proto_version(ctx, version) == 1);
  SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
  if (trusted) {
    unsigned char der[8192]; FILE* file = fopen("/etc/tls-ca.der", "rb"); CHECK(file);
    size_t n = fread(der, 1, sizeof der, file); CHECK(n > 0 && n < sizeof der && !ferror(file));
    CHECK(fclose(file) == 0);
    const unsigned char* p = der;
    X509* ca = d2i_X509(nullptr, &p, static_cast<long>(n)); CHECK(ca && p == der + n);
    CHECK(X509_STORE_add_cert(SSL_CTX_get_cert_store(ctx), ca) == 1); X509_free(ca);
  }
  SSL* client = SSL_new(ctx); CHECK(client);
  CHECK(SSL_set1_host(client, hostname) == 1 && SSL_set_tlsext_host_name(client, hostname) == 1);
  if (verify_time) X509_VERIFY_PARAM_set_time(SSL_get0_param(client), verify_time);
  BIO* incoming = BIO_new(BIO_s_mem()); BIO* outgoing = BIO_new(BIO_s_mem()); CHECK(incoming && outgoing);
  SSL_set_bio(client, incoming, outgoing); SSL_set_connect_state(client);
  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); CHECK(fd >= 0);
  sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(17443);
  CHECK(inet_pton(AF_INET, "10.0.2.2", &address.sin_addr) == 1);
  CHECK(connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0);
  bool finished = false, rejected = false;
  for (unsigned i = 0; i < 1000; ++i) {
    int result = SSL_do_handshake(client);
    int error = result == 1 ? SSL_ERROR_NONE : SSL_get_error(client, result);
    flush(outgoing, fd);
    if (result == 1) { finished = true; break; }
    if (error == SSL_ERROR_SSL) { rejected = true; break; }
    CHECK(error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE);
    if (error == SSL_ERROR_WANT_READ) receive(incoming, fd);
  }
  long verification = SSL_get_verify_result(client);
  if (expected_error == X509_V_OK) {
    CHECK(finished && !rejected && verification == X509_V_OK && SSL_version(client) == version);
    const char request[] = "GET /native-test HTTP/1.0\r\nHost: charmos.test\r\n\r\n";
    const char expected[] = "HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\nOK";
    CHECK(SSL_write(client, request, sizeof request - 1) == sizeof request - 1); flush(outgoing, fd);
    char response[sizeof expected]; size_t used = 0;
    for (unsigned i = 0; used < sizeof expected - 1 && i < 1000; ++i) {
      int result = SSL_read(client, response + used, static_cast<int>(sizeof expected - 1 - used));
      if (result > 0) { used += static_cast<size_t>(result); continue; }
      int error = SSL_get_error(client, result); flush(outgoing, fd);
      CHECK(error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE);
      if (error == SSL_ERROR_WANT_READ) receive(incoming, fd);
    }
    CHECK(used == sizeof expected - 1 && memcmp(response, expected, used) == 0);
  } else {
    CHECK(rejected && !finished && verification != X509_V_OK);
    if (expected_error > 0) CHECK(verification == expected_error);
  }
  CHECK(close(fd) == 0); SSL_free(client); SSL_CTX_free(ctx); ERR_clear_error();
}
}
extern "C" int main() {
  session(TLS1_3_VERSION, true, "charmos.test", 0, X509_V_OK);
  printf("https runtime: native TCP TLS 1.3 HTTP response verified\n");
  session(TLS1_2_VERSION, true, "charmos.test", 0, X509_V_OK);
  printf("https runtime: native TCP TLS 1.2 HTTP response verified\n");
  session(TLS1_3_VERSION, true, "wrong.test", 0, X509_V_ERR_HOSTNAME_MISMATCH);
  session(TLS1_3_VERSION, false, "charmos.test", 0, -1);
  session(TLS1_3_VERSION, true, "charmos.test", 4102444800LL, X509_V_ERR_CERT_HAS_EXPIRED);
  session(TLS1_3_VERSION, true, "charmos.test", 946684800, X509_V_ERR_CERT_NOT_YET_VALID);
  printf("https runtime: hostname, trust, expired and future certificates rejected\n");
  return 0;
}
