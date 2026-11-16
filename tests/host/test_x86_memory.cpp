#include <assert.h>
#include <initializer_list>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>

extern "C" void* charmos_x86_memcpy(void*, const void*, size_t);
extern "C" void* charmos_x86_memset(void*, int, size_t);

int main() {
  auto* region = static_cast<unsigned char*>(mmap(nullptr, 3 * 4096,
      PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  assert(region != MAP_FAILED && mprotect(region + 4096, 4096, PROT_READ | PROT_WRITE) == 0);
  unsigned char output[4096 + 64];
  for (size_t size = 0; size <= 4096; ++size) {
    auto* boundary = region + 8192 - size;
    memset(region + 4096, 0x39, 4096);
    for (size_t offset : {size_t{0}, size_t{1}, size_t{7}, size_t{31}}) {
      memset(output, 0x75, sizeof output);
      assert(charmos_x86_memcpy(output + offset, boundary, size) == output + offset);
      for (size_t i = 0; i < sizeof output; ++i)
        assert(output[i] == (i >= offset && i - offset < size ? 0x39 : 0x75));
    }
    assert(charmos_x86_memset(boundary, 0x1a7, size) == boundary);
    for (size_t i = 0; i < size; ++i) assert(boundary[i] == 0xa7);
  }
  assert(munmap(region, 3 * 4096) == 0);
}
