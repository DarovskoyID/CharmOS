// Системный журнал (docs/abi/console.md §2). Канал управления от init
// (хендл без роли): «add <имя>» с концом канала журнала службы. Сообщение
// службы: uint32_t level, затем текст до 1020 байт. Запись — строкой в
// /var/log/system.log и копией в журнал ядра.
#include <charm/libc.h>
#include <charm/sys.hpp>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace {

using charm::Handle;
using charm::Status;

constexpr int kMaxClients = 32;
constexpr uint64_t kControlKey = 1000;

struct Client {
  Handle ch;
  char name[32];
};
Client g_clients[kMaxClients];
int g_log_fd = -1;

void write_line(const char* who, uint32_t level, const char* text, uint32_t len) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  char line[1200];
  int n = snprintf(line, sizeof line, "[%5ld.%03ld] <%u> %s: %.*s\n", static_cast<long>(ts.tv_sec),
                   ts.tv_nsec / 1'000'000, level, who, static_cast<int>(len), text);
  if (n <= 0) return;
  size_t l = static_cast<size_t>(n) < sizeof line ? static_cast<size_t>(n) : sizeof line - 1;
  if (g_log_fd >= 0) (void)write(g_log_fd, line, l);
  printf("logd: %s: %.*s\n", who, static_cast<int>(len), text);
}

}  // namespace

extern "C" int main(int, char**) {
  Handle control = charm_extra_handle(0);
  if (control == 0) {
    printf("logd: no control channel\n");
    return 1;
  }
  (void)mkdir("/var", 0755);
  (void)mkdir("/var/log", 0755);
  g_log_fd = open("/var/log/system.log", O_WRONLY | O_CREAT | O_APPEND);
  if (g_log_fd < 0) printf("logd: cannot open /var/log/system.log: %s\n", strerror(errno));
  auto port = charm::port_create();
  if (!port.ok()) return 1;
  Handle p = static_cast<Handle>(port.value);
  (void)charm::object_bind(p, control, kControlKey);
  write_line("logd", 6, "started", 7);
  for (;;) {
    charm::PortPacket pkt;
    if (charm::port_wait(p, &pkt, charm::kForever) != Status::Ok) continue;
    if (pkt.key == kControlKey) {
      if (pkt.type == abi::syscall::kPacketPeerClosed) return 0;  // init ушёл
      char msg[64];
      Handle hs[1];
      auto r = charm::channel_read(control, msg, sizeof msg - 1, hs, 1, 0);
      if (r.ok()) {
        uint32_t len = static_cast<uint32_t>(r.value >> 32);
        uint32_t nh = static_cast<uint32_t>(r.value);
        msg[len] = '\0';
        if (strncmp(msg, "add ", 4) == 0 && nh == 1) {
          for (int i = 0; i < kMaxClients; ++i) {
            if (g_clients[i].ch == 0) {
              g_clients[i].ch = hs[0];
              snprintf(g_clients[i].name, sizeof g_clients[i].name, "%s", msg + 4);
              (void)charm::object_bind(p, hs[0], static_cast<uint64_t>(i));
              break;
            }
          }
        }
      }
      (void)charm::object_bind(p, control, kControlKey);
      continue;
    }
    if (pkt.key >= kMaxClients || g_clients[pkt.key].ch == 0) continue;
    Client& c = g_clients[pkt.key];
    // Прочитать всё, что есть.
    for (;;) {
      char msg[1024];
      Handle none[1];
      auto r = charm::channel_read(c.ch, msg, sizeof msg, none, 1, 0);
      if (!r.ok()) {
        if (r.status == Status::PeerClosed) {
          (void)charm::handle_close(c.ch);
          c.ch = 0;
        }
        break;
      }
      uint32_t len = static_cast<uint32_t>(r.value >> 32);
      if (len >= 4) {
        uint32_t level;
        memcpy(&level, msg, 4);
        write_line(c.name, level, msg + 4, len - 4);
      }
    }
    if (c.ch != 0) (void)charm::object_bind(p, c.ch, pkt.key);
  }
}
