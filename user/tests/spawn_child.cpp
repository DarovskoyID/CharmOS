// Ребёнок для теста запуска (docs/abi/process.md §2, §3). Лежит на диске
// (/bin/spawn_child), его запускает spawn_parent через process_spawn.
//   echo <код>  — отчёт о стартовом сообщении в канал (роль 0), выход с <код>
//   sleep       — спать без срока (его убивают)
//   spin        — крутиться без системных вызовов (лимит cpu_ns)
#include <charm/start.hpp>

#include "check.hpp"

using namespace charm;

namespace {

char g_buf[sizeof(abi::process::StartHeaderV2) + 4096 + 8192];

int64_t to_int(const char* s) {
  int64_t v = 0;
  for (; *s >= '0' && *s <= '9'; ++s) v = v * 10 + (*s - '0');
  return v;
}

}  // namespace

extern "C" int64_t charm_main(Handle boot) {
  Start st;
  EXPECT(read_start(boot, g_buf, sizeof g_buf, &st) == Status::Ok);
  const auto& h = st.header;
  EXPECT(h.argc >= 2);
  const char* mode = nth_string(st.args, 1);
  if (same(mode, static_cast<uint32_t>(length(mode)), "sleep")) {
    for (;;) sleep_until(kForever);
  }
  if (same(mode, static_cast<uint32_t>(length(mode)), "spin")) {
    for (volatile uint64_t i = 0;; i = i + 1) {
    }
  }
  EXPECT(same(mode, static_cast<uint32_t>(length(mode)), "echo") && h.argc == 3);
  // Отчёт: argv[0], окружение, роли, а корень ФС — рабочий.
  Handle reply = st.find(abi::process::kRoleNone);
  Handle root = st.find(abi::process::kRoleRoot);
  EXPECT(reply != 0 && root != 0);
  auto f = fs_open(root, "etc/hello.txt", 0);
  EXPECT(f.ok());
  char text[32] = {};
  auto rd = file_read(static_cast<Handle>(f.value), text, 5, 0);
  EXPECT(rd.ok() && rd.value == 5);
  char out[256];
  auto s = lib::core::format_to(out, sizeof out, "argv0={} envc={} env0={} env1={} handles={} file={}",
                                lib::core::StringView(st.args), h.envc,
                                lib::core::StringView(nth_string(st.env, 0)),
                                lib::core::StringView(nth_string(st.env, 1)), h.handle_count,
                                lib::core::StringView(text, 5));
  EXPECT(channel_write(reply, s.data(), s.size(), nullptr, 0) == Status::Ok);
  return to_int(nth_string(st.args, 2));
}
