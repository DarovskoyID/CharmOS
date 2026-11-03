#include <lib/fs/ext2.hpp>
#include <lib/fs/fat32.hpp>
#include <lib/fs/gpt.hpp>

#include <cstdlib>
#include <vector>

#include "test.hpp"

using lib::core::StringView;
using namespace lib::fs;

namespace {

class MemIo final : public Io {
 public:
  explicit MemIo(size_t bytes) : data(bytes, 0) {}
  Status read(uint64_t off, void* buf, size_t len) override {
    if (off + len > data.size()) return fail(FsError::IoError);
    std::memcpy(buf, data.data() + off, len);
    return ok();
  }
  Status write(uint64_t off, const void* buf, size_t len) override {
    if (off + len > data.size()) return fail(FsError::IoError);
    std::memcpy(data.data() + off, buf, len);
    return ok();
  }
  Status flush() override { return ok(); }
  uint64_t size() const override { return data.size(); }
  std::vector<uint8_t> data;
};

// Окно в другой Io (раздел).
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

StringView sv(const char* s) { return StringView(s); }

// Общий сценарий для любой ФС: каталоги, файлы, длинные имена, дыры,
// усечение, удаление, перечисление.
void exercise(FileSystem& fs, bool case_insensitive) {
  Node root = fs.root();
  auto docs = fs.create(root, sv("Documents and Settings"), NodeType::Directory);
  CHECK(docs.is_ok());
  auto other = fs.create(root, sv("documents and settings"), NodeType::File);
  if (case_insensitive) {
    CHECK(other.error() == FsError::AlreadyExists);  // FAT: регистр не различается
  } else {
    CHECK(other.is_ok() && fs.unlink(root, sv("documents and settings")).is_ok());
  }
  auto f = fs.create(docs.value(), sv("hello.txt"), NodeType::File);
  CHECK(f.is_ok());
  CHECK(fs.create(docs.value(), sv("hello.txt"), NodeType::File).error() == FsError::AlreadyExists);
  CHECK(fs.create(root, sv(".."), NodeType::File).error() == FsError::InvalidName);

  // Запись через несколько кластеров/блоков и чтение обратно.
  std::vector<uint8_t> big(300 * 1024);
  for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<uint8_t>(i * 31 + (i >> 9));
  CHECK(fs.write(f.value(), 0, big.data(), big.size()).is_ok());
  std::vector<uint8_t> back(big.size());
  auto r = fs.read(f.value(), 0, back.data(), back.size());
  CHECK(r.is_ok() && r.value() == big.size() && back == big);
  auto st = fs.stat(f.value());
  CHECK(st.is_ok() && st.value().size == big.size() && st.value().type == NodeType::File);

  // Запись за концом: дыра читается нулями.
  const char tail[] = "tail";
  CHECK(fs.write(f.value(), big.size() + 5000, tail, 4).is_ok());
  uint8_t mid[16];
  CHECK(fs.read(f.value(), big.size() + 100, mid, 16).value() == 16);
  bool zeros = true;
  for (uint8_t b : mid) zeros = zeros && b == 0;
  CHECK(zeros);
  char t4[4];
  CHECK(fs.read(f.value(), big.size() + 5000, t4, 4).value() == 4 && std::memcmp(t4, tail, 4) == 0);

  // Усечение и рост.
  CHECK(fs.resize(f.value(), 1000).is_ok());
  CHECK(fs.stat(f.value()).value().size == 1000);
  CHECK(fs.resize(f.value(), 9000).is_ok());
  uint8_t after[16];
  CHECK(fs.read(f.value(), 1000, after, 16).value() == 16);
  zeros = true;
  for (uint8_t b : after) zeros = zeros && b == 0;
  CHECK(zeros);
  CHECK(fs.read(f.value(), 0, back.data(), 1000).value() == 1000 &&
        std::memcmp(back.data(), big.data(), 1000) == 0);

  // Поиск и перечисление.
  auto found = fs.lookup(root, sv("Documents and Settings"));
  CHECK(found.is_ok() && found.value().type == NodeType::Directory);
  CHECK(fs.lookup(root, sv("nope")).error() == FsError::NotFound);
  for (int i = 0; i < 40; ++i) {
    char name[32];
    std::snprintf(name, sizeof name, "file number %02d with a long name", i);
    CHECK(fs.create(docs.value(), sv(name), NodeType::File).is_ok());
  }
  int count = 0;
  bool saw_hello = false;
  uint64_t cookie = 0;
  for (;;) {
    DirEntry e;
    uint64_t next;
    CHECK(fs.read_dir(docs.value(), cookie, &e, &next).is_ok());
    if (next == 0) break;
    ++count;
    saw_hello = saw_hello || StringView(e.name, e.name_len) == sv("hello.txt");
    cookie = next;
  }
  CHECK(count == 41 && saw_hello);

  // Удаление: непустой каталог нельзя; файл — да; затем каталог.
  CHECK(fs.unlink(root, sv("Documents and Settings")).error() == FsError::NotEmpty);
  CHECK(fs.unlink(docs.value(), sv("hello.txt")).is_ok());
  CHECK(fs.lookup(docs.value(), sv("hello.txt")).error() == FsError::NotFound);
  for (int i = 0; i < 40; ++i) {
    char name[32];
    std::snprintf(name, sizeof name, "file number %02d with a long name", i);
    CHECK(fs.unlink(docs.value(), sv(name)).is_ok());
  }
  CHECK(fs.unlink(root, sv("Documents and Settings")).is_ok());
  CHECK(fs.lookup(root, sv("Documents and Settings")).error() == FsError::NotFound);
  CHECK(fs.sync().is_ok());
}

}  // namespace

TEST(fs_crc32) {
  CHECK(crc32("123456789", 9) == 0xcbf43926u);  // контрольное значение CRC-32
}

TEST(fs_gpt_roundtrip) {
  MemIo disk(8 * 1024 * 1024);
  GptPartition parts[2] = {{kGuidEsp, {{1}}, 2048, 4095}, {kGuidLinuxData, {{2}}, 4096, 16000}};
  CHECK(write_gpt(disk, parts, 2, Guid{{9}}).is_ok());
  CHECK(disk.data[510] == 0x55 && disk.data[511] == 0xaa && disk.data[446 + 4] == 0xee);
  GptInfo info;
  CHECK(read_gpt(disk, &info).is_ok());
  CHECK(info.count == 2 && info.parts[0].type == kGuidEsp && info.parts[1].last_lba == 16000);
  disk.data[2 * 512 + 40] ^= 1;  // порча массива — CRC
  CHECK(read_gpt(disk, &info).error() == FsError::Corrupt);
}

TEST(fs_fat32) {
  MemIo io(48 * 1024 * 1024);
  CHECK(Fat32::format(io, 1, "CHARM").is_ok());
  Fat32 fs;
  CHECK(fs.mount(&io).is_ok());
  CHECK(fs.cluster_count() >= 65525);
  uint32_t free_before = fs.free_clusters().value();
  exercise(fs, true);
  CHECK(fs.free_clusters().value() == free_before);  // всё освобождено
  // Повторное монтирование видит то же.
  auto f = fs.create(fs.root(), sv("keep.bin"), NodeType::File);
  CHECK(f.is_ok() && fs.write(f.value(), 0, "abc", 3).is_ok() && fs.sync().is_ok());
  Fat32 again;
  CHECK(again.mount(&io).is_ok());
  auto k = again.lookup(again.root(), sv("KEEP.BIN"));
  char b[3];
  CHECK(k.is_ok() && again.read(k.value(), 0, b, 3).value() == 3 && std::memcmp(b, "abc", 3) == 0);
  // Слишком маленький том FAT32 не форматируется.
  MemIo small(4 * 1024 * 1024);
  CHECK(Fat32::format(small, 1, "X").is_err());
}

TEST(fs_ext2) {
  for (uint32_t bs : {1024u, 4096u}) {
    MemIo io(40 * 1024 * 1024);
    CHECK(Ext2::format(io, bs, "charm").is_ok());
    Ext2 fs;
    CHECK(fs.mount(&io).is_ok());
    CHECK(fs.lookup(fs.root(), sv("lost+found")).is_ok());
    uint32_t blocks_before = fs.free_blocks(), inodes_before = fs.free_inodes();
    exercise(fs, false);
    CHECK(fs.free_blocks() == blocks_before);
    CHECK(fs.free_inodes() == inodes_before);
    // Большой файл через двойные косвенные блоки.
    auto f = fs.create(fs.root(), sv("big"), NodeType::File);
    CHECK(f.is_ok());
    uint64_t far = uint64_t{bs} * (12 + bs / 4 + 10);
    CHECK(fs.write(f.value(), far, "xyz", 3).is_ok());
    char b[3];
    CHECK(fs.read(f.value(), far, b, 3).value() == 3 && std::memcmp(b, "xyz", 3) == 0);
    CHECK(fs.unlink(fs.root(), sv("big")).is_ok());
    CHECK(fs.free_blocks() == blocks_before);
    CHECK(fs.sync().is_ok());
    // Неизвестная несовместимая возможность — отказ.
    io.data[1024 + 96] |= 0x10;
    Ext2 bad;
    CHECK(bad.mount(&io).error() == FsError::Unsupported);
  }
}

TEST(fs_gpt_with_partitions) {
  MemIo disk(100 * 1024 * 1024);
  uint64_t sectors = disk.size() / 512;
  GptPartition parts[2] = {{kGuidEsp, {{1}}, 2048, 2048 + 34 * 2048 - 1},
                           {kGuidLinuxData, {{2}}, 2048 + 34 * 2048, sectors - 40}};
  CHECK(write_gpt(disk, parts, 2, Guid{{3}}).is_ok());
  GptInfo info;
  CHECK(read_gpt(disk, &info).is_ok() && info.count == 2);
  SubIo esp(disk, info.parts[0].first_lba * 512, (info.parts[0].last_lba - info.parts[0].first_lba + 1) * 512);
  SubIo rootp(disk, info.parts[1].first_lba * 512, (info.parts[1].last_lba - info.parts[1].first_lba + 1) * 512);
  CHECK(Fat32::format(esp, 1, "ESP").is_ok());
  CHECK(Ext2::format(rootp, 1024, "root").is_ok());
  Fat32 fat;
  Ext2 ext;
  CHECK(fat.mount(&esp).is_ok() && ext.mount(&rootp).is_ok());
  CHECK(read_gpt(disk, &info).is_ok());  // ФС не задели таблицу
}

TEST(ext2_regular_rename_identity_and_replacement) {
  for(uint32_t block_size:{1024u,2048u,4096u}) {
    MemIo storage(32*1024*1024);
    CHECK(Ext2::format(storage,block_size,"rename").is_ok());
    Ext2 fs;
    CHECK(fs.mount(&storage).is_ok());
    auto root=fs.root();
    uint32_t initial_blocks=fs.free_blocks(),initial_inodes=fs.free_inodes();
    auto folder=fs.create(root,sv("folder"),NodeType::Directory);
    auto source=fs.create(root,sv("before"),NodeType::File);
    CHECK(folder.is_ok() && source.is_ok());
    std::vector<uint8_t> contents(6000,71),back(contents.size());
    CHECK(fs.write(source.value(),0,contents.data(),contents.size()).is_ok());
    CHECK(fs.rename(root,sv("before"),folder.value(),sv("moved")).is_ok());
    CHECK(fs.lookup(root,sv("before")).error()==FsError::NotFound);
    CHECK(fs.lookup(folder.value(),sv("moved")).value().id==source.value().id);
    CHECK(fs.read(source.value(),0,back.data(),back.size()).value()==back.size() && back==contents);
    CHECK(fs.rename(folder.value(),sv("moved"),folder.value(),sv("moved")).is_ok());
    CHECK(fs.rename(folder.value(),sv("moved"),root,sv("back")).is_ok());
    auto replaced=fs.create(root,sv("replace"),NodeType::File);
    CHECK(replaced.is_ok());
    std::vector<uint8_t> old(3072,12);
    CHECK(fs.write(replaced.value(),0,old.data(),old.size()).is_ok());
    uint32_t before_blocks=fs.free_blocks(),before_inodes=fs.free_inodes();
    CHECK(fs.rename(root,sv("back"),root,sv("replace")).is_ok());
    CHECK(fs.lookup(root,sv("replace")).value().id==source.value().id);
    CHECK(fs.lookup(root,sv("back")).error()==FsError::NotFound);
    CHECK(fs.free_inodes()==before_inodes+1);
    CHECK(fs.free_blocks()==before_blocks+(3072+block_size-1)/block_size);
    CHECK(fs.rename(root,sv("replace"),root,sv("folder")).error()==FsError::IsDirectory);
    CHECK(fs.rename(root,sv("folder"),root,sv("other-folder")).error()==FsError::Unsupported);
    CHECK(fs.rename(root,sv("folder"),root,sv("folder")).is_ok());
    CHECK(fs.rename(root,sv("missing"),root,sv("name")).error()==FsError::NotFound);
    CHECK(fs.rename(root,sv("replace"),root,sv("..")).error()==FsError::InvalidName);
    CHECK(fs.unlink(root,sv("replace")).is_ok());
    CHECK(fs.unlink(root,sv("folder")).is_ok());
    CHECK(fs.free_blocks()==initial_blocks && fs.free_inodes()==initial_inodes);
  }
}

TEST(ext2_repeated_rename_does_not_leak_inodes) {
  MemIo storage(32*1024*1024);
  CHECK(Ext2::format(storage,1024,"rename-loop").is_ok());
  Ext2 fs; CHECK(fs.mount(&storage).is_ok());
  auto root=fs.root();
  uint32_t initial_inodes=fs.free_inodes();
  auto file=fs.create(root,sv("left"),NodeType::File); CHECK(file.is_ok());
  uint32_t initial_blocks=fs.free_blocks();
  for(unsigned i=0;i<200;++i) {
    auto from=i%2 ? sv("right") : sv("left");
    auto to=i%2 ? sv("left") : sv("right");
    CHECK(fs.rename(root,from,root,to).is_ok());
    CHECK(fs.lookup(root,from).error()==FsError::NotFound);
    CHECK(fs.lookup(root,to).value().id==file.value().id);
    CHECK(fs.free_inodes()==initial_inodes-1 && fs.free_blocks()==initial_blocks);
  }
  CHECK(fs.unlink(root,sv("left")).is_ok() && fs.free_inodes()==initial_inodes);
}

TEST(ext2_rename_write_failures_keep_source_data_reachable) {
  class FailingIo final : public Io {
   public:
    explicit FailingIo(Io& storage):storage_(storage) {}
    Status read(uint64_t offset,void* data,size_t length) override { return storage_.read(offset,data,length); }
    Status write(uint64_t offset,const void* data,size_t length) override {
      if(armed_ && writes_++==failure_) { armed_=false; failed_=true; return fail(FsError::IoError); }
      return storage_.write(offset,data,length);
    }
    Status flush() override { return storage_.flush(); }
    uint64_t size() const override { return storage_.size(); }
    void arm(unsigned failure) { writes_=0; failure_=failure; armed_=true; failed_=false; }
    void disarm() { armed_=false; }
    bool failed() const { return failed_; }
   private:
    Io& storage_;
    unsigned failure_=0,writes_=0;
    bool armed_=false,failed_=false;
  };
  for(bool replace:{false,true}) {
    MemIo storage(32*1024*1024);
    CHECK(Ext2::format(storage,1024,"rename-io").is_ok());
    Ext2 initial; CHECK(initial.mount(&storage).is_ok());
    auto root=initial.root();
    auto folder=initial.create(root,sv("folder"),NodeType::Directory);
    auto file=initial.create(root,sv("source"),NodeType::File);
    CHECK(folder.is_ok() && file.is_ok());
    std::vector<uint8_t> content(2048,93),back(content.size());
    CHECK(initial.write(file.value(),0,content.data(),content.size()).is_ok());
    uint64_t old_destination=0;
    if(replace) {
      auto existing=initial.create(folder.value(),sv("target"),NodeType::File); CHECK(existing.is_ok());
      old_destination=existing.value().id;
      CHECK(initial.write(existing.value(),0,"old",3).is_ok());
    }
    CHECK(initial.sync().is_ok());
    auto baseline=storage.data;
    unsigned failed_cases=0;
    for(unsigned failure=0;failure<14;++failure) {
      storage.data=baseline;
      FailingIo io(storage);
      Ext2 fs; CHECK(fs.mount(&io).is_ok());
      io.arm(failure);
      auto outcome=fs.rename(root,sv("source"),folder.value(),sv("target"));
      if(io.failed()) { CHECK(outcome.is_err()); ++failed_cases; }
      io.disarm();
      auto source=fs.lookup(root,sv("source"));
      auto target=fs.lookup(folder.value(),sv("target"));
      if(source.is_ok()) {
        CHECK(source.value().id==file.value().id);
        if(replace) CHECK(target.is_ok() && target.value().id==old_destination);
        else CHECK(target.is_err() && target.error()==FsError::NotFound);
      } else {
        CHECK(source.error()==FsError::NotFound && target.is_ok() && target.value().id==file.value().id);
      }
      CHECK(fs.read(file.value(),0,back.data(),back.size()).value()==back.size() && back==content);
    }
    CHECK(failed_cases>=2);
  }
}
