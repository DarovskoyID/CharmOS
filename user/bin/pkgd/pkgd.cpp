// Служба пакетов (docs/abi/pkg.md §3) — единственный процесс с хендлом
// закрытой области. Запросы — по каналу (хендл без роли №0): текст команды;
// ответ — uint32_t status, затем текст (для run — ещё хендл Process).
//   install <путь .cpk> [+ хендл Job клиента]  — подпись магазина → закрытая
//       область; иначе — подтверждение пользователя на консоли → /apps корня
//   list | remove <id> | run <имя> [арг...] [+ хендл Job для запуска]
// При каждом запуске: подпись package.sig (для магазина) и SHA-512 исполняемого файла.
#include <abi/package.hpp>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <abi/process.hpp>
#include <charm/libc.h>
#include <charm/sys.hpp>
#include <lib/package/package.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "store_key.hpp"

namespace {

namespace pk = abi::package;
namespace ap = abi::process;
namespace fsabi = abi::fs;
namespace r = abi::syscall::rights;
using charm::Handle;
using charm::Status;
using lib::package::Error;

Handle g_root = 0, g_system = 0, g_console = 0, g_job = 0, g_net = 0;

// --- файлы по хендлу каталога -------------------------------------------------------------

Handle open_at(Handle dir, const char* path, uint64_t flags) {
  auto res = charm::fs_open(dir, path, strlen(path), flags);
  return res.ok() ? static_cast<Handle>(res.value) : 0;
}

// Каталоги по пути (mkdir -p) от dir; возвращает хендл последнего.
Handle make_dirs(Handle dir, const char* path) {
  char part[pk::kMaxPath + 64];
  size_t n = strlen(path);
  for (size_t i = 0; i <= n; ++i) {
    if (i == n || path[i] == '/') {
      memcpy(part, path, i);
      part[i] = '\0';
      Handle h = open_at(dir, part, fsabi::kOpenDirectory | fsabi::kOpenCreate | fsabi::kOpenWrite);
      if (h == 0) return 0;
      if (i == n) return h;
      (void)charm::handle_close(h);
    }
  }
  return 0;
}

bool write_all(Handle dir, const char* path, const void* data, uint64_t size) {
  Handle h = open_at(dir, path, fsabi::kOpenWrite | fsabi::kOpenCreate | fsabi::kOpenTruncate);
  if (h == 0) return false;
  bool ok = true;
  for (uint64_t done = 0; done < size && ok;) {
    uint64_t chunk = size - done < fsabi::kMaxTransfer ? size - done : fsabi::kMaxTransfer;
    auto w = charm::file_write(h, static_cast<const uint8_t*>(data) + done, chunk, done);
    ok = w.ok() && w.value != 0;
    if (ok) done += w.value;
  }
  ok = ok && charm::file_sync(h) == Status::Ok;
  (void)charm::handle_close(h);
  return ok;
}

// Файл целиком в кучу (nullptr — нет/ошибка).
uint8_t* read_all(Handle file, uint64_t* size) {
  abi::fs::Stat st;
  if (charm::fs_stat(file, &st) != Status::Ok || st.size > (256u << 20)) return nullptr;
  auto* buf = static_cast<uint8_t*>(malloc(st.size != 0 ? st.size : 1));
  if (buf == nullptr) return nullptr;
  for (uint64_t done = 0; done < st.size;) {
    uint64_t chunk = st.size - done < fsabi::kMaxTransfer ? st.size - done : fsabi::kMaxTransfer;
    auto rd = charm::file_read(file, buf + done, chunk, done);
    if (!rd.ok() || rd.value == 0) {
      free(buf);
      return nullptr;
    }
    done += rd.value;
  }
  *size = st.size;
  return buf;
}

uint8_t* read_path(Handle dir, const char* path, uint64_t* size) {
  Handle h = open_at(dir, path, 0);
  if (h == 0) return nullptr;
  uint8_t* d = read_all(h, size);
  (void)charm::handle_close(h);
  return d;
}

// --- консоль: подтверждение пользователя (pkg.md §3) ----------------------------------------

void say(const char* text) {
  if (g_console != 0) (void)charm::console_write(g_console, text, strlen(text));
}

// Передний план — себе; y/n; затем — обратно клиенту.
bool confirm(const char* question, Handle client_job) {
  if (g_console == 0 || g_job == 0) return false;
  if (charm::console_set_foreground(g_console, g_job) != Status::Ok) return false;
  say(question);
  say(" [y/N] ");
  char c = 0, answer = 0;
  uint64_t deadline = charm::now_ns() + 60'000'000'000ull;
  for (;;) {
    auto rd = charm::console_read(g_console, &c, 1, deadline);
    if (!rd.ok()) break;
    if (c == '\r' || c == '\n') break;
    if (answer == 0 && c >= ' ') {
      answer = c;
      (void)charm::console_write(g_console, &c, 1);
    }
  }
  say("\n");
  if (client_job != 0) (void)charm::console_set_foreground(g_console, client_job);
  return answer == 'y' || answer == 'Y';
}

void perm_text(uint32_t p, char* out, size_t cap) {
  out[0] = '\0';
  auto add = [&](uint32_t bit, const char* text) {
    if ((p & bit) == 0) return;
    if (out[0] != '\0') strncat(out, ", ", cap - strlen(out) - 1);
    strncat(out, text, cap - strlen(out) - 1);
  };
  add(pk::kPermNet, "network");
  add(pk::kPermNetListen, "accept network connections");
  add(pk::kPermFiles, "its own data folder");
  add(pk::kPermHome, "ALL your files (/home)");
  add(pk::kPermConsole, "the console");
  if (out[0] == '\0') snprintf(out, cap, "none");
}

// --- установка ------------------------------------------------------------------------------

struct Reply {
  Status status;
  char text[512];
};

void install_data(uint8_t* data, uint64_t size, Handle client_job, Reply* rep);

void install(const char* path, Handle client_job, Reply* rep) {
  uint64_t size = 0;
  uint8_t* data = read_path(g_root, path[0] == '/' ? path + 1 : path, &size);
  if (data == nullptr) {
    rep->status = Status::NotFound;
    snprintf(rep->text, sizeof rep->text, "cannot read %s", path);
    return;
  }
  install_data(data, size, client_job, rep);
}

// HTTP/1.1 GET (RFC 9112 §3, §6): http://<IPv4>[:порт]/путь, ответ 200 с телом.
// TLS нет — только тестовый сервер M8.5 (pkg.md §3).
uint8_t* http_get(const char* url, uint64_t* size, char* err, size_t errcap) {
  if (strncmp(url, "http://", 7) != 0) {
    snprintf(err, errcap, "only http:// URLs");
    return nullptr;
  }
  char host[64];
  const char* p = url + 7;
  size_t hl = strcspn(p, ":/");
  if (hl == 0 || hl >= sizeof host) {
    snprintf(err, errcap, "bad URL");
    return nullptr;
  }
  memcpy(host, p, hl);
  host[hl] = '\0';
  p += hl;
  int port = 80;
  if (*p == ':') {
    port = atoi(p + 1);
    p += 1 + strspn(p + 1, "0123456789");
  }
  const char* path = *p == '/' ? p : "/";
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(static_cast<uint16_t>(port));
  if (inet_pton(AF_INET, host, &a.sin_addr) != 1) {
    snprintf(err, errcap, "host must be an IPv4 address");
    return nullptr;
  }
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0 || connect(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
    snprintf(err, errcap, "cannot connect to %s:%d", host, port);
    if (s >= 0) close(s);
    return nullptr;
  }
  char req[600];
  int n = snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
  (void)write(s, req, static_cast<size_t>(n));
  uint64_t cap = 1 << 20, len = 0;
  auto* buf = static_cast<uint8_t*>(malloc(cap));
  for (;;) {
    if (buf == nullptr) break;
    if (len == cap) {
      cap *= 2;
      buf = static_cast<uint8_t*>(realloc(buf, cap));
      if (buf == nullptr) break;
    }
    ssize_t got = read(s, buf + len, cap - len);
    if (got <= 0) break;
    len += static_cast<uint64_t>(got);
  }
  close(s);
  if (buf == nullptr) {
    snprintf(err, errcap, "out of memory");
    return nullptr;
  }
  // Статус-строка и конец заголовков.
  uint64_t body = 0;
  for (uint64_t i = 0; i + 3 < len; ++i) {
    if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
      body = i + 4;
      break;
    }
  }
  if (body == 0 || len < 12 || memcmp(buf, "HTTP/1.", 7) != 0 || memcmp(buf + 9, "200", 3) != 0) {
    snprintf(err, errcap, "HTTP error (%.12s)", len >= 12 ? reinterpret_cast<char*>(buf) : "no response");
    free(buf);
    return nullptr;
  }
  memmove(buf, buf + body, len - body);
  *size = len - body;
  return buf;
}

void install_url(const char* url, Handle client_job, Reply* rep) {
  uint64_t size = 0;
  char err[128];
  uint8_t* data = http_get(url, &size, err, sizeof err);
  if (data == nullptr) {
    rep->status = Status::IoError;
    snprintf(rep->text, sizeof rep->text, "download failed: %s", err);
    return;
  }
  install_data(data, size, client_job, rep);
}

void install_data(uint8_t* data, uint64_t size, Handle client_job, Reply* rep) {
  auto v = lib::package::open(data, size);
  if (v.is_err()) {
    rep->status = Status::InvalidArgs;
    snprintf(rep->text, sizeof rep->text, "rejected: %s", lib::package::error_name(v.error()));
    free(data);
    return;
  }
  const lib::package::View& pv = v.value();
  Error files = lib::package::check_files(pv);
  Error sig = lib::package::check_signature(pv, kStorePublicKey);
  bool store = sig == Error::Ok;
  // Подпись есть, но не сходится — пакет испорчен: отвергаем всегда.
  if (files != Error::Ok || sig == Error::BadSignature) {
    rep->status = Status::AccessDenied;
    snprintf(rep->text, sizeof rep->text, "rejected: %s",
             lib::package::error_name(files != Error::Ok ? files : sig));
    charm_log_write(4, rep->text);
    free(data);
    return;
  }
  const lib::package::Manifest& m = pv.manifest;
  if (!store) {
    char perms[160], q[512];
    perm_text(m.permissions, perms, sizeof perms);
    snprintf(q, sizeof q,
             "\n*** %s %s (%s) is NOT from the CharmOS store.\n*** It asks for: %s.\n*** Install it outside the "
             "system area?",
             m.name, m.version, m.id, perms);
    if (!confirm(q, client_job)) {
      rep->status = Status::AccessDenied;
      snprintf(rep->text, sizeof rep->text, "cancelled by user");
      free(data);
      return;
    }
  }
  Handle area = store ? g_system : g_root;
  char dir[128];
  snprintf(dir, sizeof dir, "apps/%s", m.id);
  Handle app = area != 0 ? make_dirs(area, dir) : 0;
  bool ok = app != 0;
  for (uint32_t i = 0; ok && i < pv.header->file_count; ++i) {
    const pk::FileEntry& f = pv.files[i];
    const char* slash = strrchr(f.path, '/');
    if (slash != nullptr) {
      char sub[pk::kMaxPath];
      memcpy(sub, f.path, static_cast<size_t>(slash - f.path));
      sub[slash - f.path] = '\0';
      Handle d = make_dirs(app, sub);
      ok = d != 0;
      if (d != 0) (void)charm::handle_close(d);
    }
    ok = ok && write_all(app, f.path, data + f.offset, f.size);
  }
  // package.sig: подписанная часть и блок подписи (package.md §7).
  if (ok) {
    uint64_t head = pv.header->data_offset;
    uint64_t meta_size = head + (pv.signature != nullptr ? pk::kSignatureBlock : 0);
    auto* meta = static_cast<uint8_t*>(malloc(meta_size));
    ok = meta != nullptr;
    if (ok) {
      memcpy(meta, data, head);
      if (pv.signature != nullptr) memcpy(meta + head, pv.signature, pk::kSignatureBlock);
      ok = write_all(app, "package.sig", meta, meta_size);
      free(meta);
    }
  }
  if (app != 0) (void)charm::handle_close(app);
  rep->status = ok ? Status::Ok : Status::IoError;
  snprintf(rep->text, sizeof rep->text, ok ? "installed %s %s (%s)" : "install of %s %s failed (%s)", m.id,
           m.version, store ? "store, system area" : "not from store");
  charm_log_write(6, rep->text);
  free(data);
}

// --- поиск и запуск -----------------------------------------------------------------------

// Каталог установленной программы по id или короткому имени (последняя часть id).
Handle find_app(const char* name, bool* store, char* id, size_t cap) {
  Handle areas[2] = {g_system, g_root};
  for (int a = 0; a < 2; ++a) {
    if (areas[a] == 0) continue;
    Handle apps = open_at(areas[a], "apps", fsabi::kOpenDirectory);
    if (apps == 0) continue;
    uint8_t buf[2048];
    Handle found = 0;
    for (uint64_t cookie = 0; found == 0;) {
      auto rd = charm::dir_read(apps, buf, sizeof buf, cookie);
      if (!rd.ok()) break;
      uint32_t off = 0;
      bool any = false;
      while (off + sizeof(fsabi::DirEntryHeader) <= sizeof buf) {
        auto* h = reinterpret_cast<fsabi::DirEntryHeader*>(buf + off);
        if (h->record_len == 0) break;
        any = true;
        char ent[256];
        memcpy(ent, buf + off + sizeof *h, h->name_len);
        ent[h->name_len] = '\0';
        const char* dot = strrchr(ent, '.');
        const char* shortname = dot != nullptr ? dot + 1 : ent;
        if (strcmp(ent, name) == 0 || strcmp(shortname, name) == 0) {
          found = open_at(apps, ent, fsabi::kOpenDirectory);
          snprintf(id, cap, "%s", ent);
          *store = a == 0;
          break;
        }
        off += h->record_len;
      }
      if (!any) break;
      cookie = rd.value;
    }
    (void)charm::handle_close(apps);
    if (found != 0) return found;
  }
  return 0;
}

Handle dup_or_zero(Handle h, uint32_t rights) {
  if (h == 0) return 0;
  auto d = charm::handle_duplicate(h, rights);
  return d.ok() ? static_cast<Handle>(d.value) : 0;
}

void run(char* args, Handle client_job, Reply* rep, Handle* out_proc) {
  char* save;
  char* name = strtok_r(args, " ", &save);
  if (name == nullptr) {
    rep->status = Status::InvalidArgs;
    snprintf(rep->text, sizeof rep->text, "usage: run <app>");
    return;
  }
  bool store = false;
  char id[128];
  Handle app = find_app(name, &store, id, sizeof id);
  if (app == 0) {
    rep->status = Status::NotFound;
    snprintf(rep->text, sizeof rep->text, "%s: not installed", name);
    return;
  }
  uint64_t meta_size = 0;
  uint8_t* meta = read_path(app, "package.sig", &meta_size);
  auto v = meta != nullptr ? lib::package::open_meta(meta, meta_size)
                           : lib::core::Result<lib::package::View, Error>::err(Error::Truncated);
  Error why = v.is_err() ? v.error() : Error::Ok;
  if (why == Error::Ok && store) why = lib::package::check_signature(v.value(), kStorePublicKey);
  Handle exe = 0;
  if (why == Error::Ok) {
    const pk::FileEntry* f = lib::package::find(v.value(), v.value().manifest.exec);
    exe = f != nullptr ? open_at(app, f->path, 0) : 0;
    uint64_t esize = 0;
    uint8_t* ed = exe != 0 ? read_all(exe, &esize) : nullptr;
    if (f == nullptr || ed == nullptr || !lib::package::check_file(*f, ed, esize)) why = Error::BadHash;
    free(ed);
  }
  if (why != Error::Ok) {
    rep->status = Status::AccessDenied;
    snprintf(rep->text, sizeof rep->text, "%s: verification failed (%s), not started", id,
             lib::package::error_name(why));
    charm_log_write(4, rep->text);
    if (exe != 0) (void)charm::handle_close(exe);
    (void)charm::handle_close(app);
    free(meta);
    return;
  }
  // Права из манифеста (package.md §4).
  const lib::package::Manifest& m = v.value().manifest;
  uint32_t handles[ap::kMaxStartHandles], roles[ap::kMaxStartHandles];
  uint32_t n = 0;
  auto give = [&](Handle h, uint32_t role) {
    if (h != 0 && n < ap::kMaxStartHandles) {
      handles[n] = h;
      roles[n++] = role;
    }
  };
  if ((m.permissions & pk::kPermHome) != 0) {
    give(make_dirs(g_root, "home"), ap::kRoleRoot);
  } else if ((m.permissions & pk::kPermFiles) != 0) {
    char data_dir[160];
    snprintf(data_dir, sizeof data_dir, "home/apps/%s", id);
    give(make_dirs(g_root, data_dir), ap::kRoleRoot);
  } else {
    give(dup_or_zero(app, r::kDuplicate | r::kTransfer | r::kRead), ap::kRoleRoot);  // только свои файлы, чтение
  }
  if ((m.permissions & pk::kPermConsole) != 0) {
    give(dup_or_zero(g_console, r::kDuplicate | r::kTransfer | r::kRead | r::kWrite), ap::kRoleConsole);
  }
  if ((m.permissions & (pk::kPermNet | pk::kPermNetListen)) != 0) {
    uint32_t nr = r::kDuplicate | r::kTransfer | r::kNetConnect | r::kNetResolve;
    if ((m.permissions & pk::kPermNetListen) != 0) nr |= r::kNetListen;
    give(dup_or_zero(g_net, nr), ap::kRoleNet);
  }
  // Аргументы: имя, затем остальное из запроса.
  char argbuf[1024];
  size_t used = 0;
  uint32_t argc = 0;
  auto addarg = [&](const char* a) {
    size_t l = strlen(a) + 1;
    if (used + l > sizeof argbuf) return;
    memcpy(argbuf + used, a, l);
    used += l;
    ++argc;
  };
  addarg(name);
  while (char* a = strtok_r(nullptr, " ", &save)) addarg(a);
  ap::SpawnArgs sa{};
  sa.executable = exe;
  sa.handle_count = n;
  sa.handles = handles;
  sa.roles = roles;
  sa.args = argbuf;
  sa.args_bytes = static_cast<uint32_t>(used);
  sa.argc = argc;
  const char* shortname = strrchr(id, '.');
  shortname = shortname != nullptr ? shortname + 1 : id;
  sa.name = shortname;
  sa.name_len = static_cast<uint32_t>(strnlen(shortname, ap::kMaxName));
  sa.job = client_job;
  auto res = charm::process_spawn(&sa);
  (void)charm::handle_close(exe);
  (void)charm::handle_close(app);
  free(meta);
  if (!res.ok()) {
    for (uint32_t i = 0; i < n; ++i) (void)charm::handle_close(handles[i]);
    rep->status = res.status;
    snprintf(rep->text, sizeof rep->text, "%s: cannot start", id);
    return;
  }
  *out_proc = static_cast<Handle>(res.value);
  rep->status = Status::Ok;
  snprintf(rep->text, sizeof rep->text, "%s: started (%s)", id, store ? "store" : "not from store");
}

void remove(const char* name, Handle client_job, Reply* rep) {
  bool store = false;
  char id[128];
  Handle app = find_app(name, &store, id, sizeof id);
  if (app == 0) {
    rep->status = Status::NotFound;
    snprintf(rep->text, sizeof rep->text, "%s: not installed", name);
    return;
  }
  char q[256];
  snprintf(q, sizeof q, "\n*** Remove %s%s?", id, store ? " (from the store)" : "");
  if (!confirm(q, client_job)) {
    (void)charm::handle_close(app);
    rep->status = Status::AccessDenied;
    snprintf(rep->text, sizeof rep->text, "cancelled by user");
    return;
  }
  uint64_t meta_size = 0;
  uint8_t* meta = read_path(app, "package.sig", &meta_size);
  auto v = meta != nullptr ? lib::package::open_meta(meta, meta_size)
                           : lib::core::Result<lib::package::View, Error>::err(Error::Truncated);
  (void)charm::handle_close(app);
  Handle apps = open_at(store ? g_system : g_root, "apps", fsabi::kOpenDirectory | fsabi::kOpenWrite);
  bool ok = apps != 0 && v.is_ok();
  char path[pk::kMaxPath + 140];
  // Файлы, затем их каталоги — от глубоких к мелким (unlink непустого — Busy).
  for (uint32_t i = 0; ok && i < v.value().header->file_count; ++i) {
    snprintf(path, sizeof path, "%s/%s", id, v.value().files[i].path);
    ok = charm::fs_unlink(apps, path) == Status::Ok;
  }
  for (int pass = 0; ok && pass < 8; ++pass) {
    for (uint32_t i = 0; i < v.value().header->file_count; ++i) {
      snprintf(path, sizeof path, "%s/%s", id, v.value().files[i].path);
      for (int up = 0; up <= pass; ++up) {
        char* slash = strrchr(path, '/');
        if (slash == nullptr) break;
        *slash = '\0';
      }
      if (strchr(path, '/') != nullptr) (void)charm::fs_unlink(apps, path);
    }
  }
  if (ok) {
    snprintf(path, sizeof path, "%s/package.sig", id);
    ok = charm::fs_unlink(apps, path) == Status::Ok && charm::fs_unlink(apps, id) == Status::Ok;
  }
  if (apps != 0) (void)charm::handle_close(apps);
  free(meta);
  rep->status = ok ? Status::Ok : Status::IoError;
  snprintf(rep->text, sizeof rep->text, ok ? "removed %s" : "remove of %s failed", id);
  charm_log_write(6, rep->text);
}

void list(Reply* rep) {
  rep->text[0] = '\0';
  Handle areas[2] = {g_system, g_root};
  for (int a = 0; a < 2; ++a) {
    Handle apps = areas[a] != 0 ? open_at(areas[a], "apps", fsabi::kOpenDirectory) : 0;
    if (apps == 0) continue;
    uint8_t buf[2048];
    auto rd = charm::dir_read(apps, buf, sizeof buf, 0);
    for (uint32_t off = 0; rd.ok() && off + 4 <= sizeof buf;) {
      auto* h = reinterpret_cast<fsabi::DirEntryHeader*>(buf + off);
      if (h->record_len == 0) break;
      char line[300];
      snprintf(line, sizeof line, "%.*s  (%s)\n", h->name_len, reinterpret_cast<char*>(buf + off + 4),
               a == 0 ? "store" : "not from store");
      strncat(rep->text, line, sizeof rep->text - strlen(rep->text) - 1);
      off += h->record_len;
    }
    (void)charm::handle_close(apps);
  }
  if (rep->text[0] == '\0') snprintf(rep->text, sizeof rep->text, "no programs installed\n");
  rep->status = Status::Ok;
}

}  // namespace

extern "C" int main(int, char**) {
  g_root = charm_root();
  g_system = charm_system();
  g_console = charm_console();
  g_job = charm_job();
  g_net = charm_net();
  Handle req = charm_extra_handle(0);
  printf("pkgd: %s\n", g_system != 0 ? "system area available" : "NO system area (store installs disabled)");
  if (req == 0) return 1;
  for (;;) {
    char msg[1024];
    Handle hs[1];
    auto rd = charm::channel_read(req, msg, sizeof msg - 1, hs, 1, charm::kForever);
    if (rd.status == Status::Interrupted) continue;
    if (!rd.ok()) return rd.status == Status::PeerClosed ? 0 : 1;
    uint32_t len = static_cast<uint32_t>(rd.value >> 32);
    uint32_t nh = static_cast<uint32_t>(rd.value);
    msg[len] = '\0';
    Handle client_job = nh == 1 ? hs[0] : 0;
    Reply rep{Status::InvalidArgs, "unknown request"};
    Handle proc = 0;
    if (strncmp(msg, "install-url ", 12) == 0) {
      install_url(msg + 12, client_job, &rep);
    } else if (strncmp(msg, "remove ", 7) == 0) {
      remove(msg + 7, client_job, &rep);
    } else if (strncmp(msg, "install ", 8) == 0) {
      install(msg + 8, client_job, &rep);
    } else if (strncmp(msg, "run ", 4) == 0) {
      run(msg + 4, client_job, &rep, &proc);
    } else if (strcmp(msg, "list") == 0) {
      list(&rep);
    }
    char out[4 + sizeof rep.text];
    uint32_t st = static_cast<uint32_t>(rep.status);
    memcpy(out, &st, 4);
    size_t tl = strlen(rep.text);
    memcpy(out + 4, rep.text, tl);
    (void)charm::channel_write(req, out, 4 + tl, proc != 0 ? &proc : nullptr, proc != 0 ? 1 : 0);
    if (client_job != 0) (void)charm::handle_close(client_job);
  }
}
