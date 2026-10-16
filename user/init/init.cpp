// init и менеджер служб (M8). Запускается ядром (init= в командной строке)
// с ролями Root, Console, Net и Job (корневой). Читает /etc/init.conf:
//   service <имя> <путь> [аргументы...]   — служба, перезапуск при выходе
//   console <имя> <путь> [аргументы...]   — сеанс на консоли (шелл); его
//                                            выход — завершение системы
// Первой запускается служба logd: ей init передаёт концы каналов журнала
// всех остальных. Шелл получает канал управления init: «list», «kill <имя>»,
// «poweroff», «reboot» (power.md: хендл Power есть только у init).
#include <abi/process.hpp>
#include <charm/libc.h>
#include <charm/sys.hpp>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace {

namespace ap = abi::process;
using charm::Handle;
using charm::Status;

constexpr int kMaxServices = 16;
constexpr int kMaxArgs = 16;
constexpr int kMaxRestarts = 5;
constexpr uint64_t kControlKey = 1000;

struct Service {
  char name[32];
  char path[128];
  char* argv[kMaxArgs + 1];
  char args[256];
  bool console;
  Handle proc;  // 0 — не запущена
  int restarts;
};

Service g_svc[kMaxServices];
int g_count = 0;
Handle g_port = 0;
Handle g_logd_ctl = 0;   // наш конец канала управления logd
Handle g_control = 0;    // наш конец канала управления init (для шелла)
Handle g_control_peer = 0;
Handle g_pkgd_peer = 0;  // конец канала запросов pkgd — сеансу на консоли
Handle g_wm_door = 0;    // наш конец двери компоновщика (wm.md §1), дубликаты — всем

bool parse_conf() {
  FILE* f = fopen("/etc/init.conf", "r");
  if (f == nullptr) return false;
  char line[256];
  while (fgets(line, sizeof line, f) != nullptr && g_count < kMaxServices) {
    char* save;
    char* kind = strtok_r(line, " \t\r\n", &save);
    if (kind == nullptr || kind[0] == '#') continue;
    bool console = strcmp(kind, "console") == 0;
    if (!console && strcmp(kind, "service") != 0) continue;
    Service& s = g_svc[g_count];
    memset(&s, 0, sizeof s);
    char* name = strtok_r(nullptr, " \t\r\n", &save);
    char* path = strtok_r(nullptr, " \t\r\n", &save);
    if (name == nullptr || path == nullptr) continue;
    snprintf(s.name, sizeof s.name, "%s", name);
    snprintf(s.path, sizeof s.path, "%s", path);
    s.console = console;
    // argv: имя службы, затем аргументы (копии в s.args).
    size_t used = 0;
    int n = 0;
    auto add = [&](const char* a) {
      size_t l = strlen(a) + 1;
      if (n == kMaxArgs || used + l > sizeof s.args) return;
      memcpy(s.args + used, a, l);
      s.argv[n++] = s.args + used;
      used += l;
    };
    add(name);
    while (char* a = strtok_r(nullptr, " \t\r\n", &save)) add(a);
    s.argv[n] = nullptr;
    ++g_count;
  }
  fclose(f);
  return true;
}

// Канал журнала для службы: один конец — logd (сообщением «add <имя>»), другой — службе.
Handle log_channel(const char* name) {
  if (g_logd_ctl == 0) return 0;
  Handle ch[2];
  if (charm::channel_create(ch) != Status::Ok) return 0;
  char msg[48];
  int n = snprintf(msg, sizeof msg, "add %s", name);
  if (charm::channel_write(g_logd_ctl, msg, static_cast<uint64_t>(n), &ch[1], 1) != Status::Ok) {
    (void)charm::handle_close(ch[0]);
    (void)charm::handle_close(ch[1]);
    return 0;
  }
  return ch[0];
}

bool start(int i) {
  Service& s = g_svc[i];
  uint32_t extra[8], roles[8];
  uint32_t n = 0;
  charm_spawn_opts o{};
  o.give = CHARM_GIVE_ROOT | CHARM_GIVE_CWD;
  bool logd = strcmp(s.name, "logd") == 0;
  if (logd) {
    Handle ch[2];
    if (charm::channel_create(ch) != Status::Ok) return false;
    g_logd_ctl = ch[0];
    extra[n] = ch[1];
    roles[n++] = ap::kRoleNone;
  } else if (Handle log = log_channel(s.name)) {
    extra[n] = log;
    roles[n++] = ap::kRoleLog;
  }
  Handle job = 0;
  bool is_wm = strcmp(s.name, "wm") == 0;
  if (is_wm) {
    // wm.md §1: дверь — канал, конец компоновщику (без роли), наш — дубликатами всем.
    Handle door[2];
    if (charm::channel_create(door) == Status::Ok) {
      if (g_wm_door != 0) (void)charm::handle_close(g_wm_door);
      g_wm_door = door[0];
      extra[n] = door[1];
      roles[n++] = ap::kRoleNone;
    }
    // display.md §1, input.md §1: экран и ввод — только компоновщику.
    if (charm_display() != 0) {
      auto d = charm::handle_duplicate(charm_display(), charm::rights::kDuplicate | charm::rights::kTransfer |
                                                            charm::rights::kRead | charm::rights::kMap);
      if (d.ok()) {
        extra[n] = static_cast<Handle>(d.value);
        roles[n++] = ap::kRoleDisplay;
      }
    }
    if (charm_input() != 0) {
      auto d = charm::handle_duplicate(charm_input(), charm::rights::kDuplicate | charm::rights::kTransfer |
                                                          charm::rights::kRead | charm::rights::kWrite);
      if (d.ok()) {
        extra[n] = static_cast<Handle>(d.value);
        roles[n++] = ap::kRoleInput;
      }
    }
  }
  if (!is_wm && g_wm_door != 0) {
    auto d = charm::handle_duplicate(g_wm_door, charm::rights::kDuplicate | charm::rights::kTransfer |
                                                    charm::rights::kWrite);
    if (d.ok()) {
      extra[n] = static_cast<Handle>(d.value);
      roles[n++] = ap::kRoleWm;
    }
  }
  bool pkgd = strcmp(s.name, "pkgd") == 0;
  if (pkgd) {
    // pkg.md §3: закрытая область — только pkgd; свой Job (передний план для
    // подтверждений), консоль, канал запросов (хендл без роли №0).
    Handle ch[2];
    if (charm::channel_create(ch) != Status::Ok) return false;
    extra[n] = ch[0];
    roles[n++] = ap::kRoleNone;
    if (g_pkgd_peer != 0) (void)charm::handle_close(g_pkgd_peer);
    g_pkgd_peer = ch[1];
    if (charm_system() != 0) {
      auto sd = charm::handle_duplicate(charm_system(), charm::rights::kDuplicate | charm::rights::kTransfer |
                                                            charm::rights::kRead | charm::rights::kWrite);
      if (sd.ok()) {
        extra[n] = static_cast<Handle>(sd.value);
        roles[n++] = ap::kRoleSystem;
      }
    }
    auto j = charm::job_create(charm_job());
    if (j.ok()) {
      job = static_cast<Handle>(j.value);
      auto jd = charm::handle_duplicate(job, charm::rights::kDuplicate | charm::rights::kTransfer |
                                                 charm::rights::kRead | charm::rights::kWrite | charm::rights::kSpawn);
      if (jd.ok()) {
        extra[n] = static_cast<Handle>(jd.value);
        roles[n++] = ap::kRoleJob;
      }
      o.job = job;
    }
    o.give |= CHARM_GIVE_CONSOLE;
  }
  if (s.console) {
    // Сеанс: свой Job (передний план консоли), канал управления init.
    auto j = charm::job_create(charm_job());
    if (!j.ok()) return false;
    job = static_cast<Handle>(j.value);
    auto jd = charm::handle_duplicate(job, charm::rights::kDuplicate | charm::rights::kTransfer |
                                               charm::rights::kRead | charm::rights::kWrite |
                                               charm::rights::kSpawn);
    if (jd.ok()) {
      extra[n] = static_cast<Handle>(jd.value);
      roles[n++] = ap::kRoleJob;
    }
    if (g_control_peer != 0) {
      extra[n] = g_control_peer;
      roles[n++] = ap::kRoleNone;
      g_control_peer = 0;
    }
    if (g_pkgd_peer != 0) {  // второй хендл без роли — канал pkgd
      extra[n] = g_pkgd_peer;
      roles[n++] = ap::kRoleNone;
      g_pkgd_peer = 0;
    }
    o.give |= CHARM_GIVE_CONSOLE;
    o.job = job;
  } else {
    o.give |= CHARM_GIVE_NET;  // pkgd тоже: сеть — программам с правом net
  }
  o.extra = extra;
  o.extra_roles = roles;
  o.extra_count = n;
  int64_t p = charm_spawn(s.path, s.argv, nullptr, &o);
  if (p < 0) {
    printf("init: cannot start %s (%s): %s\n", s.name, s.path, strerror(errno));
    for (uint32_t k = 0; k < n; ++k) (void)charm::handle_close(extra[k]);
    if (job != 0) (void)charm::handle_close(job);
    return false;
  }
  s.proc = static_cast<Handle>(p);
  if (s.console && charm_console() != 0) (void)charm::console_set_foreground(charm_console(), job);
  if (job != 0) (void)charm::handle_close(job);  // держат ребёнок и консоль
  (void)charm::object_bind(g_port, s.proc, static_cast<uint64_t>(i));
  printf("init: started %s\n", s.name);
  return true;
}

void reply(const char* text) { (void)charm::channel_write(g_control, text, strlen(text), nullptr, 0); }

void power(uint64_t action);

void control() {
  char msg[128];
  Handle none[1];
  auto r = charm::channel_read(g_control, msg, sizeof msg - 1, none, 1, 0);
  if (!r.ok()) return;
  uint32_t len = static_cast<uint32_t>(r.value >> 32);
  msg[len] = '\0';
  if (strcmp(msg, "list") == 0) {
    char out[1024];
    size_t used = 0;
    for (int i = 0; i < g_count; ++i) {
      int n = snprintf(out + used, sizeof out - used, "%-8s %s%s\n", g_svc[i].name,
                       g_svc[i].proc != 0 ? "running" : "stopped", g_svc[i].console ? " (console)" : "");
      if (n > 0) used += static_cast<size_t>(n);
    }
    out[used] = '\0';
    reply(out);
  } else if (strncmp(msg, "kill ", 5) == 0) {
    for (int i = 0; i < g_count; ++i) {
      if (strcmp(g_svc[i].name, msg + 5) == 0 && g_svc[i].proc != 0 && !g_svc[i].console) {
        (void)charm_kill(g_svc[i].proc);
        reply("ok\n");
        return;
      }
    }
    reply("no such service\n");
  } else if (strcmp(msg, "poweroff") == 0) {
    power(abi::syscall::kPowerOff);
  } else if (strcmp(msg, "reboot") == 0) {
    power(abi::syscall::kPowerReboot);
  } else {
    reply("unknown command\n");
  }
}

void stop_services() {
  for (int i = 0; i < g_count; ++i) {
    if (g_svc[i].proc == 0) continue;
    printf("init: stopping %s\n", g_svc[i].name);
    (void)charm_kill(g_svc[i].proc);
    int64_t code;
    (void)charm_wait(g_svc[i].proc, &code);
    g_svc[i].proc = 0;
  }
}

// power.md §2: сначала службы, потом машина. Вернулись — не вышло.
void power(uint64_t action) {
  if (charm_power() == 0) {
    reply("no power handle\n");
    return;
  }
  printf("init: %s: stopping services\n", action == abi::syscall::kPowerOff ? "poweroff" : "reboot");
  reply("ok\n");
  stop_services();
  auto r = charm::syscall2(charm::Number::SystemPower, charm_power(), action);
  printf("init: system_power failed (%d)\n", static_cast<int>(r.status));
}

[[noreturn]] void shutdown() {
  printf("init: console session ended, stopping services\n");
  for (int i = 0; i < g_count; ++i) {
    if (g_svc[i].proc == 0) continue;
    (void)charm_kill(g_svc[i].proc);
    int64_t code;
    (void)charm_wait(g_svc[i].proc, &code);
  }
  printf("init: halt\n");
  exit(0);
}

}  // namespace

extern "C" int main(int, char**) {
  printf("init: CharmOS init\n");
  auto port = charm::port_create();
  if (!port.ok()) return 1;
  g_port = static_cast<Handle>(port.value);
  Handle ctl[2];
  if (charm::channel_create(ctl) == Status::Ok) {
    g_control = ctl[0];
    g_control_peer = ctl[1];
    (void)charm::object_bind(g_port, g_control, kControlKey);
  }
  if (!parse_conf()) {
    printf("init: no /etc/init.conf\n");
    return 1;
  }
  // logd — первым: остальным нужен журнал.
  for (int i = 0; i < g_count; ++i) {
    if (strcmp(g_svc[i].name, "logd") == 0) start(i);
  }
  for (int i = 0; i < g_count; ++i) {
    if (strcmp(g_svc[i].name, "logd") != 0) start(i);
  }
  for (;;) {
    charm::PortPacket pkt;
    Status s = charm::port_wait(g_port, &pkt, charm::kForever);
    if (s != Status::Ok) continue;
    if (pkt.key == kControlKey) {
      if (pkt.type == abi::syscall::kPacketChannelReadable) {
        control();
        (void)charm::object_bind(g_port, g_control, kControlKey);
      }
      continue;
    }
    if (pkt.type != abi::syscall::kPacketProcessExited || pkt.key >= static_cast<uint64_t>(g_count)) continue;
    Service& svc = g_svc[pkt.key];
    printf("init: %s exited with %ld\n", svc.name, static_cast<long>(static_cast<int64_t>(pkt.data0)));
    (void)charm::handle_close(svc.proc);
    svc.proc = 0;
    if (svc.console) shutdown();
    if (svc.restarts++ >= kMaxRestarts) {
      printf("init: %s failed %d times, not restarting\n", svc.name, svc.restarts);
      continue;
    }
    usleep(200'000);
    printf("init: restarting %s\n", svc.name);
    start(static_cast<int>(pkt.key));
  }
}
