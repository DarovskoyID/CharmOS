// M6: файловые вызовы ABI v1.3 из ring 3 (docs/abi/fs.md). Корень приходит
// сообщением "root" в стартовом канале. Образ диска — tools/qemu/fs_*.sh:
// в корне /etc/hello.txt. В конце оставляет /persist.txt для теста
// перезагрузки (fs_check).
#include "check.hpp"

using namespace charm;
namespace fsabi = abi::fs;

namespace {

uint8_t g_big[200 * 1024];
uint8_t g_back[200 * 1024];

bool has_entry(Handle dir, const char* want) {
  uint8_t buf[512];
  for (uint64_t cookie = 0;;) {
    auto r = dir_read(dir, buf, sizeof buf, cookie);
    if (!r.ok()) return false;
    uint32_t off = 0;
    bool any = false;
    while (off + 4 <= sizeof buf) {
      auto* h = reinterpret_cast<fsabi::DirEntryHeader*>(buf + off);
      if (h->record_len == 0) break;
      any = true;
      if (same(reinterpret_cast<const char*>(buf + off + 4), h->name_len, want)) return true;
      off += h->record_len;
    }
    if (!any) return false;
    cookie = r.value;
  }
}

}  // namespace

extern "C" int64_t charm_main(Handle start) {
  char msg[8];
  Handle hs[1];
  uint32_t n, nh;
  EXPECT(read_msg(start, msg, sizeof msg, hs, 1, &n, &nh) == Status::Ok);
  EXPECT(same(msg, n, "root") && nh == 1);
  Handle root = hs[0];

  // Файл, положенный mkdisk.
  auto hello = fs_open(root, "etc/hello.txt", 0);
  EXPECT(hello.ok());
  char text[64];
  auto rd = file_read(static_cast<Handle>(hello.value), text, sizeof text, 0);
  EXPECT(rd.ok() && same(text, static_cast<uint32_t>(rd.value), "hello from mkdisk\n"));
  EXPECT(file_write(static_cast<Handle>(hello.value), "x", 1, 0).status == Status::AccessDenied);

  // Ошибки путей и прав.
  EXPECT(fs_open(root, "etc/../etc/hello.txt", 0).status == Status::InvalidArgs);
  EXPECT(fs_open(root, "/etc/hello.txt", 0).status == Status::InvalidArgs);
  EXPECT(fs_open(root, "etc/nope", 0).status == Status::NotFound);
  EXPECT(fs_open(root, "etc", 0).status == Status::WrongType);
  auto ro = handle_duplicate(root, rights::kRead);
  EXPECT(ro.ok());
  EXPECT(fs_open(static_cast<Handle>(ro.value), "new", fsabi::kOpenCreate | fsabi::kOpenWrite).status ==
         Status::AccessDenied);

  // Каталог и большой файл: запись, sync, чтение, размер.
  auto work = fs_open(root, "work", fsabi::kOpenCreate | fsabi::kOpenDirectory);
  EXPECT(work.ok());
  Handle wd = static_cast<Handle>(work.value);
  auto f = fs_open(wd, "data.bin", fsabi::kOpenCreate | fsabi::kOpenWrite | fsabi::kOpenExclusive);
  EXPECT(f.ok());
  Handle fh = static_cast<Handle>(f.value);
  for (uint32_t i = 0; i < sizeof g_big; ++i) g_big[i] = static_cast<uint8_t>(i * 13 + (i >> 10));
  auto w = file_write(fh, g_big, sizeof g_big, 0);
  EXPECT(w.ok() && w.value == sizeof g_big);
  EXPECT(file_sync(fh) == Status::Ok);
  auto r = file_read(fh, g_back, sizeof g_back, 0);
  EXPECT(r.ok() && r.value == sizeof g_back);
  for (uint32_t i = 0; i < sizeof g_big; ++i) EXPECT(g_back[i] == g_big[i]);
  fsabi::Stat st;
  EXPECT(fs_stat(fh, &st) == Status::Ok && st.size == sizeof g_big && st.type == fsabi::kTypeFile);
  EXPECT(file_resize(fh, 1000) == Status::Ok);
  EXPECT(fs_stat(fh, &st) == Status::Ok && st.size == 1000);
  EXPECT(fs_open(wd, "data.bin", fsabi::kOpenCreate | fsabi::kOpenExclusive).status ==
         Status::AlreadyExists);
  EXPECT(has_entry(root, "work") && has_entry(wd, "data.bin"));

  // Удаление: открытый файл — Busy; закрытый — да; затем каталог.
  EXPECT(fs_unlink(root, "work") == Status::Busy);  // непустой и открытый
  EXPECT(fs_unlink(wd, "data.bin") == Status::Busy);
  EXPECT(handle_close(fh) == Status::Ok);
  EXPECT(fs_unlink(wd, "data.bin") == Status::Ok);
  EXPECT(fs_open(wd, "data.bin", 0).status == Status::NotFound);
  EXPECT(handle_close(wd) == Status::Ok);
  EXPECT(fs_unlink(root, "work") == Status::Ok);
  EXPECT(!has_entry(root, "work"));

  // Для теста перезагрузки.
  auto p = fs_open(root, "persist.txt", fsabi::kOpenCreate | fsabi::kOpenWrite | fsabi::kOpenTruncate);
  EXPECT(p.ok());
  const char note[] = "survives reboot";
  EXPECT(file_write(static_cast<Handle>(p.value), note, sizeof note - 1, 0).ok());
  EXPECT(file_sync(static_cast<Handle>(p.value)) == Status::Ok);
  auto bulk = fs_open(root, "persist-bulk.bin", fsabi::kOpenCreate | fsabi::kOpenWrite | fsabi::kOpenTruncate);
  EXPECT(bulk.ok());
  Handle bh = static_cast<Handle>(bulk.value);
  auto bulk_write = file_write(bh, g_big, sizeof g_big, 0);
  EXPECT(bulk_write.ok() && bulk_write.value == sizeof g_big);
  // Cross page boundaries and leave mixed dirty/cached contents before sync.
  auto patch = file_write(bh, g_big + 7, 8001, 4093);
  EXPECT(patch.ok() && patch.value == 8001);
  EXPECT(file_sync(bh) == Status::Ok);
  EXPECT(handle_close(bh) == Status::Ok);
  print("fs_test: files, directories, rights, sync ok\n");
  return 0;
}
