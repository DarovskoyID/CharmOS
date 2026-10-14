// mkdisk: образ диска для QEMU-тестов — GPT, раздел 1 (ESP, FAT32) и
// раздел 2 (корень, ext2 или FAT32), файлы с хоста. Использует те же
// lib/fs, что и ядро (docs/abi/fs.md §3; журнал решений 2026-10-14).
//
//   mkdisk <образ> <МиБ> [--esp <МиБ>] [--root ext2|fat32] [--block <байт>]
//          [--put <1|2>:<путь в ФС>=<файл хоста>]...
#include <lib/fs/ext2.hpp>
#include <lib/fs/fat32.hpp>
#include <lib/fs/gpt.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

using namespace lib::fs;
using lib::core::StringView;

namespace {

class FileIo final : public Io {
 public:
  FileIo(int fd, uint64_t size) : fd_(fd), size_(size) {}
  Status read(uint64_t off, void* buf, size_t len) override {
    if (off + len > size_) return fail(FsError::IoError);
    return pread(fd_, buf, len, static_cast<off_t>(off)) == static_cast<ssize_t>(len) ? ok()
                                                                                      : fail(FsError::IoError);
  }
  Status write(uint64_t off, const void* buf, size_t len) override {
    if (off + len > size_) return fail(FsError::IoError);
    return pwrite(fd_, buf, len, static_cast<off_t>(off)) == static_cast<ssize_t>(len) ? ok()
                                                                                       : fail(FsError::IoError);
  }
  Status flush() override { return fsync(fd_) == 0 ? ok() : fail(FsError::IoError); }
  uint64_t size() const override { return size_; }

 private:
  int fd_;
  uint64_t size_;
};

class SubIo final : public Io {
 public:
  SubIo(Io& base, uint64_t off, uint64_t len) : base_(base), off_(off), len_(len) {}
  Status read(uint64_t o, void* b, size_t l) override {
    return o + l > len_ ? fail(FsError::IoError) : base_.read(off_ + o, b, l);
  }
  Status write(uint64_t o, const void* b, size_t l) override {
    return o + l > len_ ? fail(FsError::IoError) : base_.write(off_ + o, b, l);
  }
  Status flush() override { return base_.flush(); }
  uint64_t size() const override { return len_; }

 private:
  Io& base_;
  uint64_t off_, len_;
};

[[noreturn]] void die(const char* what, const char* detail = "") {
  std::fprintf(stderr, "mkdisk: %s%s%s\n", what, *detail ? ": " : "", detail);
  std::exit(1);
}

// Кладёт файл хоста по пути path (каталоги по пути создаются).
void put(FileSystem& fs, const std::string& path, const std::string& host) {
  FILE* f = std::fopen(host.c_str(), "rb");
  if (f == nullptr) die("cannot open", host.c_str());
  std::vector<uint8_t> data;
  uint8_t buf[65536];
  for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) data.insert(data.end(), buf, buf + n);
  std::fclose(f);

  Node dir = fs.root();
  size_t start = path[0] == '/' ? 1 : 0;
  for (;;) {
    size_t slash = path.find('/', start);
    std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
    StringView name(part.data(), part.size());
    if (slash == std::string::npos) {
      auto node = fs.create(dir, name, NodeType::File);
      if (node.is_err()) die("cannot create", path.c_str());
      if (!data.empty() && fs.write(node.value(), 0, data.data(), data.size()).is_err()) {
        die("cannot write", path.c_str());
      }
      return;
    }
    auto next = fs.lookup(dir, name);
    if (next.is_err()) next = fs.create(dir, name, NodeType::Directory);
    if (next.is_err() || next.value().type != NodeType::Directory) die("bad directory in", path.c_str());
    dir = next.value();
    start = slash + 1;
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) die("usage: mkdisk <image> <MiB> [--esp MiB] [--root ext2|fat32] [--block N] [--put p:path=file]");
  const char* image = argv[1];
  uint64_t mib = std::strtoull(argv[2], nullptr, 10);
  uint64_t esp_mib = 40;
  std::string root_fs = "ext2";
  uint32_t block = 1024;
  std::vector<std::pair<int, std::pair<std::string, std::string>>> puts;
  for (int i = 3; i < argc; ++i) {
    std::string a = argv[i];
    if (i + 1 >= argc) die("missing value for", a.c_str());
    std::string v = argv[++i];
    if (a == "--esp") {
      esp_mib = std::strtoull(v.c_str(), nullptr, 10);
    } else if (a == "--root") {
      root_fs = v;
    } else if (a == "--block") {
      block = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
    } else if (a == "--put") {
      size_t colon = v.find(':'), eq = v.find('=');
      if (colon != 1 || eq == std::string::npos || (v[0] != '1' && v[0] != '2')) die("bad --put", v.c_str());
      puts.push_back({v[0] - '0', {v.substr(2, eq - 2), v.substr(eq + 1)}});
    } else {
      die("unknown option", a.c_str());
    }
  }
  if (root_fs != "ext2" && root_fs != "fat32") die("--root must be ext2 or fat32");

  int fd = open(image, O_RDWR | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) die("cannot create", image);
  uint64_t bytes = mib * 1024 * 1024;
  if (ftruncate(fd, static_cast<off_t>(bytes)) != 0) die("cannot size", image);
  FileIo disk(fd, bytes);

  uint64_t sectors = bytes / 512;
  uint64_t esp_first = 2048, esp_last = esp_first + esp_mib * 2048 - 1;
  uint64_t root_first = esp_last + 1, root_last = sectors - 34 - 1;
  if (root_first + 2048 > root_last) die("disk too small");
  GptPartition parts[2] = {{kGuidEsp, {{0x11, 0x22, 0x33, 1}}, esp_first, esp_last},
                           {kGuidLinuxData, {{0x11, 0x22, 0x33, 2}}, root_first, root_last}};
  if (write_gpt(disk, parts, 2, Guid{{0x43, 0x48, 0x41, 0x52, 0x4d}}).is_err()) die("cannot write GPT");

  SubIo esp(disk, esp_first * 512, (esp_last - esp_first + 1) * 512);
  SubIo rootp(disk, root_first * 512, (root_last - root_first + 1) * 512);
  if (auto s = Fat32::format(esp, 1, "ESP"); s.is_err()) die("ESP format", error_name(s.error()));
  Status rs = root_fs == "ext2" ? Ext2::format(rootp, block, "charmos-root")
                                : Fat32::format(rootp, 1, "CHARMROOT");
  if (rs.is_err()) die("root format", error_name(rs.error()));

  Fat32 esp_fs;
  if (esp_fs.mount(&esp).is_err()) die("cannot mount new ESP");
  Fat32 root_fat;
  Ext2 root_ext;
  FileSystem* root = &root_ext;
  if (root_fs == "ext2") {
    if (root_ext.mount(&rootp).is_err()) die("cannot mount new root");
  } else {
    if (root_fat.mount(&rootp).is_err()) die("cannot mount new root");
    root = &root_fat;
  }
  for (auto& [part, pf] : puts) put(part == 1 ? static_cast<FileSystem&>(esp_fs) : *root, pf.first, pf.second);
  if (esp_fs.sync().is_err() || root->sync().is_err()) die("sync failed");
  close(fd);
  return 0;
}
