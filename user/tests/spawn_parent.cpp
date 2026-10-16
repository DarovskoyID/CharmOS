// Запуск процессов (docs/abi/process.md §2–§5): стартовое сообщение "strt"
// от ядра (роли Root и Job), process_spawn ребёнка с диска с аргументами,
// окружением и хендлами, ожидание через порт, process_info, process_kill,
// Job (job_create, job_info, job_kill, пакет JobEmpty), ошибки.
#include <charm/start.hpp>

#include "check.hpp"

using namespace charm;
namespace ap = abi::process;

namespace {

char g_buf[sizeof(ap::StartHeader) + 4096 + 8192];

const char kArgsEcho[] = "spawn_child\0echo\0" "42";  // + завершающий \0 литерала
const char kArgsSleep[] = "spawn_child\0sleep";
const char kEnv[] = "A=1\0B=two";

SysResult spawn(Handle exe, const char* args, uint32_t args_bytes, uint32_t argc, const Handle* hs,
                const uint32_t* roles, uint32_t n, Handle job, bool v2 = false) {
  ap::SpawnArgs a{};
  a.executable = exe;
  a.handle_count = n;
  a.handles = hs;
  a.roles = roles;
  a.args = args;
  a.args_bytes = args_bytes;
  a.argc = argc;
  a.name = "child";
  a.name_len = 5;
  a.env = kEnv;
  a.env_bytes = sizeof kEnv;
  a.envc = 2;
  a.job = job;
  a.flags = v2 ? ap::kSpawnStartV2 : 0;
  return process_spawn(&a);
}

// Ждёт пакет на порту с ключом key; возвращает data0.
bool wait_packet(Handle port, uint64_t key, uint32_t type, uint64_t* data0) {
  PortPacket pkt;
  if (port_wait(port, &pkt, now_ns() + 30'000'000'000ull) != Status::Ok) return false;
  *data0 = pkt.data0;
  return pkt.key == key && pkt.type == type;
}

}  // namespace

extern "C" int64_t charm_main(Handle boot) {
  Start st;
  EXPECT(read_start(boot, g_buf, sizeof g_buf, &st) == Status::Ok);
  EXPECT(st.header.argc == 1 && same(st.args, 12, "spawn_parent"));
  Handle root = st.find(ap::kRoleRoot);
  Handle job = st.find(ap::kRoleJob);
  EXPECT(root != 0 && job != 0);

  auto exe = fs_open(root, "bin/spawn_child", 0);
  EXPECT(exe.ok());
  Handle exe_h = static_cast<Handle>(exe.value);
  auto port = port_create();
  EXPECT(port.ok());
  Handle port_h = static_cast<Handle>(port.value);

  // 1. echo: аргументы, окружение, два хендла (канал и копия корня).
  Handle ch[2];
  EXPECT(channel_create(ch) == Status::Ok);
  auto root2 = handle_duplicate(root, rights::kDuplicate | rights::kTransfer | rights::kRead);
  EXPECT(root2.ok());
  Handle give[2] = {ch[1], static_cast<Handle>(root2.value)};
  uint32_t roles[2] = {ap::kRoleNone, ap::kRoleRoot};
  auto child = spawn(exe_h, kArgsEcho, sizeof kArgsEcho, 3, give, roles, 2, 0);
  EXPECT(child.ok());
  Handle child_h = static_cast<Handle>(child.value);
  EXPECT(handle_close(ch[1]) == Status::BadHandle);  // перемещён ребёнку
  char reply[256];
  Handle none[1];
  uint32_t n, nh;
  EXPECT(read_msg(ch[0], reply, sizeof reply - 1, none, 1, &n, &nh) == Status::Ok);
  char line[300];
  auto said = lib::core::format_to(line, sizeof line, "spawn: child says {}\n", lib::core::StringView(reply, n));
  debug_write(said.data(), said.size());
  EXPECT(object_bind(port_h, child_h, 1) == Status::Ok);
  uint64_t code;
  EXPECT(wait_packet(port_h, 1, abi::syscall::kPacketProcessExited, &code));
  EXPECT(static_cast<int64_t>(code) == 42);
  ap::ProcessInfo info;
  EXPECT(process_info(child_h, &info) == Status::Ok);
  EXPECT(info.state == ap::kProcessExited && info.exit_code == 42 && info.pid != 0);
  // Уже завершён: подписка срабатывает сразу.
  EXPECT(object_bind(port_h, child_h, 2) == Status::Ok);
  EXPECT(wait_packet(port_h, 2, abi::syscall::kPacketProcessExited, &code) && code == 42);

  // 2. process_kill: спящий ребёнок → -1000.
  auto sleeper = spawn(exe_h, kArgsSleep, sizeof kArgsSleep, 2, nullptr, nullptr, 0, 0);
  EXPECT(sleeper.ok());
  Handle sleeper_h = static_cast<Handle>(sleeper.value);
  EXPECT(process_info(sleeper_h, &info) == Status::Ok && info.state == ap::kProcessRunning);
  EXPECT(object_bind(port_h, sleeper_h, 3) == Status::Ok);
  EXPECT(process_kill(sleeper_h) == Status::Ok);
  EXPECT(wait_packet(port_h, 3, abi::syscall::kPacketProcessExited, &code));
  EXPECT(static_cast<int64_t>(code) == ap::kExitKilled);

  // 3. Job: двое в новом Job, job_info, job_kill, JobEmpty.
  auto sub = job_create(job);
  EXPECT(sub.ok());
  Handle sub_h = static_cast<Handle>(sub.value);
  auto c1 = spawn(exe_h, kArgsSleep, sizeof kArgsSleep, 2, nullptr, nullptr, 0, sub_h);
  auto c2 = spawn(exe_h, kArgsSleep, sizeof kArgsSleep, 2, nullptr, nullptr, 0, sub_h);
  EXPECT(c1.ok() && c2.ok());
  ap::JobInfo ji;
  EXPECT(job_info(sub_h, &ji) == Status::Ok);
  EXPECT(ji.processes == 2 && ji.threads == 2);
  EXPECT(object_bind(port_h, sub_h, 4) == Status::Ok);
  EXPECT(job_kill(sub_h) == Status::Ok);
  EXPECT(wait_packet(port_h, 4, abi::syscall::kPacketJobEmpty, &code));
  EXPECT(process_info(static_cast<Handle>(c1.value), &info) == Status::Ok);
  EXPECT(info.state == ap::kProcessExited && info.exit_code == ap::kExitKilled);
  EXPECT(job_info(sub_h, &ji) == Status::Ok && ji.processes == 0 && ji.threads == 0);

  // 4. Ошибки: хендлы при ошибке остаются у вызывающего.
  Handle keep[1];
  EXPECT(channel_create(ch) == Status::Ok);
  keep[0] = ch[1];
  uint32_t role0[1] = {0};
  EXPECT(spawn(root, kArgsSleep, sizeof kArgsSleep, 2, keep, role0, 1, 0).status == Status::WrongType);
  EXPECT(spawn(exe_h, kArgsSleep, sizeof kArgsSleep, 3, keep, role0, 1, 0).status == Status::InvalidArgs);
  EXPECT(handle_close(ch[1]) == Status::Ok);  // остался у нас
  auto data = fs_open(root, "etc/hello.txt", 0);
  EXPECT(data.ok());
  EXPECT(spawn(static_cast<Handle>(data.value), kArgsSleep, sizeof kArgsSleep, 2, nullptr, nullptr, 0, 0).status ==
         Status::InvalidArgs);  // не ELF
  // Both wire formats are exercised: the legacy two-handle echo above and
  // all sixteen v2 slots here. Rejecting the legacy oversized request must
  // leave every handle available for the following v2 transfer.
  EXPECT(channel_create(ch) == Status::Ok);
  Handle many[16]{}; uint32_t many_roles[16]{}; many[0] = ch[1];
  for (uint32_t i = 1; i < 16; ++i) {
    auto copy = handle_duplicate(root, rights::kDuplicate | rights::kTransfer | rights::kRead);
    EXPECT(copy.ok()); many[i] = static_cast<Handle>(copy.value); many_roles[i] = ap::kRoleRoot;
  }
  EXPECT(spawn(exe_h, kArgsEcho, sizeof kArgsEcho, 3, many, many_roles, 16, 0).status == Status::InvalidArgs);
  auto wide = spawn(exe_h, kArgsEcho, sizeof kArgsEcho, 3, many, many_roles, 16, 0, true);
  EXPECT(wide.ok());
  EXPECT(read_msg(ch[0], reply, sizeof reply - 1, none, 1, &n, &nh) == Status::Ok);
  reply[n] = 0;
  EXPECT(same(reply, n, "argv0=spawn_child envc=2 env0=A=1 env1=B=two handles=16 file=hello"));
  EXPECT(object_bind(port_h, static_cast<Handle>(wide.value), 10) == Status::Ok);
  EXPECT(wait_packet(port_h, 10, abi::syscall::kPacketProcessExited, &code) && code == 42);
  print("spawn: v1 and v2 (16 handles) verified\n");
  print("spawn: ok\n");
  return 0;
}
