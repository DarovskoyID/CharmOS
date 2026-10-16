#include <abi/process.hpp>
#include "test.hpp"
namespace ap = abi::process;
TEST(start_versions_preserve_legacy_and_all_roles) {
  ap::StartHeaderV2 h{}, decoded{}; uint32_t offset = 0;
  char message[128]{};
  h.argc = 1; h.args_bytes = 2; h.envc = 1; h.env_bytes = 4;
  h.handle_count = 8;
  for (uint32_t i = 0; i < 16; ++i) h.roles[i] = i + 1;
  CHECK(ap::pack_start_header(h, false, message) == 56);
  CHECK(ap::unpack_start_header(message, 62, &decoded, &offset));
  CHECK(offset == 56 && decoded.roles[7] == 8 && decoded.roles[8] == 0 && decoded.env_bytes == 4);
  h.handle_count = 16;
  CHECK(ap::pack_start_header(h, true, message) == 88);
  CHECK(ap::unpack_start_header(message, 94, &decoded, &offset));
  CHECK(offset == 88 && decoded.handle_count == 16 && decoded.roles[15] == 16 && decoded.envc == 1);
  CHECK(!ap::unpack_start_header(message, 87, &decoded, &offset));
  CHECK(!ap::unpack_start_header(message, 93, &decoded, &offset));
  h.handle_count = 17; ap::pack_start_header(h, true, message);
  CHECK(!ap::unpack_start_header(message, 94, &decoded, &offset));
  h.handle_count = 9; ap::pack_start_header(h, false, message);
  CHECK(!ap::unpack_start_header(message, 62, &decoded, &offset));
  message[0] = '?'; CHECK(!ap::unpack_start_header(message, 62, &decoded, &offset));
}
