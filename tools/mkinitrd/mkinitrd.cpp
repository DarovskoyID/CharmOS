// Сборка initrd (docs/abi/initrd.md) на хосте.
//   mkinitrd <выход> <имя>=<путь> [<имя>=<путь> ...]

#include <abi/initrd.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

bool read_file(const char* path, std::vector<unsigned char>* out) {
  std::FILE* f = std::fopen(path, "rb");
  if (f == nullptr) return false;
  unsigned char buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out->insert(out->end(), buf, buf + n);
  bool ok = std::ferror(f) == 0;
  std::fclose(f);
  return ok;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <out> <name>=<path>...\n", argv[0]);
    return 2;
  }
  struct Input {
    std::string name;
    std::vector<unsigned char> data;
  };
  std::vector<Input> inputs;
  for (int i = 2; i < argc; ++i) {
    const char* eq = std::strchr(argv[i], '=');
    if (eq == nullptr || eq == argv[i]) {
      std::fprintf(stderr, "mkinitrd: bad argument '%s' (want name=path)\n", argv[i]);
      return 2;
    }
    Input in;
    in.name.assign(argv[i], static_cast<size_t>(eq - argv[i]));
    if (in.name.size() >= abi::initrd::kNameSize) {
      std::fprintf(stderr, "mkinitrd: name too long: %s\n", in.name.c_str());
      return 2;
    }
    if (!read_file(eq + 1, &in.data)) {
      std::fprintf(stderr, "mkinitrd: cannot read %s\n", eq + 1);
      return 1;
    }
    inputs.push_back(std::move(in));
  }

  auto align = [](uint64_t v) {
    return (v + abi::initrd::kDataAlign - 1) / abi::initrd::kDataAlign * abi::initrd::kDataAlign;
  };
  std::vector<abi::initrd::Entry> entries(inputs.size());
  uint64_t offset = align(sizeof(abi::initrd::Header) + entries.size() * sizeof(abi::initrd::Entry));
  for (size_t i = 0; i < inputs.size(); ++i) {
    std::memset(&entries[i], 0, sizeof entries[i]);
    std::memcpy(entries[i].name, inputs[i].name.data(), inputs[i].name.size());
    entries[i].offset = offset;
    entries[i].size = inputs[i].data.size();
    offset = align(offset + inputs[i].data.size());
  }

  abi::initrd::Header h;
  std::memset(&h, 0, sizeof h);
  std::memcpy(h.magic, abi::initrd::kMagic, sizeof h.magic);
  h.version = abi::initrd::kVersion;
  h.entry_count = static_cast<uint32_t>(entries.size());
  h.total_size = offset;

  std::vector<unsigned char> image(offset, 0);
  std::memcpy(image.data(), &h, sizeof h);
  std::memcpy(image.data() + sizeof h, entries.data(), entries.size() * sizeof(abi::initrd::Entry));
  for (size_t i = 0; i < inputs.size(); ++i) {
    std::memcpy(image.data() + entries[i].offset, inputs[i].data.data(), inputs[i].data.size());
  }

  std::FILE* f = std::fopen(argv[1], "wb");
  if (f == nullptr || std::fwrite(image.data(), 1, image.size(), f) != image.size()) {
    std::fprintf(stderr, "mkinitrd: cannot write %s\n", argv[1]);
    return 1;
  }
  std::fclose(f);
  return 0;
}
