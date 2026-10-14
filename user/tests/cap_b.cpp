// Получатель: читает чужую страницу через урезанную капабилити, после отзыва
// теряет и хендл, и отображение.
#include "check.hpp"

using namespace charm;

extern "C" int64_t charm_main(Handle ab) {
  char buf[64];
  Handle hs[4];
  uint32_t n, nh;
  EXPECT(read_msg(ab, buf, sizeof buf, hs, 4, &n, &nh) == Status::Ok);
  EXPECT(same(buf, n, "vmo") && nh == 1);
  Handle vmo = hs[0];

  // Права только сужаются.
  EXPECT(vmo_map(vmo, rights::kRead | rights::kWrite).status == Status::AccessDenied);
  EXPECT(handle_duplicate(vmo, rights::kRead).status == Status::AccessDenied);
  auto m = vmo_map(vmo, rights::kRead);
  EXPECT(m.ok());
  volatile const char* page = reinterpret_cast<const char*>(m.value);
  EXPECT(page[0] == 's' && page[7] == '4' && page[8] == '2');
  print("cap_b: read the shared page\n");
  EXPECT(send(ab, "read-ok") == Status::Ok);

  EXPECT(read_msg(ab, buf, sizeof buf, hs, 4, &n, &nh) == Status::Ok);
  EXPECT(same(buf, n, "revoked"));
  EXPECT(vmo_map(vmo, rights::kRead).status == Status::BadHandle);
  EXPECT(handle_close(vmo) == Status::BadHandle);
  print("cap_b: handle revoked, touching the old mapping\n");
  char c = page[0];  // отображение снято: ошибка страницы, процесс убит
  (void)c;
  print("cap_b: still alive (BUG)\n");
  return 0;
}
