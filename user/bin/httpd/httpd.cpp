// Простой HTTP/1.0-сервер (M8): httpd <порт> <каталог>. GET отдаёт файл из
// каталога (/ → index.html), остальное — 404/405. Соединение — в своём
// потоке. Каждый запрос — строкой в журнал (logd) и в stdout.
#include <arpa/inet.h>
#include <charm/libc.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

const char* g_root = "/srv/www";

const char* content_type(const char* path) {
  const char* dot = strrchr(path, '.');
  if (dot == nullptr) return "application/octet-stream";
  if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0) return "text/html";
  if (strcmp(dot, ".txt") == 0) return "text/plain";
  if (strcmp(dot, ".css") == 0) return "text/css";
  if (strcmp(dot, ".js") == 0) return "text/javascript";
  return "application/octet-stream";
}

void respond(int fd, int code, const char* reason, const char* type, size_t len) {
  char head[256];
  int n = snprintf(head, sizeof head,
                   "HTTP/1.0 %d %s\r\nServer: CharmOS-httpd\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                   "Connection: close\r\n\r\n",
                   code, reason, type, len);
  (void)write(fd, head, static_cast<size_t>(n));
}

void log_request(const char* method, const char* path, int code) {
  char line[300];
  snprintf(line, sizeof line, "%s %s %d", method, path, code);
  printf("httpd: %s\n", line);
  (void)charm_log_write(6, line);
}

void* serve(void* arg) {
  int fd = static_cast<int>(reinterpret_cast<intptr_t>(arg));
  char req[2048];
  size_t len = 0;
  // Заголовок запроса — до пустой строки.
  while (len < sizeof req - 1) {
    ssize_t n = read(fd, req + len, sizeof req - 1 - len);
    if (n <= 0) break;
    len += static_cast<size_t>(n);
    req[len] = '\0';
    if (strstr(req, "\r\n\r\n") != nullptr || strstr(req, "\n\n") != nullptr) break;
  }
  req[len] = '\0';
  char* save;
  char* method = strtok_r(req, " ", &save);
  char* path = strtok_r(nullptr, " \r\n", &save);
  if (method == nullptr || path == nullptr) {
    close(fd);
    return nullptr;
  }
  static const char kNotFound[] = "<h1>404 Not Found</h1>\n";
  if (strcmp(method, "GET") != 0) {
    respond(fd, 405, "Method Not Allowed", "text/plain", 0);
    log_request(method, path, 405);
  } else if (path[0] != '/' || strstr(path, "..") != nullptr) {
    respond(fd, 400, "Bad Request", "text/plain", 0);
    log_request(method, path, 400);
  } else {
    char file[512];
    snprintf(file, sizeof file, "%s%s%s", g_root, path, strcmp(path, "/") == 0 ? "index.html" : "");
    int f = open(file, O_RDONLY);
    struct stat st;
    if (f < 0 || fstat(f, &st) != 0 || !S_ISREG(st.st_mode)) {
      if (f >= 0) close(f);
      respond(fd, 404, "Not Found", "text/html", sizeof kNotFound - 1);
      (void)write(fd, kNotFound, sizeof kNotFound - 1);
      log_request(method, path, 404);
    } else {
      respond(fd, 200, "OK", content_type(file), static_cast<size_t>(st.st_size));
      char buf[8192];
      ssize_t n;
      while ((n = read(f, buf, sizeof buf)) > 0) {
        if (write(fd, buf, static_cast<size_t>(n)) != n) break;
      }
      close(f);
      log_request(method, path, 200);
    }
  }
  shutdown(fd, SHUT_WR);
  close(fd);
  return nullptr;
}

}  // namespace

extern "C" int main(int argc, char** argv) {
  int port = argc > 1 ? atoi(argv[1]) : 8080;
  if (argc > 2) g_root = argv[2];
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) {
    printf("httpd: socket: %s\n", strerror(errno));
    return 1;
  }
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(static_cast<uint16_t>(port));
  a.sin_addr.s_addr = INADDR_ANY;
  if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || listen(s, 16) != 0) {
    printf("httpd: bind/listen on %d: %s\n", port, strerror(errno));
    return 1;
  }
  printf("httpd: listening on port %d, serving %s\n", port, g_root);
  char msg[64];
  snprintf(msg, sizeof msg, "listening on port %d", port);
  (void)charm_log_write(6, msg);
  for (;;) {
    int c = accept(s, nullptr, nullptr);
    if (c < 0) {
      if (errno == EINTR) continue;
      printf("httpd: accept: %s\n", strerror(errno));
      return 1;
    }
    pthread_t t;
    if (pthread_create(&t, nullptr, serve, reinterpret_cast<void*>(static_cast<intptr_t>(c))) == 0) {
      pthread_detach(t);
    } else {
      serve(reinterpret_cast<void*>(static_cast<intptr_t>(c)));
    }
  }
}
